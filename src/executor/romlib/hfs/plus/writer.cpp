/* writer.cpp -- changes to an HFS+ volume (TN1150): the catalog's records
 * and threads, the allocation file, forks' extents (the first 8 in the
 * catalog record, the rest in the extents overflow file), the volume
 * header and its copy at the end of the volume. Every change is written
 * through at once; the volume is marked in use (kHFSVolumeUnmountedBit
 * clear) only while a change is being made, so a process killed between
 * changes leaves it clean. No journal: classic volumes have none. */
#include "reader.h"
#include "btree.h"

#include <algorithm>
#include <cstring>
#include <ctime>

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
const uint32_t kMacOS810 = 0x382E3130;     /* lastMountedVersion '8.10', as Mac OS 8.1-9 */

void merge(std::vector<Extent>& v)
{
    std::vector<Extent> out;
    for(const Extent& e : v)
    {
        if(!e.count)
            continue;
        if(!out.empty() && out.back().start + out.back().count == e.start)
            out.back().count += e.count;
        else
            out.push_back(e);
    }
    v = out;
}

uint32_t blocksIn(const std::vector<Extent>& v)
{
    uint32_t n = 0;
    for(const Extent& e : v)
        n += e.count;
    return n;
}

std::vector<uint8_t> threadRecord(bool folder, uint32_t parentID, const std::u16string& name)
{
    std::vector<uint8_t> d(10 + 2 * name.size(), 0);
    put16(d.data(), uint16_t(folder ? kFolderThreadRecord : kFileThreadRecord));
    put32(d.data() + 4, parentID);
    put16(d.data() + 8, uint16_t(name.size()));
    for(size_t i = 0; i < name.size(); i++)
        put16(d.data() + 10 + 2 * i, name[i]);
    return d;
}
} // namespace

uint32_t Reader::now()
{
    return uint32_t(int64_t(time(nullptr)) + 2082844800);
}

/* ---- the volume header --------------------------------------------------- */

Error Reader::writeHeader(bool alternate)
{
    uint8_t m[512];
    memcpy(m, header_.raw, sizeof m);
    const VolumeHeader& h = header_;
    put32(m + 4, h.attributes);
    put32(m + 8, h.lastMountedVersion);
    put32(m + 20, h.modifyDate);
    put32(m + 32, h.fileCount);
    put32(m + 36, h.folderCount);
    put32(m + 48, h.freeBlocks);
    put32(m + 52, h.nextAllocation);
    put32(m + 64, h.nextCatalogID);
    put32(m + 68, h.writeCount);
    put64(m + 72, h.encodingsBitmap);
    for(int i = 0; i < 8; i++)
        put32(m + 80 + 4 * i, h.finderInfo[i]);
    h.allocationFile.store(m + 112);
    h.extentsFile.store(m + 192);
    h.catalogFile.store(m + 272);
    memcpy(header_.raw, m, sizeof m);
    if(!writeAt(1024, m, sizeof m))
        return Error::io;
    /* the alternate header: 1024 bytes before the end of the volume */
    if(alternate && !writeAt(uint64_t(h.totalBlocks) * h.blockSize - 1024, m, sizeof m))
        return Error::io;
    return Error::none;
}

Error Reader::begin()
{
    if(!writable_)
        return Error::readOnly;
    cursor_ = {};
    if(dirty_)
        return Error::none;
    header_.attributes &= ~VolumeHeader::kUnmounted;
    header_.lastMountedVersion = kMacOS810;
    header_.writeCount++;
    dirty_ = true;
    return writeHeader(false);
}

Error Reader::end(Error e)
{
    header_.modifyDate = now();
    header_.attributes |= VolumeHeader::kUnmounted;
    dirty_ = false;
    Error w = writeHeader(true);
    return e != Error::none ? e : w;
}

Error Reader::flush()
{
    if(!writable_)
        return Error::none;
    Error e = writeHeader(true);
    image_->sync();
    return e;
}

