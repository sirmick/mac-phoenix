/* hfsplus_dump -- what Executor's HFS+ reader makes of an image, for
 * comparing with other readers (tests/test_hfsplus_reader.py), and its
 * writer driven by a script (tests/test_hfsplus_writer.py):
 *
 *   hfsplus_dump IMAGE                  every volume's header and tree, a JSON object per line
 *   hfsplus_dump IMAGE read CNID [rsrc] a file's data (or resource) fork to stdout
 *   hfsplus_dump IMAGE script FILE      changes, a line each (paths UTF-8, '/' between names;
 *                                       data bytes: (seed + 7 * offset + offset / 251) & 0xFF):
 *       mkdir PATH | create PATH | write PATH data|rsrc OFFSET LENGTH SEED
 *       truncate PATH data|rsrc LENGTH | rename PATH NEWNAME | move PATH FOLDER | delete PATH
 *
 * --volume N picks the Nth HFS+ volume of a partitioned image (default 0). */
#include "reader.h"
#include "unicode.h"

#include <fcntl.h>
#include <optional>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace Executor::hfsplus;

static std::string json(const std::string& s)
{
    std::string out = "\"";
    for(unsigned char c : s)
    {
        if(c == '"' || c == '\\')
            out += '\\', out += char(c);
        else if(c < 0x20)
        {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        }
        else
            out += char(c);
    }
    return out + "\"";
}

static std::string hex(const std::string& s)
{
    std::string out;
    char b[4];
    for(unsigned char c : s)
    {
        snprintf(b, sizeof b, "%02X", c);
        out += b;
    }
    return out;
}

/* hex: a type or creator can hold any byte */
static std::string ostype(const uint8_t *p)
{
    return "\"" + hex(std::string((const char *)p, 4)) + "\"";
}


static std::u16string fromUTF8(const std::string& s)
{
    std::u16string out;
    for(size_t i = 0; i < s.size();)
    {
        uint32_t c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        c &= n == 0 ? 0x7F : n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07;
        for(int k = 1; k <= n && i + k < s.size(); k++)
            c = c << 6 | ((unsigned char)s[i + k] & 0x3F);
        i += n + 1;
        if(c >= 0x10000)
        {
            c -= 0x10000;
            out += char16_t(0xD800 + (c >> 10));
            out += char16_t(0xDC00 + (c & 0x3FF));
        }
        else
            out += char16_t(c);
    }
    return out;
}

static std::optional<CatalogEntry> resolvePath(Reader& r, const std::string& path)
{
    std::optional<CatalogEntry> at = r.byID(kRootFolderID);
    std::stringstream ss(path);
    std::string part;
    while(at && std::getline(ss, part, '/'))
        if(!part.empty())
            at = r.lookup(at->cnid, fromUTF8(part));
    return at;
}

static const char *errorName(Error e)
{
    switch(e)
    {
        case Error::none: return "ok";
        case Error::readOnly: return "read-only";
        case Error::diskFull: return "disk full";
        case Error::exists: return "exists";
        case Error::notFound: return "not found";
        case Error::notEmpty: return "not empty";
        case Error::badMove: return "bad move";
        case Error::io: return "I/O error";
    }
    return "?";
}

/* One line of a script; false on a failure (reported). */
static bool runLine(Reader& r, const std::string& line)
{
    std::stringstream ss(line);
    std::string op;
    ss >> op;
    if(op.empty() || op[0] == '#')
        return true;
    auto arg = [&]() {
        std::string a;
        ss >> std::ws;
        if(ss.peek() == '"')
        {
            ss.get();
            std::getline(ss, a, '"');
        }
        else
            ss >> a;
        return a;
    };
    std::string path = arg();
    size_t slash = path.rfind('/');
    std::string dir = slash == std::string::npos ? "" : path.substr(0, slash), leaf = path.substr(slash + 1);
    Error e = Error::none;
    if(op == "mkdir" || op == "create")
    {
        auto parent = resolvePath(r, dir);
        e = parent ? r.create(parent->cnid, fromUTF8(leaf), op == "mkdir", nullptr, nullptr) : Error::notFound;
    }
    else
    {
        auto item = resolvePath(r, path);
        if(!item)
            e = Error::notFound;
        else if(op == "write")
        {
            bool rsrc = arg() == "rsrc";
            uint64_t off = std::stoull(arg()), len = std::stoull(arg());
            unsigned seed = (unsigned)std::stoul(arg());
            uint64_t size = (rsrc ? item->resourceFork : item->dataFork).logicalSize;
            e = r.setForkSize(item->cnid, rsrc, std::max(size, off + len));
            if(e == Error::none)
            {
                auto f = r.openFork(*r.byID(item->cnid), rsrc);
                std::vector<uint8_t> buf(len);
                for(uint64_t i = 0; i < len; i++)
                {
                    uint64_t o = off + i;
                    buf[i] = uint8_t(seed + 7 * o + o / 251);
                }
                e = f->write(off, buf.data(), buf.size()) ? Error::none : Error::io;
            }
        }
        else if(op == "truncate")
        {
            bool rsrc = arg() == "rsrc";
            e = r.setForkSize(item->cnid, rsrc, std::stoull(arg()));
        }
        else if(op == "rename")
            e = r.rename(item->cnid, fromUTF8(arg()));
        else if(op == "move")
        {
            auto to = resolvePath(r, arg());
            e = to ? r.move(item->cnid, to->cnid) : Error::notFound;
        }
        else if(op == "delete")
            e = r.remove(item->cnid);
        else
        {
            fprintf(stderr, "unknown: %s\n", line.c_str());
            return false;
        }
    }
    if(e != Error::none)
        printf("%s: %s\n", line.c_str(), errorName(e));
    return true;
}

