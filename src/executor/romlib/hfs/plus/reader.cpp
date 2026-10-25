#include "reader.h"
#include "btree.h"
#include "unicode.h"

#include <algorithm>
#include <cstring>

using namespace Executor::hfsplus;

namespace
{
enum : int16_t
{
    kFolderRecord = 1,
    kFileRecord = 2,
    kFolderThreadRecord = 3,
    kFileThreadRecord = 4,
};
}

Reader::~Reader() = default;

/* ---- structures ------------------------------------------------------- */

ForkData ForkData::parse(const uint8_t *p)
{
    ForkData f;
    f.logicalSize = be64(p);
    f.clumpSize = be32(p + 8);
    f.totalBlocks = be32(p + 12);
    for(int i = 0; i < 8; i++)
        f.extents[i] = { be32(p + 16 + 8 * i), be32(p + 20 + 8 * i) };
    return f;
}

void ForkData::store(uint8_t *p) const
{
    put64(p, logicalSize);
    put32(p + 8, clumpSize);
    put32(p + 12, totalBlocks);
    for(int i = 0; i < 8; i++)
    {
        put32(p + 16 + 8 * i, extents[i].start);
        put32(p + 20 + 8 * i, extents[i].count);
    }
}

static std::u16string readUniStr(const uint8_t *p, size_t room)
{
    if(room < 2)
        return {};
    size_t n = std::min<size_t>(be16(p), (room - 2) / 2);
    std::u16string s(n, u'\0');
    for(size_t i = 0; i < n; i++)
        s[i] = char16_t(be16(p + 2 + 2 * i));
    return s;
}

std::optional<CatalogEntry> Reader::parseRecord(const uint8_t *key, const uint8_t *data, size_t room)
{
    if(room < 2)
        return std::nullopt;
    int16_t type = int16_t(be16(data));
    if(!(type == kFolderRecord && room >= 88) && !(type == kFileRecord && room >= 248))
        return std::nullopt;
    CatalogEntry e;
    e.folder = type == kFolderRecord;
    size_t keyLen = be16(key);
    e.parentID = be32(key + 2);
    e.name = readUniStr(key + 6, keyLen >= 4 ? keyLen - 4 : 0);
    e.flags = be16(data + 2);
    e.valence = e.folder ? be32(data + 4) : 0;
    e.cnid = be32(data + 8);
    e.createDate = be32(data + 12);
    e.contentModDate = be32(data + 16);
    e.attributeModDate = be32(data + 20);
    e.accessDate = be32(data + 24);
    e.backupDate = be32(data + 28);
    e.special = be32(data + 44);
    memcpy(e.userInfo, data + 48, 16);
    memcpy(e.finderInfo, data + 64, 16);
    e.textEncoding = be32(data + 80);
    if(!e.folder)
    {
        e.dataFork = ForkData::parse(data + 88);
        e.resourceFork = ForkData::parse(data + 168);
    }
    e.raw.assign(data, data + (e.folder ? 88 : 248));
    return e;
}

/* ---- opening ---------------------------------------------------------- */

bool Executor::hfsplus::isHFSPlus(ImageReader& image, uint64_t start)
{
    uint8_t m[130];
    if(!image.read(start + 1024, m, sizeof m))
        return false;
    uint16_t sig = be16(m);
    return sig == 0x482B || sig == 0x4858 || (sig == 0x4244 && be16(m + 124) == 0x482B);
}

std::vector<uint64_t> Executor::hfsplus::volumeStarts(ImageReader& image)
{
    std::vector<uint64_t> out;
    uint8_t b[512];
    if(image.read(0, b, sizeof b) && be16(b) == 0x4552)    /* 'ER': a driver descriptor */
    {
        uint32_t bs = be16(b + 2);
        if(bs < 512 || (bs & (bs - 1)))
            bs = 512;
        uint8_t pm[512];
        if(!image.read(bs, pm, 2) || be16(pm) != 0x504D)
            bs = 512;                       /* the map in 512-byte blocks whatever the device's */
        for(uint32_t i = 1, n = 1; i <= n && i < 256; i++)
        {
            if(!image.read(uint64_t(bs) * i, pm, sizeof pm) || be16(pm) != 0x504D)  /* 'PM' */
                break;
            n = be32(pm + 4);
            char type[33] = {};
            memcpy(type, pm + 48, 32);
            if(!strcmp(type, "Apple_HFS") || !strcmp(type, "Apple_HFSX"))
                out.push_back(uint64_t(bs) * be32(pm + 8));
        }
    }
    if(out.empty())
        out.push_back(0);
    return out;
}

