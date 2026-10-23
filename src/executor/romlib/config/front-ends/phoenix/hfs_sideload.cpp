/*
 * hfs_sideload.cpp - see hfs_sideload.h. The HFS reader follows Inside
 * Macintosh: Files, chapter 2 ("Data Organization on Volumes"): the master
 * directory block, the catalog and extents overflow B-trees, allocation
 * blocks.
 */
#include "hfs_sideload.h"

#include <util/macstrings.h>
#include <file/localvolume/item.h>
#include <rsys/filesystem.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

using namespace Executor;

namespace {

uint16_t be16(const uint8_t *p) { return p[0] << 8 | p[1]; }
uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

struct Extent
{
    uint16_t start, count;
};

struct Entry
{
    std::string name;  // MacRoman, no length byte
    bool folder = false;
    uint32_t id = 0;   // file number or directory ID
    // Files:
    uint8_t finfo[16] = {}, fxinfo[16] = {};
    uint32_t dataLen = 0, rsrcLen = 0, modDate = 0;
    Extent dataExt[3] = {}, rsrcExt[3] = {};
};

class HfsVolume
{
public:
    explicit HfsVolume(const std::string& path) : f_(fopen(path.c_str(), "rb")) {}
    ~HfsVolume()
    {
        if(f_)
            fclose(f_);
    }

    bool open(std::string *error)
    {
        if(!f_)
            return fail(error, "cannot open the image");
        if(!findVolume(error))
            return false;
        const uint8_t *m = mdb_;
        blockSize_ = be32(m + 20);
        firstBlock_ = be16(m + 28);
        blessed_ = be32(m + 92);  // drFndrInfo[0]: the System Folder
        uint32_t xtSize = be32(m + 130), ctSize = be32(m + 146);
        readExtents(m + 134, xtExt_);
        readExtents(m + 150, ctExt_);
        extentsFile_ = fork(xtExt_, xtSize, 3, 0);
        catalogFile_ = fork(ctExt_, ctSize, 4, 0);
        if(catalogFile_.size() < 512)
            return fail(error, "no catalog");
        readCatalog();
        return true;
    }

    uint32_t blessed() const { return blessed_; }

    // The entries in a folder.
    const std::vector<Entry>& children(uint32_t dirID) { return tree_[dirID]; }

    std::vector<uint8_t> dataFork(const Entry& e) { return fork(e.dataExt, e.dataLen, e.id, 0x00); }
    std::vector<uint8_t> rsrcFork(const Entry& e) { return fork(e.rsrcExt, e.rsrcLen, e.id, 0xFF); }

private:
    static bool fail(std::string *error, const char *why)
    {
        if(error)
            *error = why;
        return false;
    }

    bool readAt(uint64_t offset, void *buf, size_t n)
    {
        return fseeko(f_, (off_t)offset, SEEK_SET) == 0 && fread(buf, 1, n, f_) == n;
    }

    // The HFS volume: at the start of the image, or in an Apple partition
    // map's first Apple_HFS partition.
    bool findVolume(std::string *error)
    {
        for(uint64_t base : candidates())
        {
            if(!readAt(base + 1024, mdb_, sizeof mdb_))
                continue;
            if(be16(mdb_) != 0x4244)  // 'BD'
                continue;
            if(be16(mdb_ + 124) == 0x482B)  // 'H+' embedded: HFS+
                return fail(error, "an HFS+ volume (only HFS so far)");
            volume_ = base;
            return true;
        }
        return fail(error, "no HFS volume");
    }

    std::vector<uint64_t> candidates()
    {
        std::vector<uint64_t> out{ 0 };
        uint8_t b[512];
        if(!readAt(0, b, 2) || be16(b) != 0x4552)  // 'ER': a driver descriptor
            return out;
        for(uint32_t i = 1; i < 64; i++)
        {
            if(!readAt(512ull * i, b, sizeof b) || be16(b) != 0x504D)  // 'PM'
                break;
            if(!strncmp((const char *)b + 48, "Apple_HFS", 32))
                out.insert(out.begin(), 512ull * be32(b + 8));
            if(i >= be32(b + 4))  // pmMapBlkCnt
                break;
        }
        return out;
    }

