/* hfsplus_dump -- what Executor's HFS+ reader makes of an image, for
 * comparing with other readers (tests/test_hfsplus_reader.py):
 *
 *   hfsplus_dump IMAGE                  every volume's header and tree, a JSON object per line
 *   hfsplus_dump IMAGE read CNID [rsrc] a file's data (or resource) fork to stdout
 *
 * --volume N picks the Nth HFS+ volume of a partitioned image (default 0). */
#include "reader.h"
#include "unicode.h"

#include <cstdio>
#include <cstring>
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
    auto image = FdImageReader::open(args[0], &err);
    if(!image)
    {
        fprintf(stderr, "%s\n", err.c_str());
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