/* ---- allocation blocks --------------------------------------------------- */

Error Reader::loadBitmap()
{
    if(!bitmap_.empty())
        return Error::none;
    size_t bytes = (header_.totalBlocks + 7) / 8;
    bitmap_.resize(bytes);
    if(allocationFork_->read(0, bitmap_.data(), bytes) != bytes)
    {
        bitmap_.clear();
        return Error::io;
    }
    return Error::none;
}

Error Reader::markBlocks(uint32_t start, uint32_t count, bool used)
{
    for(uint32_t b = start; b < start + count; b++)
    {
        uint8_t mask = uint8_t(0x80 >> (b % 8));
        if(used)
            bitmap_[b / 8] |= mask;
        else
            bitmap_[b / 8] &= uint8_t(~mask);
    }
    uint32_t first = start / 8, last = (start + count - 1) / 8;
    return allocationFork_->write(first, bitmap_.data() + first, last - first + 1) ? Error::none : Error::io;
}

/* want blocks: one free run where there is one (searched from hint on,
   round to the start), else the free runs from hint on until there are
   enough */
Error Reader::allocate(uint32_t want, uint32_t hint, std::vector<Extent> *got)
{
    got->clear();
    if(!want)
        return Error::none;
    Error e = loadBitmap();
    if(e != Error::none)
        return e;
    if(want > header_.freeBlocks)
        return Error::diskFull;
    const uint32_t total = header_.totalBlocks;
    auto used = [&](uint32_t b) { return bitmap_[b / 8] & (0x80 >> (b % 8)); };
    if(hint >= total)
        hint = 0;
    /* one run */
    for(uint32_t pass = 0; pass < 2 && got->empty(); pass++)
    {
        uint32_t from = pass ? 0 : hint, to = pass ? hint : total;
        for(uint32_t b = from; b < to;)
        {
            if(used(b))
            {
                b++;
                continue;
            }
            uint32_t run = b;
            while(run < total && !used(run) && run - b < want)
                run++;
            if(run - b == want)
            {
                got->push_back({ b, want });
                break;
            }
            b = run;
        }
    }
    /* else pieces */
    if(got->empty())
    {
        uint32_t need = want;
        for(uint32_t i = 0; i < total && need; i++)
        {
            uint32_t b = (hint + i) % total;
            if(used(b))
                continue;
            if(!got->empty() && got->back().start + got->back().count == b)
                got->back().count++;
            else
                got->push_back({ b, 1 });
            need--;
        }
        if(need)
        {
            got->clear();
            return Error::diskFull;
        }
    }
    for(const Extent& x : *got)
        if((e = markBlocks(x.start, x.count, true)) != Error::none)
            return e;
    header_.freeBlocks -= want;
    header_.nextAllocation = got->back().start + got->back().count;
    return Error::none;
}

Error Reader::release(const Extent& x)
{
    if(!x.count)
        return Error::none;
    Error e = loadBitmap();
    if(e == Error::none)
        e = markBlocks(x.start, x.count, false);
    if(e == Error::none)
        header_.freeBlocks += x.count;
    return e;
}

Error Reader::zero(const Extent& x)
{
    uint64_t at = uint64_t(x.start) * header_.blockSize;
    return image_->zero(offset_ + at, uint64_t(x.count) * header_.blockSize) ? Error::none : Error::io;
}

/* ---- extents -------------------------------------------------------------- */