    static void readExtents(const uint8_t *p, Extent (&out)[3])
    {
        for(int i = 0; i < 3; i++)
            out[i] = { be16(p + 4 * i), be16(p + 4 * i + 2) };
    }

    uint64_t blockOffset(uint32_t block) const
    {
        return volume_ + 512ull * firstBlock_ + (uint64_t)block * blockSize_;
    }

    // A fork's bytes: its first three extents, then the extents overflow
    // file's records for it (key: fork type, file number, start block).
    std::vector<uint8_t> fork(const Extent (&first)[3], uint32_t length, uint32_t fileID, uint8_t type)
    {
        std::vector<uint8_t> out(length);
        uint32_t got = 0, fileBlock = 0;
        auto take = [&](const Extent& e) {
            for(uint32_t b = 0; b < e.count && got < length; b++)
            {
                uint32_t n = std::min<uint32_t>(blockSize_, length - got);
                if(!readAt(blockOffset(e.start + b), out.data() + got, n))
                    return false;
                got += n;
            }
            fileBlock += e.count;
            return true;
        };
        for(const Extent& e : first)
            if(e.count && !take(e))
                break;
        while(got < length)
        {
            auto more = overflow(fileID, type, fileBlock);
            if(!more)
                break;
            for(const Extent& e : *more)
                if(e.count && !take(e))
                    break;
        }
        out.resize(got);
        return out;
    }

    // Leaf records of a B-tree file, in order: (key, data) spans.
    template<class F>
    void leaves(const std::vector<uint8_t>& tree, F&& each)
    {
        if(tree.size() < 512)
            return;
        const uint8_t *hdr = tree.data() + 14;
        uint16_t nodeSize = be16(hdr + 18);
        if(!nodeSize)
            nodeSize = 512;
        uint32_t node = be32(hdr + 10);  // bthFNode: the first leaf
        for(int guard = 0; node && guard < 1 << 20; guard++)
        {
            if((uint64_t)(node + 1) * nodeSize > tree.size())
                break;
            const uint8_t *n = tree.data() + (size_t)node * nodeSize;
            uint16_t records = be16(n + 10);
            for(uint16_t r = 0; r < records; r++)
            {
                uint16_t off = be16(n + nodeSize - 2 * (r + 1));
                if(off + 1 >= nodeSize)
                    break;
                const uint8_t *key = n + off;
                const uint8_t *data = key + ((1 + key[0] + 1) & ~1);
                if(data >= n + nodeSize)
                    break;
                each(key, data, n + nodeSize - data);
            }
            node = be32(n);  // ndFLink
        }
    }

    std::optional<std::vector<Extent>> overflow(uint32_t fileID, uint8_t type, uint32_t fileBlock)
    {
        std::optional<std::vector<Extent>> found;
        leaves(extentsFile_, [&](const uint8_t *key, const uint8_t *data, size_t) {
            if(found || key[0] < 7 || key[1] != type || be32(key + 2) != fileID
               || be16(key + 6) != fileBlock)
                return;
            std::vector<Extent> v;
            for(int i = 0; i < 3; i++)
                v.push_back({ be16(data + 4 * i), be16(data + 4 * i + 2) });
            found = v;
        });
        return found;
    }

    void readCatalog()
    {
        leaves(catalogFile_, [&](const uint8_t *key, const uint8_t *data, size_t room) {
            if(key[0] < 6)
                return;
            uint32_t parent = be32(key + 2);
            Entry e;
            e.name.assign((const char *)key + 7, std::min<int>(key[6], 31));
            if(data[0] == 1 && room >= 10)       // a folder
            {
                e.folder = true;
                e.id = be32(data + 6);
            }
            else if(data[0] == 2 && room >= 98)  // a file
            {
                memcpy(e.finfo, data + 4, 16);
                e.id = be32(data + 20);
                e.dataLen = be32(data + 26);
                e.rsrcLen = be32(data + 36);
                e.modDate = be32(data + 48);
                memcpy(e.fxinfo, data + 56, 16);
                readExtents(data + 74, e.dataExt);
                readExtents(data + 86, e.rsrcExt);
            }
            else
                return;  // threads
            tree_[parent].push_back(std::move(e));
        });
    }

