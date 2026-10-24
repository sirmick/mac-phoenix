#include "reader.h"
#include "unicode.h"

#include <algorithm>
#include <cstring>

using namespace Executor::hfsplus;

/* ---- the B-trees (TN1150 "B-Trees") ---------------------------------- */

namespace
{
enum : int8_t
{
    kLeafNode = -1,
    kIndexNode = 0,
    kHeaderNode = 1,
};
enum : uint32_t
{
    kBTBigKeys = 1u << 1,
    kBTVariableIndexKeys = 1u << 2,
};
enum : int16_t
{
    kFolderRecord = 1,
    kFileRecord = 2,
    kFolderThreadRecord = 3,
    kFileThreadRecord = 4,
};
}

class Reader::BTree
{
public:
    using Node = std::shared_ptr<const std::vector<uint8_t>>;
    /* <0, 0, >0: the key sought against a node's key */
    using Compare = std::function<int(const uint8_t *key)>;

    struct Position
    {
        uint32_t node = 0;
        uint16_t record = 0;
    };

    BTree(Fork& file) : file_(file) {}

    bool open()
    {
        uint8_t h[512];
        if(file_.read(0, h, sizeof h) != sizeof h || int8_t(h[8]) != kHeaderNode)
            return false;
        const uint8_t *r = h + 14;
        depth_ = be16(r);
        root_ = be32(r + 2);
        firstLeaf_ = be32(r + 10);
        nodeSize_ = be16(r + 18);
        maxKeyLength_ = be16(r + 20);
        keyCompareType_ = r[37];
        attributes_ = be32(r + 38);
        return nodeSize_ >= 512 && (nodeSize_ & (nodeSize_ - 1)) == 0;
    }

    uint8_t keyCompareType() const { return keyCompareType_; }

    Node node(uint32_t n)
    {
        auto it = index_.find(n);
        if(it != index_.end())
        {
            lru_.splice(lru_.begin(), lru_, it->second);
            return it->second->second;
        }
        auto buf = std::make_shared<std::vector<uint8_t>>(nodeSize_);
        if(file_.read(uint64_t(n) * nodeSize_, buf->data(), nodeSize_) != nodeSize_)
            return nullptr;
        lru_.emplace_front(n, buf);
        index_[n] = lru_.begin();
        if(lru_.size() > 64)
        {
            index_.erase(lru_.back().first);
            lru_.pop_back();
        }
        return buf;
    }

    static int8_t kind(const Node& n) { return int8_t((*n)[8]); }
    static uint16_t count(const Node& n) { return be16(n->data() + 10); }
    static uint32_t next(const Node& n) { return be32(n->data()); }

    /* A record's key and data, and the bytes from the data to the node's end. */
    bool record(const Node& n, uint16_t i, const uint8_t **key, const uint8_t **data, size_t *room)
    {
        if(i >= count(n))
            return false;
        const uint8_t *base = n->data();
        uint16_t off = be16(base + nodeSize_ - 2 * (i + 1));
        if(off < 14 || off + 2 > nodeSize_)
            return false;
        size_t keyBytes = 2 + be16(base + off);
        if(kind(n) == kIndexNode && !(attributes_ & kBTVariableIndexKeys))
            keyBytes = 2 + maxKeyLength_;
        keyBytes = (keyBytes + 1) & ~size_t(1);
        if(off + keyBytes > nodeSize_)
            return false;
        *key = base + off;
        *data = base + off + keyBytes;
        *room = nodeSize_ - (off + keyBytes);
        return true;
    }

    /* The first leaf record whose key is >= the one sought. */
    bool seek(const Compare& cmp, Position *pos)
    {
        uint32_t n = root_;
        for(int guard = 0; n && guard < 32; guard++)
        {
            Node nd = node(n);
            if(!nd)
                return false;
            const uint8_t *key, *data;
            size_t room;
            if(kind(nd) == kLeafNode)
            {
                uint16_t i = 0;
                for(; i < count(nd); i++)
                    if(record(nd, i, &key, &data, &room) && cmp(key) <= 0)
                        break;
                pos->node = n;
                pos->record = i;
                return true;
            }
            if(kind(nd) != kIndexNode)
                return false;
            /* the last record whose key is <= the one sought (or the first) */
            uint32_t child = 0;
            for(uint16_t i = 0; i < count(nd); i++)
            {
                if(!record(nd, i, &key, &data, &room) || room < 4)
                    return false;
                if(i > 0 && cmp(key) < 0)
                    break;
                child = be32(data);
            }
            n = child;
        }
        return false;
    }

    /* The record at pos (moved on to the next leaf at a leaf's end). */
    bool at(Position *pos, const uint8_t **key, const uint8_t **data, size_t *room, Node *keep)
    {
        for(int guard = 0; guard < 1 << 20; guard++)
        {
            if(!pos->node)
                return false;
            Node nd = node(pos->node);
            if(!nd || kind(nd) != kLeafNode)
                return false;
            if(pos->record < count(nd))
            {
                *keep = nd;
                return record(nd, pos->record, key, data, room);
            }
            pos->node = next(nd);
            pos->record = 0;
        }
        return false;
    }

private:
    Fork& file_;
    uint16_t depth_ = 0, nodeSize_ = 0, maxKeyLength_ = 0;
    uint32_t root_ = 0, firstLeaf_ = 0, attributes_ = 0;
    uint8_t keyCompareType_ = 0;
    std::list<std::pair<uint32_t, Node>> lru_;
    std::unordered_map<uint32_t, std::list<std::pair<uint32_t, Node>>::iterator> index_;
};

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
       can go on in the extents file. */
    r->extentsFork_.reset(new Fork);
    r->extentsFork_->reader_ = r.get();
    r->extentsFork_->size_ = h.extentsFile.logicalSize;
    for(const Extent& e : h.extentsFile.extents)
        if(e.count)
            r->extentsFork_->extents_.push_back(e);
    r->extents_.reset(new BTree(*r->extentsFork_));
    if(!r->extents_->open())
        return fail("bad extents overflow file");
    r->catalogFork_.reset(new Fork);
    r->catalogFork_->reader_ = r.get();
    r->catalogFork_->size_ = h.catalogFile.logicalSize;
    r->fillExtents(*r->catalogFork_, kCatalogFileID, 0, h.catalogFile);
    r->catalog_.reset(new BTree(*r->catalogFork_));
    if(!r->catalog_->open())
        return fail("bad catalog file");
    r->binaryOrder_ = r->isHFSX() && r->catalog_->keyCompareType() == 0xBC;
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

CatalogEntry Reader::resolveLink(const CatalogEntry& e)
{
    if(e.folder || memcmp(e.userInfo, "hlnk", 4) || memcmp(e.userInfo + 4, "hfs+", 4))
        return e;
    if(!privateFolder_)
    {
        static const std::u16string name = std::u16string(4, u'\0') + u"HFS+ Private Data";
        auto f = lookup(kRootFolderID, name);
        privateFolder_ = f && f->folder ? f->cnid : 0;
    }
    if(!*privateFolder_)
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