Error Reader::storeExtents(uint32_t fileID, uint8_t forkType, const std::vector<Extent>& in, ForkData *fd)
{
    std::vector<Extent> all = in;
    merge(all);
    if(fileID == kExtentsFileID && all.size() > 8)
        return Error::diskFull;                 /* the extents file can't overflow into itself */
    /* the old overflow records out */
    for(int guard = 0; guard < 1 << 16; guard++)
    {
        BTree::Position pos;
        const uint8_t *key, *data;
        size_t room;
        BTree::Node keep;
        auto cmp = [&](const uint8_t *k) {
            uint32_t f = be32(k + 4);
            if(fileID != f)
                return fileID < f ? -1 : 1;
            if(forkType != k[2])
                return forkType < k[2] ? -1 : 1;
            return be32(k + 8) == 0 ? 0 : -1;
        };
        if(!extents_->seek(cmp, &pos) || !extents_->at(&pos, &key, &data, &room, &keep)
           || be32(key + 4) != fileID || key[2] != forkType)
            break;
        Error e = extents_->remove(std::vector<uint8_t>(key, key + 12));
        if(e != Error::none)
            return e;
    }
    for(int i = 0; i < 8; i++)
        fd->extents[i] = i < (int)all.size() ? all[i] : Extent{};
    fd->totalBlocks = blocksIn(all);
    uint32_t start = 0;
    for(size_t i = 0; i < all.size(); i++)
    {
        if(i >= 8 && (i - 8) % 8 == 0)
        {
            std::vector<uint8_t> data(64, 0);
            for(size_t j = 0; j < 8 && i + j < all.size(); j++)
            {
                put32(data.data() + 8 * j, all[i + j].start);
                put32(data.data() + 8 * j + 4, all[i + j].count);
            }
            Error e = extents_->insert(extentKey(forkType, fileID, start), data);
            if(e != Error::none)
                return e;
        }
        start += all[i].count;
    }
    return Error::none;
}

/* extents grown (new blocks allocated after the last, zeroed) or cut to
   `blocks` */
Error Reader::resizeExtents(uint32_t fileID, uint8_t forkType, std::vector<Extent>& extents, uint32_t blocks,
                            uint32_t clump)
{
    uint32_t have = blocksIn(extents);
    Error e;
    if(blocks > have)
    {
        uint32_t clumpBlocks = std::max<uint32_t>(1, clump / header_.blockSize);
        uint32_t add = (blocks - have + clumpBlocks - 1) / clumpBlocks * clumpBlocks;
        if(add > header_.freeBlocks)
            add = blocks - have;                /* no room for the whole clump: what's needed */
        uint32_t hint = extents.empty() ? header_.nextAllocation : extents.back().start + extents.back().count;
        std::vector<Extent> got;
        if((e = allocate(add, hint, &got)) != Error::none)
            return e;
        for(const Extent& x : got)
            if((e = zero(x)) != Error::none)
                return e;
        extents.insert(extents.end(), got.begin(), got.end());
        merge(extents);
        return Error::none;
    }
    while(have > blocks && !extents.empty())
    {
        Extent& last = extents.back();
        uint32_t cut = std::min(last.count, have - blocks);
        if((e = release({ last.start + last.count - cut, cut })) != Error::none)
            return e;
        last.count -= cut;
        have -= cut;
        if(!last.count)
            extents.pop_back();
    }
    return Error::none;
}

Error Reader::growTree(BTree& tree, uint32_t nodes)
{
    Fork& f = tree.file();
    ForkData& fd = tree.fileID() == kCatalogFileID ? header_.catalogFile : header_.extentsFile;
    uint64_t bytes = f.allocatedBytes() + uint64_t(nodes) * tree.nodeSize();
    uint32_t blocks = uint32_t((bytes + header_.blockSize - 1) / header_.blockSize);
    std::vector<Extent> extents = f.extents_;
    Error e = resizeExtents(tree.fileID(), 0, extents, blocks, fd.clumpSize);
    if(e != Error::none)
        return e;
    if((e = storeExtents(tree.fileID(), 0, extents, &fd)) != Error::none)
        return e;
    uint64_t newSize = uint64_t(blocksIn(extents)) * header_.blockSize;
    uint32_t added = uint32_t((newSize - f.size_) / tree.nodeSize());
    f.extents_ = extents;
    merge(f.extents_);
    f.size_ = newSize;
    fd.logicalSize = newSize;
    if((e = writeHeader(false)) != Error::none)
        return e;
    return tree.added(added);
}