std::unique_ptr<Reader> Reader::open(std::shared_ptr<ImageReader> image, uint64_t start, std::string *error)
{
    auto fail = [error](const char *why) {
        if(error)
            *error = why;
        return nullptr;
    };
    uint8_t m[512];
    if(!image->read(start + 1024, m, sizeof m))
        return fail("can't read the volume header");
    std::unique_ptr<Reader> r(new Reader);
    r->image_ = image;
    r->offset_ = start;
    if(be16(m) == 0x4244)                   /* 'BD': an HFS wrapper (TN1150 "HFS Wrapper") */
    {
        if(be16(m + 124) != 0x482B)
            return fail("an HFS volume, not HFS+");
        uint32_t alBlkSiz = be32(m + 20);
        uint16_t alBlSt = be16(m + 28);
        uint16_t embedStart = be16(m + 126);
        r->offset_ = start + uint64_t(alBlSt) * 512 + uint64_t(embedStart) * alBlkSiz;
        r->wrapped_ = true;
        if(!image->read(r->offset_ + 1024, m, sizeof m))
            return fail("can't read the embedded volume's header");
    }
    VolumeHeader& h = r->header_;
    memcpy(h.raw, m, sizeof h.raw);
    h.signature = be16(m);
    h.version = be16(m + 2);
    if(h.signature != 0x482B && h.signature != 0x4858)
        return fail("no HFS+ volume header");
    h.attributes = be32(m + 4);
    h.lastMountedVersion = be32(m + 8);
    h.journalInfoBlock = be32(m + 12);
    h.createDate = be32(m + 16);
    h.modifyDate = be32(m + 20);
    h.backupDate = be32(m + 24);
    h.checkedDate = be32(m + 28);
    h.fileCount = be32(m + 32);
    h.folderCount = be32(m + 36);
    h.blockSize = be32(m + 40);
    h.totalBlocks = be32(m + 44);
    h.freeBlocks = be32(m + 48);
    h.nextAllocation = be32(m + 52);
    h.rsrcClumpSize = be32(m + 56);
    h.dataClumpSize = be32(m + 60);
    h.nextCatalogID = be32(m + 64);
    h.writeCount = be32(m + 68);
    h.encodingsBitmap = be64(m + 72);
    for(int i = 0; i < 8; i++)
        h.finderInfo[i] = be32(m + 80 + 4 * i);
    h.allocationFile = ForkData::parse(m + 112);
    h.extentsFile = ForkData::parse(m + 192);
    h.catalogFile = ForkData::parse(m + 272);
    h.attributesFile = ForkData::parse(m + 352);
    h.startupFile = ForkData::parse(m + 432);
    if(h.blockSize < 512 || (h.blockSize & (h.blockSize - 1)))
        return fail("bad allocation block size");

    /* The extents file's own extents are all in the header; the catalog's
       and the allocation file's can go on in the extents file. */
    auto extentOrder = [](const uint8_t *a, const uint8_t *b) {
        uint32_t fa = be32(a + 4), fb = be32(b + 4), sa = be32(a + 8), sb = be32(b + 8);
        if(fa != fb)
            return fa < fb ? -1 : 1;
        if(a[2] != b[2])
            return a[2] < b[2] ? -1 : 1;
        if(sa != sb)
            return sa < sb ? -1 : 1;
        return 0;
    };
    Reader *self = r.get();
    auto catalogOrder = [self](const uint8_t *a, const uint8_t *b) {
        uint32_t pa = be32(a + 2), pb = be32(b + 2);
        if(pa != pb)
            return pa < pb ? -1 : 1;
        size_t la = be16(a), lb = be16(b);
        return self->compareNames(readUniStr(a + 6, la >= 4 ? la - 4 : 0), readUniStr(b + 6, lb >= 4 ? lb - 4 : 0));
    };
    r->extentsFork_.reset(new Fork);
    r->extentsFork_->reader_ = r.get();
    r->extentsFork_->size_ = h.extentsFile.logicalSize;
    for(const Extent& e : h.extentsFile.extents)
        if(e.count)
            r->extentsFork_->extents_.push_back(e);
    r->extents_.reset(new BTree(*r, *r->extentsFork_, kExtentsFileID, extentOrder));
    if(!r->extents_->open())
        return fail("bad extents overflow file");
    r->catalogFork_.reset(new Fork);
    r->catalogFork_->reader_ = r.get();
    r->catalogFork_->size_ = h.catalogFile.logicalSize;
    r->fillExtents(*r->catalogFork_, kCatalogFileID, 0, h.catalogFile);
    r->catalog_.reset(new BTree(*r, *r->catalogFork_, kCatalogFileID, catalogOrder));
    if(!r->catalog_->open())
        return fail("bad catalog file");
    r->binaryOrder_ = r->isHFSX() && r->catalog_->keyCompareType() == 0xBC;
    r->allocationFork_.reset(new Fork);
    r->allocationFork_->reader_ = r.get();
    r->allocationFork_->size_ = h.allocationFile.logicalSize;
    r->fillExtents(*r->allocationFork_, kAllocationFileID, 0, h.allocationFile);

    /* Changes only to a clean, unjournaled, unlocked volume, all its
       special files whole. Extended attributes (an attributes file, as
       mkfs.hfsplus makes) are left alone, as Mac OS 9 leaves them. */
    r->writable_ = image->writable() && !(h.attributes & (VolumeHeader::kJournaled | VolumeHeader::kSoftwareLock
                                                           | VolumeHeader::kHardwareLock))
                   && (h.attributes & VolumeHeader::kUnmounted) && !(h.attributes & VolumeHeader::kInconsistent)
                   && r->catalogFork_->complete_ && r->allocationFork_->complete_;
    return r;
}

