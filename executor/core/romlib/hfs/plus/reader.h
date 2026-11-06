#pragma once
/* reader.h -- an HFS Plus volume, from Apple's TN1150 ("HFS Plus Volume
 * Format") and TN1189 (partitions): the volume header, the catalog and
 * extents overflow B-trees, the allocation file, forks. Read-only, or
 * read-write on a writable image (writer.cpp). Host code: the File
 * Manager's HFSPlusVolume (file/hfsplusvolume.cpp), the System sideload
 * (front-ends/phoenix/hfs_sideload.cpp) and tools/hfsplus use it.
 * docs/executor/HFSPlus.md. */
#include "image.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Executor::hfsplus
{

enum : uint32_t
{
    kRootParentID = 1,
    kRootFolderID = 2,
    kExtentsFileID = 3,
    kCatalogFileID = 4,
    kBadBlocksFileID = 5,
    kAllocationFileID = 6,
    kFirstUserCatalogNodeID = 16,
};

/* What a change to the volume can come to. */
enum class Error
{
    none,
    readOnly,       /* the image can't be written */
    diskFull,       /* no free blocks (or B-tree nodes) left */
    exists,         /* a name taken in the folder */
    notFound,
    notEmpty,       /* a folder with items in it */
    badMove,        /* a folder into itself */
    io,             /* the image refused a read or write */
};

struct Extent
{
    uint32_t start = 0, count = 0;
};

struct ForkData
{
    uint64_t logicalSize = 0;
    uint32_t clumpSize = 0, totalBlocks = 0;
    Extent extents[8];
    static ForkData parse(const uint8_t *p);
    void store(uint8_t *p) const;
};

struct VolumeHeader
{
    uint16_t signature = 0, version = 0;
    uint32_t attributes = 0, lastMountedVersion = 0, journalInfoBlock = 0;
    uint32_t createDate = 0, modifyDate = 0, backupDate = 0, checkedDate = 0;   /* createDate: local time; the rest GMT */
    uint32_t fileCount = 0, folderCount = 0;
    uint32_t blockSize = 0, totalBlocks = 0, freeBlocks = 0, nextAllocation = 0;
    uint32_t rsrcClumpSize = 0, dataClumpSize = 0, nextCatalogID = 0, writeCount = 0;
    uint64_t encodingsBitmap = 0;
    uint32_t finderInfo[8] = {};    /* [0]: the blessed System Folder */
    ForkData allocationFile, extentsFile, catalogFile, attributesFile, startupFile;
    uint8_t raw[512] = {};          /* as read: what this doesn't name is written back as it was */

    enum : uint32_t
    {
        kHardwareLock = 1u << 7,
        kUnmounted = 1u << 8,
        kInconsistent = 1u << 11,
        kJournaled = 1u << 13,
        kSoftwareLock = 1u << 15,
    };
};

/* A catalog file or folder record (TN1150 "Catalog File"); dates GMT. */
struct CatalogEntry
{
    bool folder = false;
    uint32_t parentID = 0, cnid = 0;
    std::u16string name;
    uint16_t flags = 0;             /* bit 0 locked, bit 1 a thread exists */
    uint32_t valence = 0;           /* folders */
    uint32_t createDate = 0, contentModDate = 0, attributeModDate = 0, accessDate = 0, backupDate = 0;
    uint8_t userInfo[16] = {};      /* FInfo / DInfo */
    uint8_t finderInfo[16] = {};    /* FXInfo / DXInfo */
    uint32_t textEncoding = 0;
    uint32_t special = 0;           /* permissions.special: a hard link's iNode number */
    ForkData dataFork, resourceFork;
    std::vector<uint8_t> raw;       /* the record as read: written back with the fields above put in */

    enum : uint16_t
    {
        kLocked = 1 << 0,
        kThreadExists = 1 << 1,
    };
};

class Reader;
class BTree;

/* An open fork: reads (and, on a writable volume, writes within what is
   allocated to it) by byte offset. */
class Fork
{
public:
    uint64_t size() const { return size_; }
    /* Up to n bytes at offset (fewer at the end); how many. */
    size_t read(uint64_t offset, void *buf, size_t n);
    /* n bytes at offset, inside the fork's allocation blocks. */
    bool write(uint64_t offset, const void *buf, size_t n);
    /* Its extents, overflow ones included. */
    const std::vector<Extent>& extents() const { return extents_; }
    uint64_t allocatedBytes() const;

private:
    friend class Reader;
    Reader *reader_ = nullptr;
    uint64_t size_ = 0;
    std::vector<Extent> extents_;
    bool complete_ = true;          /* the extents cover the fork */
};

class Reader
{
public:
    /* The HFS+ volume whose header is at volumeStart + 1024 (bare); or the
       one embedded in the HFS wrapper there. nullptr, with why in *error,
       if there's none. Writable when the image is, the volume isn't
       journaled or locked, and its last unmount was clean. */
    static std::unique_ptr<Reader> open(std::shared_ptr<ImageReader> image, uint64_t volumeStart,
                                        std::string *error = nullptr);
    ~Reader();

    const VolumeHeader& header() const { return header_; }
    bool isHFSX() const { return header_.signature == 0x4858; }
    bool wrapped() const { return wrapped_; }
    /* In an HFS wrapper: its allocation block size, and the embedded
       volume's length in those blocks (drEmbedExtent) */
    uint32_t wrapperBlockSize() const { return wrapperBlockSize_; }
    uint16_t wrapperEmbedBlocks() const { return wrapperEmbedBlocks_; }
    bool writable() const { return writable_; }
    uint64_t volumeOffset() const { return offset_; }
    std::u16string volumeName();

    std::optional<CatalogEntry> lookup(uint32_t parentID, const std::u16string& name);
    std::optional<CatalogEntry> byID(uint32_t cnid);
    /* A folder's items in catalog order; f returns false to stop. */
    void forEachChild(uint32_t parentID, const std::function<bool(const CatalogEntry&)>& f);
    std::vector<CatalogEntry> children(uint32_t parentID);
    /* Every file and folder record in the catalog file's order on disk
       (leaf nodes by number), as a catalog search goes through them. */
    void forEachLeafRecord(const std::function<void(const CatalogEntry&)>& f);
    /* The index-th item (1-based) in a folder, as PBGetCatInfo's ioFDirIndex;
       sequential indexes cost one step each. */
    std::optional<CatalogEntry> childAt(uint32_t parentID, uint32_t index);

    /* A hard link's file (TN1150 "Hard Links"); the entry itself otherwise. */
    CatalogEntry resolveLink(const CatalogEntry& e);
    static bool isLink(const CatalogEntry& e);
    /* The folder of hard links' files (indirect node files), if there's one */
    std::optional<uint32_t> metadataFolder();

    std::unique_ptr<Fork> openFork(const CatalogEntry& file, bool resource);

    int compareNames(const std::u16string& a, const std::u16string& b) const;

    /* ---- changing the volume (writer.cpp); dates GMT, from now() ---- */

    /* A new file or folder; its entry in *made. */
    Error create(uint32_t parentID, const std::u16string& name, bool folder, const uint8_t *userInfo,
                 CatalogEntry *made);
    /* A file (its forks freed) or an empty folder. */
    Error remove(uint32_t cnid);
    Error rename(uint32_t cnid, const std::u16string& name);
    Error move(uint32_t cnid, uint32_t newParentID);
    /* Two files' contents swapped (ExchangeFiles): both forks and the
       modification date; IDs, names, Finder info and creation dates stay. */
    Error exchange(uint32_t a, uint32_t b);
    /* An item's flags, dates, Finder info and text encoding, as in e. */
    Error update(const CatalogEntry& e);
    /* A fork's length, its allocation grown (new blocks zeroed) or shrunk
       to cover it, at least `reserve` bytes allocated: in whole blocks, or
       (clump) grown a clump at a time. */
    Error setForkSize(uint32_t cnid, bool resource, uint64_t size, uint64_t reserve = 0, bool clump = false);
    /* A hard link to a file, name in parentID: a file not yet linked
       becomes the indirect node file, a link left where it was (Mac OS
       makes none; for tests) */
    Error makeLink(uint32_t targetID, uint32_t parentID, const std::u16string& name);
    /* The volume's name (the root folder's) */
    Error renameVolume(const std::u16string& name);
    /* The header's Finder info (the blessed folder...) */
    Error setFinderInfo(const uint32_t info[8]);
    /* The header and its copy at the volume's end written, marked cleanly
       unmounted; the image synced. */
    Error flush();
    static uint32_t now();

private:
    friend class Fork;
    friend class BTree;

    Reader() = default;
    bool readAt(uint64_t volumeByte, void *buf, size_t n) { return image_->read(offset_ + volumeByte, buf, n); }
    bool writeAt(uint64_t volumeByte, const void *buf, size_t n) { return image_->write(offset_ + volumeByte, buf, n); }
    std::vector<Extent> overflowExtents(uint32_t fileID, uint8_t forkType, uint32_t startBlock);
    void fillExtents(Fork& fork, uint32_t fileID, uint8_t forkType, const ForkData& fd);
    static std::optional<CatalogEntry> parseRecord(const uint8_t *key, const uint8_t *data, size_t room);

    /* writer.cpp */
    Error begin();                  /* before a change: the volume marked in use */
    Error end(Error e);             /* after: the header written */
    Error writeHeader(bool alternate);
    Error loadBitmap();
    Error allocate(uint32_t want, uint32_t hint, std::vector<Extent> *got);
    Error release(const Extent& e);
    Error markBlocks(uint32_t start, uint32_t count, bool used);
    Error zero(const Extent& e);
    /* A fork's extent list stored: the first 8 in fd, the rest as extents
       overflow records (the old ones replaced) */
    Error storeExtents(uint32_t fileID, uint8_t forkType, const std::vector<Extent>& all, ForkData *fd);
    Error resizeExtents(uint32_t fileID, uint8_t forkType, std::vector<Extent>& extents, uint32_t blocks,
                        uint32_t clump);
    Error growTree(BTree& tree, uint32_t nodes);
    Error putEntry(const CatalogEntry& e);        /* the record, under its key */
    Error adjustFolder(uint32_t folderID, int delta);
    std::vector<uint8_t> encode(const CatalogEntry& e);

    std::shared_ptr<ImageReader> image_;
    uint64_t offset_ = 0;
    bool wrapped_ = false, writable_ = false, dirty_ = false;
    uint32_t wrapperBlockSize_ = 0;
    uint16_t wrapperEmbedBlocks_ = 0;
    VolumeHeader header_;
    std::unique_ptr<Fork> extentsFork_, catalogFork_, allocationFork_;
    std::unique_ptr<BTree> extents_, catalog_;
    bool binaryOrder_ = false;
    std::vector<uint8_t> bitmap_;               /* the allocation file, once a change needs it */
    /* childAt's place: the last folder walked, its index and the key there */
    struct Cursor
    {
        uint32_t parentID = 0, index = 0;
        uint32_t node = 0;
        uint16_t record = 0;
    } cursor_;
    std::optional<uint32_t> privateFolder_;   /* the hard links' folder */
};

/* "\0\0\0\0HFS+ Private Data", the metadata folder's name */
extern const std::u16string kMetadataFolderName;

/* Catalog and extents keys in their on-disk form (keyLength first). */
std::vector<uint8_t> catalogKey(uint32_t parentID, const std::u16string& name);
std::vector<uint8_t> extentKey(uint8_t forkType, uint32_t fileID, uint32_t startBlock);

/* Where HFS+ volumes start in an image: every Apple_HFS partition of an
   Apple partition map, else the image itself (offset 0). Whether each is
   HFS+ is Reader::open's to find. */
std::vector<uint64_t> volumeStarts(ImageReader& image);

/* Whether there's an HFS+ volume (bare 'H+'/'HX', or 'BD' with an embedded
   'H+') whose start is volumeStart. */
bool isHFSPlus(ImageReader& image, uint64_t volumeStart);

} // namespace Executor::hfsplus