/* ---- catalog records ---------------------------------------------------- */

std::vector<uint8_t> Reader::encode(const CatalogEntry& e)
{
    std::vector<uint8_t> d = e.raw;
    d.resize(e.folder ? 88 : 248, 0);
    put16(d.data(), uint16_t(e.folder ? kFolderRecord : kFileRecord));
    put16(d.data() + 2, e.flags);
    put32(d.data() + 4, e.folder ? e.valence : be32(d.data() + 4));
    put32(d.data() + 8, e.cnid);
    put32(d.data() + 12, e.createDate);
    put32(d.data() + 16, e.contentModDate);
    put32(d.data() + 20, e.attributeModDate);
    put32(d.data() + 24, e.accessDate);
    put32(d.data() + 28, e.backupDate);
    memcpy(d.data() + 48, e.userInfo, 16);
    memcpy(d.data() + 64, e.finderInfo, 16);
    put32(d.data() + 44, e.special);
    put32(d.data() + 80, e.textEncoding);
    if(!e.folder)
    {
        e.dataFork.store(d.data() + 88);
        e.resourceFork.store(d.data() + 168);
    }
    return d;
}

Error Reader::putEntry(const CatalogEntry& e)
{
    return catalog_->replace(catalogKey(e.parentID, e.name), encode(e));
}

Error Reader::adjustFolder(uint32_t folderID, int delta)
{
    auto f = byID(folderID);
    if(!f || !f->folder)
        return Error::notFound;
    f->valence = uint32_t(int64_t(f->valence) + delta);
    f->contentModDate = now();
    return putEntry(*f);
}

/* ---- the changes ---------------------------------------------------------- */

Error Reader::create(uint32_t parentID, const std::u16string& name, bool folder, const uint8_t *userInfo,
                     CatalogEntry *made)
{
    Error e = begin();
    if(e != Error::none)
        return e;
    auto parent = byID(parentID);
    if(!parent || !parent->folder)
        return end(Error::notFound);
    if(name.empty() || name.size() > 255)
        return end(Error::notFound);
    if(lookup(parentID, name))
        return end(Error::exists);
    CatalogEntry c;
    c.folder = folder;
    c.parentID = parentID;
    c.cnid = header_.nextCatalogID++;
    c.name = name;
    c.flags = folder ? 0 : CatalogEntry::kThreadExists;   /* as Mac OS 9 writes them */
    c.createDate = c.contentModDate = now();
    if(userInfo)
        memcpy(c.userInfo, userInfo, 16);
    if((e = catalog_->insert(catalogKey(parentID, name), encode(c))) != Error::none
       || (e = catalog_->insert(catalogKey(c.cnid, u""), threadRecord(folder, parentID, name))) != Error::none
       || (e = adjustFolder(parentID, +1)) != Error::none)
        return end(e);
    if(folder)
        header_.folderCount++;
    else
        header_.fileCount++;
    header_.encodingsBitmap |= 1;           /* MacRoman */
    if(made)
        *made = *byID(c.cnid);
    return end(Error::none);
}

Error Reader::remove(uint32_t cnid)
{
    Error e = begin();
    if(e != Error::none)
        return e;
    auto c = byID(cnid);
    if(!c || cnid < kFirstUserCatalogNodeID)
        return end(Error::notFound);
    if(c->folder && c->valence)
        return end(Error::notEmpty);
    if(!c->folder)
        for(bool rsrc : { false, true })
        {
            ForkData& fd = rsrc ? c->resourceFork : c->dataFork;
            auto fork = openFork(*c, rsrc);
            for(const Extent& x : fork->extents())
                if((e = release(x)) != Error::none)
                    return end(e);
            if((e = storeExtents(cnid, rsrc ? 0xFF : 0x00, {}, &fd)) != Error::none)
                return end(e);
        }
    if((e = catalog_->remove(catalogKey(c->parentID, c->name))) != Error::none
       || (e = catalog_->remove(catalogKey(cnid, u""))) != Error::none
       || (e = adjustFolder(c->parentID, -1)) != Error::none)
        return end(e);
    if(c->folder)
        header_.folderCount--;
    else
        header_.fileCount--;
    return end(Error::none);
}