/* ---- forks ------------------------------------------------------------ */

std::vector<Extent> Reader::overflowExtents(uint32_t fileID, uint8_t forkType, uint32_t startBlock)
{
    /* Key: keyLength 10, forkType, pad, fileID, startBlock; ordered by
       fileID, forkType, startBlock. */
    auto cmp = [&](const uint8_t *k) {
        uint32_t f = be32(k + 4), s = be32(k + 8);
        uint8_t t = k[2];
        if(fileID != f)
            return fileID < f ? -1 : 1;
        if(forkType != t)
            return forkType < t ? -1 : 1;
        if(startBlock != s)
            return startBlock < s ? -1 : 1;
        return 0;
    };
    BTree::Position pos;
    const uint8_t *key, *data;
    size_t room;
    BTree::Node keep;
    std::vector<Extent> out;
    if(extents_->seek(cmp, &pos) && extents_->at(&pos, &key, &data, &room, &keep) && cmp(key) == 0 && room >= 64)
        for(int i = 0; i < 8; i++)
            if(be32(data + 8 * i + 4))
                out.push_back({ be32(data + 8 * i), be32(data + 8 * i + 4) });
    return out;
}

void Reader::fillExtents(Fork& fork, uint32_t fileID, uint8_t forkType, const ForkData& fd)
{
    uint64_t blocks = 0;
    for(const Extent& e : fd.extents)
        if(e.count)
        {
            fork.extents_.push_back(e);
            blocks += e.count;
        }
    while(blocks < fd.totalBlocks)
    {
        auto more = overflowExtents(fileID, forkType, uint32_t(blocks));
        if(more.empty())
        {
            fork.complete_ = false;
            break;
        }
        for(const Extent& e : more)
        {
            fork.extents_.push_back(e);
            blocks += e.count;
        }
    }
}