    FILE *f_;
    uint8_t mdb_[162] = {};
    uint64_t volume_ = 0;
    uint32_t blockSize_ = 512, blessed_ = 0;
    uint16_t firstBlock_ = 0;
    Extent xtExt_[3] = {}, ctExt_[3] = {};
    std::vector<uint8_t> extentsFile_, catalogFile_;
    std::map<uint32_t, std::vector<Entry>> tree_;
};

// AppleDouble version 2 with the Finder info (9) and resource fork (2), as
// Executor's local volume reads "%NAME".
std::vector<uint8_t> appledouble(const Entry& e, const std::vector<uint8_t>& rsrc)
{
    std::vector<uint8_t> out;
    auto put32 = [&](uint32_t v) {
        for(int s = 24; s >= 0; s -= 8)
            out.push_back((uint8_t)(v >> s));
    };
    auto put16 = [&](uint16_t v) {
        out.push_back(v >> 8);
        out.push_back((uint8_t)v);
    };
    put32(0x00051607);
    put32(0x00020000);
    out.insert(out.end(), 16, 0);
    put16(2);
    uint32_t header = 26 + 12 * 2;
    put32(9);
    put32(header);
    put32(32);
    put32(2);
    put32(header + 32);
    put32((uint32_t)rsrc.size());
    out.insert(out.end(), e.finfo, e.finfo + 16);
    out.insert(out.end(), e.fxinfo, e.fxinfo + 16);
    out.insert(out.end(), rsrc.begin(), rsrc.end());
    return out;
}

fs::path hostName(const std::string& mac)
{
    return toUnicodeFilename(mac_string_view((const unsigned char *)mac.data(), mac.size()));
}

/* The folder names asked for are ASCII; case doesn't matter, as in HFS. */
bool equalMac(const std::string& a, const char *b)
{
    if(a.size() != strlen(b))
        return false;
    for(size_t i = 0; i < a.size(); i++)
        if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}

void write(const fs::path& p, const std::vector<uint8_t>& bytes, std::time_t mod)
{
    fs::ofstream(p, std::ios::binary).write((const char *)bytes.data(), bytes.size());
    std::error_code ec;
    fs::last_write_time(p, mod, ec);
}

// One file into folder: its data fork and "%NAME".
void copyFile(HfsVolume& v, const Entry& e, const fs::path& folder)
{
    fs::path leaf = hostName(e.name);
    std::time_t mod = (std::time_t)macToHostTime(e.modDate);
    write(folder / leaf, v.dataFork(e), mod);
    write(folder / ("%" + leaf.string()), appledouble(e, v.rsrcFork(e)), mod);
}

void copyTree(HfsVolume& v, uint32_t dirID, const fs::path& folder)
{
    fs::create_directories(folder);
    for(const Entry& e : v.children(dirID))
        if(e.folder)
            copyTree(v, e.id, folder / hostName(e.name));
        else
            copyFile(v, e, folder);
}

}  // namespace

namespace executor_host {

bool sideload_system(const std::string& image, const std::string& data_dir,
                     bool *has_finder, std::string *error)
{
    HfsVolume v(image);
    if(!v.open(error))
        return false;
    if(!v.blessed())
    {
        if(error)
            *error = "no System Folder (the volume is not blessed)";
        return false;
    }
    fs::path sys = fs::path(data_dir) / "System Folder";
    fs::create_directories(sys);
    bool finder = false;
    static const std::vector<const char *> files = { "System", "Finder" };
    static const std::vector<const char *> folders = { "Preferences", "Fonts", "Apple Menu Items",
                                                       "Extensions", "Control Panels", "Startup Items" };
    for(const Entry& e : v.children(v.blessed()))
    {
        bool want = false;
        for(const char *n : e.folder ? folders : files)
            want |= equalMac(e.name, n);
        if(!want)
            continue;
        if(e.folder)
            copyTree(v, e.id, sys / hostName(e.name));
        else
        {
            copyFile(v, e, sys);
            finder |= equalMac(e.name, "Finder");
        }
    }
    if(has_finder)
        *has_finder = finder;
    return true;
}

}  // namespace executor_host