Error Reader::rename(uint32_t cnid, const std::u16string& name)
{
    Error e = begin();
    if(e != Error::none)
        return e;
    auto c = byID(cnid);
    if(!c)
        return end(Error::notFound);
    if(name.empty() || name.size() > 255)
        return end(Error::notFound);
    auto other = lookup(c->parentID, name);
    if(other && other->cnid != cnid)
        return end(Error::exists);
    if((e = catalog_->remove(catalogKey(c->parentID, c->name))) != Error::none)
        return end(e);
    c->name = name;
    if((e = catalog_->insert(catalogKey(c->parentID, name), encode(*c))) != Error::none
       || (e = catalog_->replace(catalogKey(cnid, u""), threadRecord(c->folder, c->parentID, name))) != Error::none)
        return end(e);
    if(cnid != kRootFolderID && (e = adjustFolder(c->parentID, 0)) != Error::none)
        return end(e);
    return end(Error::none);
}

/* TN1150 "Hard Links": the metadata folder holds each linked file's
   content as "iNode<link reference>"; a link is a file of type 'hlnk',
   creator 'hfs+', its permissions' special field the reference. */
std::optional<uint32_t> Reader::metadataFolder()
{
    if(!privateFolder_)
    {
        auto f = lookup(kRootFolderID, kMetadataFolderName);
        privateFolder_ = f && f->folder ? f->cnid : 0;
    }
    return *privateFolder_ ? privateFolder_ : std::nullopt;
}

static std::u16string inodeName(uint32_t ref)
{
    std::string n = "iNode" + std::to_string(ref);
    return std::u16string(n.begin(), n.end());
}

Error Reader::makeLink(uint32_t targetID, uint32_t parentID, const std::u16string& name)
{
    auto t = byID(targetID);
    if(!t || t->folder)
        return Error::notFound;
    if(lookup(parentID, name))
        return Error::exists;
    Error e;
    if(!metadataFolder())
    {
        /* invisible, name locked, out of the way (22460, 22460); dated as the root */
        uint8_t info[16] = {};
        put16(info + 8, 0x5000);
        put16(info + 10, 22460);
        put16(info + 12, 22460);
        CatalogEntry made;
        if((e = create(kRootFolderID, kMetadataFolderName, true, info, &made)) != Error::none)
            return e;
        made.createDate = byID(kRootFolderID)->createDate;
        if((e = update(made)) != Error::none)
            return e;
        privateFolder_ = made.cnid;
    }
    uint32_t meta = *privateFolder_;
    uint32_t ref;
    std::optional<CatalogEntry> inode;
    std::vector<std::pair<uint32_t, std::u16string>> links = { { parentID, name } };
    if(isLink(*t))
    {
        ref = t->special;
        if(!(inode = lookup(meta, inodeName(ref))))
            return Error::notFound;
    }
    else
    {
        /* a new link reference, in 100..1073741923 */
        for(ref = 100 + (t->cnid * 2654435761u) % 1073741824u; lookup(meta, inodeName(ref));)
            ref = 100 + (ref * 2654435761u + 1) % 1073741824u;
        links.insert(links.begin(), { t->parentID, t->name });
        if((e = move(t->cnid, meta)) != Error::none || (e = rename(t->cnid, inodeName(ref))) != Error::none)
            return e;
        inode = byID(t->cnid);
        inode->special = 0;
    }
    uint32_t created = byID(meta)->createDate;
    for(const auto& [dir, n] : links)
    {
        uint8_t info[16] = {};
        memcpy(info, "hlnkhfs+", 8);
        put16(info + 8, 0x0100);                /* kHasBeenInited */
        CatalogEntry made;
        if((e = create(dir, n, false, info, &made)) != Error::none)
            return e;
        made.createDate = created;
        made.special = ref;
        inode->special++;                       /* the link count */
        if((e = begin()) != Error::none || (e = end(putEntry(made))) != Error::none)
            return e;
    }
    if((e = begin()) != Error::none)
        return e;
    return end(putEntry(*inode));
}