size_t Fork::read(uint64_t offset, void *buf, size_t n)
{
    if(offset >= size_)
        return 0;
    n = size_t(std::min<uint64_t>(n, size_ - offset));
    const uint64_t bs = reader_->header_.blockSize;
    auto *out = static_cast<uint8_t *>(buf);
    size_t done = 0;
    uint64_t extentStart = 0;               /* the extent's first byte in the fork */
    for(const Extent& e : extents_)
    {
        uint64_t len = uint64_t(e.count) * bs;
        uint64_t pos = offset + done;
        if(pos < extentStart + len)
        {
            uint64_t within = pos - extentStart;
            size_t take = size_t(std::min<uint64_t>(n - done, len - within));
            if(!reader_->readAt(uint64_t(e.start) * bs + within, out + done, take))
                return done;
            done += take;
            if(done == n)
                return done;
        }
        extentStart += len;
    }
    return done;
}

uint64_t Fork::allocatedBytes() const
{
    uint64_t blocks = 0;
    for(const Extent& e : extents_)
        blocks += e.count;
    return blocks * reader_->header_.blockSize;
}

bool Fork::write(uint64_t offset, const void *buf, size_t n)
{
    const uint64_t bs = reader_->header_.blockSize;
    auto *in = static_cast<const uint8_t *>(buf);
    size_t done = 0;
    uint64_t extentStart = 0;
    for(const Extent& e : extents_)
    {
        if(done == n)
            break;
        uint64_t len = uint64_t(e.count) * bs;
        uint64_t pos = offset + done;
        if(pos < extentStart + len)
        {
            uint64_t within = pos - extentStart;
            size_t put = size_t(std::min<uint64_t>(n - done, len - within));
            if(!reader_->writeAt(uint64_t(e.start) * bs + within, in + done, put))
                return false;
            done += put;
        }
        extentStart += len;
    }
    return done == n;
}

std::unique_ptr<Fork> Reader::openFork(const CatalogEntry& file, bool resource)
{
    const ForkData& fd = resource ? file.resourceFork : file.dataFork;
    std::unique_ptr<Fork> f(new Fork);
    f->reader_ = this;
    f->size_ = fd.logicalSize;
    fillExtents(*f, file.cnid, resource ? 0xFF : 0x00, fd);
    return f;
}

/* ---- the catalog ------------------------------------------------------ */

int Reader::compareNames(const std::u16string& a, const std::u16string& b) const
{
    return binaryOrder_ ? binaryCompare(a.data(), a.size(), b.data(), b.size())
                        : fastUnicodeCompare(a.data(), a.size(), b.data(), b.size());
}

namespace
{
/* The catalog key sought (parent, name) against a node's key. */
struct CatalogKeyCompare
{
    uint32_t parent;
    const std::u16string& name;
    const Reader& reader;
    int operator()(const uint8_t *k) const
    {
        uint32_t p = be32(k + 2);
        if(parent != p)
            return parent < p ? -1 : 1;
        size_t keyLen = be16(k);
        std::u16string other = readUniStr(k + 6, keyLen >= 4 ? keyLen - 4 : 0);
        return reader.compareNames(name, other);
    }
};
}

std::optional<CatalogEntry> Reader::lookup(uint32_t parentID, const std::u16string& name)
{
    CatalogKeyCompare cmp{ parentID, name, *this };
    BTree::Position pos;
    const uint8_t *key, *data;
    size_t room;
    BTree::Node keep;
    if(!catalog_->seek(cmp, &pos) || !catalog_->at(&pos, &key, &data, &room, &keep) || cmp(key) != 0)
        return std::nullopt;
    return parseRecord(key, data, room);
}

std::optional<CatalogEntry> Reader::byID(uint32_t cnid)
{
    std::u16string empty;
    CatalogKeyCompare cmp{ cnid, empty, *this };
    BTree::Position pos;
    const uint8_t *key, *data;
    size_t room;
    BTree::Node keep;
    if(!catalog_->seek(cmp, &pos) || !catalog_->at(&pos, &key, &data, &room, &keep) || cmp(key) != 0 || room < 10)
        return std::nullopt;
    int16_t type = int16_t(be16(data));
    if(type != kFolderThreadRecord && type != kFileThreadRecord)
        return std::nullopt;
    uint32_t parent = be32(data + 4);
    std::u16string name = readUniStr(data + 8, room - 8);
    return lookup(parent, name);
}