static void tree(Reader& r, uint32_t dir, const std::string& path)
{
    for(const CatalogEntry& raw : r.children(dir))
    {
        CatalogEntry e = r.resolveLink(raw);
        std::string p = path + "/" + toUTF8(raw.name);
        printf("{\"path\": %s, \"name\": %s, \"kind\": \"%s\", \"cnid\": %u, \"parent\": %u, \"classic\": \"%s\"",
               json(p).c_str(), json(toUTF8(raw.name)).c_str(), e.folder ? "folder" : "file", raw.cnid, raw.parentID,
               hex(classicName(raw.name, raw.cnid)).c_str());
        if(e.folder)
            printf(", \"valence\": %u", e.valence);
        else
        {
            auto d = r.openFork(e, false), rs = r.openFork(e, true);
            printf(", \"data\": %llu, \"rsrc\": %llu, \"data_extents\": %zu, \"rsrc_extents\": %zu"
                   ", \"type\": %s, \"creator\": %s, \"link\": %s",
                   (unsigned long long)d->size(), (unsigned long long)rs->size(),
                   d->extents().size(), rs->extents().size(),
                   ostype(e.userInfo).c_str(), ostype(e.userInfo + 4).c_str(),
                   e.cnid != raw.cnid ? "true" : "false");
        }
        printf(", \"finder_flags\": %u, \"flags\": %u, \"created\": %u, \"modified\": %u}\n",
               unsigned(e.userInfo[8] << 8 | e.userInfo[9]), e.flags, e.createDate, e.contentModDate);
        if(e.folder)
            tree(r, e.cnid, p);
    }
}

int main(int argc, char **argv)
{
    int volume = 0;
    std::vector<std::string> args;
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "--volume") && i + 1 < argc)
            volume = atoi(argv[++i]);
        else
            args.push_back(argv[i]);
    }
    if(args.empty())
    {
        fprintf(stderr, "usage: hfsplus_dump [--volume N] IMAGE [read CNID [rsrc]]\n");
        return 2;
    }
    std::string err;
    bool scripting = args.size() >= 3 && args[1] == "script";
    std::shared_ptr<ImageReader> image;
    if(scripting)
    {
        int fd = open(args[0].c_str(), O_RDWR);
        if(fd >= 0)
            image = std::make_shared<FdImageReader>(fd, true, true);
    }
    else
        image = FdImageReader::open(args[0], &err);
    if(!image)
    {
        fprintf(stderr, "%s: can't open %s\n", args[0].c_str(), err.c_str());
        return 1;
    }
    std::vector<uint64_t> starts;
    for(uint64_t s : volumeStarts(*image))
        if(isHFSPlus(*image, s))
            starts.push_back(s);
    if(volume >= (int)starts.size())
    {
        fprintf(stderr, "%s: no HFS+ volume %d\n", args[0].c_str(), volume);
        return 1;
    }
    auto r = Reader::open(image, starts[volume], &err);
    if(!r)
    {
        fprintf(stderr, "%s: %s\n", args[0].c_str(), err.c_str());
        return 1;
    }

    if(scripting)
    {
        if(!r->writable())
        {
            fprintf(stderr, "%s: not writable (journaled, locked, or not cleanly unmounted)\n", args[0].c_str());
            return 1;
        }
        std::ifstream in(args[2]);
        std::string line;
        int failed = 0;
        while(std::getline(in, line))
            failed += !runLine(*r, line);
        r->flush();
        return failed ? 1 : 0;
    }

    if(args.size() >= 3 && args[1] == "read")
    {
        auto e = r->byID((uint32_t)strtoul(args[2].c_str(), nullptr, 0));
        if(!e || e->folder)
        {
            fprintf(stderr, "no file %s\n", args[2].c_str());
            return 1;
        }
        auto f = r->openFork(r->resolveLink(*e), args.size() >= 4 && args[3] == "rsrc");
        std::vector<char> buf(1 << 20);
        for(uint64_t at = 0; at < f->size();)
        {
            size_t n = f->read(at, buf.data(), buf.size());
            if(!n)
                return 1;
            fwrite(buf.data(), 1, n, stdout);
            at += n;
        }
        return 0;
    }

    const VolumeHeader& h = r->header();
    printf("{\"volume\": %s, \"signature\": \"%c%c\", \"wrapped\": %s, \"offset\": %llu, \"block_size\": %u"
           ", \"total_blocks\": %u, \"free_blocks\": %u, \"files\": %u, \"folders\": %u, \"next_cnid\": %u"
           ", \"journaled\": %s, \"blessed\": %u}\n",
           json(toUTF8(r->volumeName())).c_str(), h.signature >> 8, h.signature & 0xFF,
           r->wrapped() ? "true" : "false", (unsigned long long)r->volumeOffset(), h.blockSize,
           h.totalBlocks, h.freeBlocks, h.fileCount, h.folderCount, h.nextCatalogID,
           h.attributes & VolumeHeader::kJournaled ? "true" : "false", h.finderInfo[0]);
    tree(*r, kRootFolderID, "");
    return 0;
}