Error Reader::renameVolume(const std::u16string& name)
{
    return rename(kRootFolderID, name);
}

Error Reader::move(uint32_t cnid, uint32_t newParentID)
{
    Error e = begin();
    if(e != Error::none)
        return e;
    auto c = byID(cnid);
    auto np = byID(newParentID);
    if(!c || !np || !np->folder || cnid == kRootFolderID)
        return end(Error::notFound);
    if(c->parentID == newParentID)
        return end(Error::none);
    for(uint32_t up = newParentID; up >= kRootFolderID;)   /* not into itself */
    {
        if(up == cnid)
            return end(Error::badMove);
        auto u = byID(up);
        if(!u)
            break;
        up = u->parentID;
    }
    if(lookup(newParentID, c->name))
        return end(Error::exists);
    uint32_t oldParent = c->parentID;
    if((e = catalog_->remove(catalogKey(oldParent, c->name))) != Error::none)
        return end(e);
    c->parentID = newParentID;
    if((e = catalog_->insert(catalogKey(newParentID, c->name), encode(*c))) != Error::none
       || (e = catalog_->replace(catalogKey(cnid, u""), threadRecord(c->folder, newParentID, c->name))) != Error::none
       || (e = adjustFolder(oldParent, -1)) != Error::none || (e = adjustFolder(newParentID, +1)) != Error::none)
        return end(e);
    return end(Error::none);
}

Error Reader::update(const CatalogEntry& in)
{
    Error e = begin();
    if(e != Error::none)
        return e;
    auto c = byID(in.cnid);
    if(!c)
        return end(Error::notFound);
    c->flags = (c->flags & ~CatalogEntry::kLocked) | (in.flags & CatalogEntry::kLocked);
    c->createDate = in.createDate;
    c->contentModDate = in.contentModDate;
    c->attributeModDate = in.attributeModDate;
    c->backupDate = in.backupDate;
    memcpy(c->userInfo, in.userInfo, 16);
    memcpy(c->finderInfo, in.finderInfo, 16);
    return end(putEntry(*c));
}

Error Reader::setForkSize(uint32_t cnid, bool resource, uint64_t size, uint64_t reserve, bool clumped)
{
    Error e = begin();
    if(e != Error::none)
        return e;
    auto c = byID(cnid);
    if(!c || c->folder)
        return end(Error::notFound);
    ForkData& fd = resource ? c->resourceFork : c->dataFork;
    const uint32_t bs = header_.blockSize;
    uint64_t want = std::max(size, reserve);
    uint32_t blocks = uint32_t((want + bs - 1) / bs);
    std::vector<Extent> extents = openFork(*c, resource)->extents();
    uint32_t clump = !clumped ? bs : fd.clumpSize ? fd.clumpSize : resource ? header_.rsrcClumpSize : header_.dataClumpSize;
    if(blocks != blocksIn(extents))
    {
        if((e = resizeExtents(cnid, resource ? 0xFF : 0x00, extents, blocks, clump)) != Error::none)
            return end(e);
        if((e = storeExtents(cnid, resource ? 0xFF : 0x00, extents, &fd)) != Error::none)
            return end(e);
    }
    fd.logicalSize = size;
    return end(putEntry(*c));
}

Error Reader::setFinderInfo(const uint32_t info[8])
{
    Error e = begin();
    if(e != Error::none)
        return e;
    memcpy(header_.finderInfo, info, sizeof header_.finderInfo);
    return end(Error::none);
}