std::u16string Reader::volumeName()
{
    auto root = byID(kRootFolderID);
    return root ? root->name : std::u16string();
}

void Reader::forEachChild(uint32_t parentID, const std::function<bool(const CatalogEntry&)>& f)
{
    std::u16string empty;
    CatalogKeyCompare cmp{ parentID, empty, *this };
    BTree::Position pos;
    if(!catalog_->seek(cmp, &pos))
        return;
    const uint8_t *key, *data;
    size_t room;
    BTree::Node keep;
    for(; catalog_->at(&pos, &key, &data, &room, &keep); pos.record++)
    {
        if(be32(key + 2) != parentID)
            return;
        if(auto e = parseRecord(key, data, room))
            if(!f(*e))
                return;
    }
}

std::vector<CatalogEntry> Reader::children(uint32_t parentID)
{
    std::vector<CatalogEntry> out;
    forEachChild(parentID, [&](const CatalogEntry& e) {
        out.push_back(e);
        return true;
    });
    return out;
}

std::optional<CatalogEntry> Reader::childAt(uint32_t parentID, uint32_t index)
{
    if(!index)
        return std::nullopt;
    BTree::Position pos;
    uint32_t at = 0;                        /* items counted before pos */
    if(cursor_.parentID == parentID && cursor_.index && cursor_.index <= index)
    {
        pos = { cursor_.node, cursor_.record };
        at = cursor_.index - 1;
    }
    else
    {
        std::u16string empty;
        CatalogKeyCompare cmp{ parentID, empty, *this };
        if(!catalog_->seek(cmp, &pos))
            return std::nullopt;
    }
    const uint8_t *key, *data;
    size_t room;
    BTree::Node keep;
    for(; catalog_->at(&pos, &key, &data, &room, &keep); pos.record++)
    {
        if(be32(key + 2) != parentID)
            break;
        auto e = parseRecord(key, data, room);
        if(e && ++at == index)
        {
            cursor_ = { parentID, index, pos.node, pos.record };
            return e;
        }
    }
    return std::nullopt;
}

const std::u16string Executor::hfsplus::kMetadataFolderName = std::u16string(4, u'\0') + u"HFS+ Private Data";

bool Reader::isLink(const CatalogEntry& e)
{
    return !e.folder && !memcmp(e.userInfo, "hlnk", 4) && !memcmp(e.userInfo + 4, "hfs+", 4);
}

CatalogEntry Reader::resolveLink(const CatalogEntry& e)
{
    if(!isLink(e) || !metadataFolder())
        return e;
    std::string n = "iNode" + std::to_string(e.special);
    auto target = lookup(*privateFolder_, std::u16string(n.begin(), n.end()));
    if(!target || target->folder)
        return e;
    CatalogEntry out = *target;             /* the file, under the link's name */
    out.parentID = e.parentID;
    out.name = e.name;
    return out;
}

std::vector<uint8_t> Executor::hfsplus::catalogKey(uint32_t parentID, const std::u16string& name)
{
    std::vector<uint8_t> k(8 + 2 * name.size());
    put16(k.data(), uint16_t(6 + 2 * name.size()));
    put32(k.data() + 2, parentID);
    put16(k.data() + 6, uint16_t(name.size()));
    for(size_t i = 0; i < name.size(); i++)
        put16(k.data() + 8 + 2 * i, name[i]);
    return k;
}

std::vector<uint8_t> Executor::hfsplus::extentKey(uint8_t forkType, uint32_t fileID, uint32_t startBlock)
{
    std::vector<uint8_t> k(12, 0);
    put16(k.data(), 10);
    k[2] = forkType;
    put32(k.data() + 4, fileID);
    put32(k.data() + 8, startBlock);
    return k;
}
