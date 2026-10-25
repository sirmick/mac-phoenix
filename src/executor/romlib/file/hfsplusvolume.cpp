/* hfsplusvolume.cpp -- see hfsplusvolume.h. The File Manager's classic
 * calls on an HFS+ volume as Mac OS 9.0.4 answers them
 * (docs/executor/HFSPlus.md): CNIDs are the dirIDs and file numbers;
 * names longer than 31 characters, or with characters MacRoman lacks, are
 * the short "#<CNID>" form; HFS+ dates (GMT) are reported in local time.
 * On a writable image the changes go to hfs/plus/writer.cpp; else the
 * volume is locked and writes fail with wPrErr. */
#include <file/hfsplusvolume.h>

#include <base/common.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <file/file.h>
#include <file/localvolume/item.h>
#include <hfs/hfs.h>
#include <hfs/plus/unicode.h>
#include <util/macstrings.h>

#include <algorithm>
#include <cstring>

using namespace Executor;
using namespace Executor::hfsplus;

namespace
{
/* The classic File Manager's lengths are LONGINTs: forks over 2 GB are
   cut there. */
const uint64_t kMaxEOF = 0x7FFFFFFF;

std::string pstr(ConstStringPtr p)
{
    return p ? std::string((const char *)p + 1, p[0]) : std::string();
}

bool equalNames(const std::string& a, const std::string& b)
{
    return fastUnicodeCompare(macRomanToUnicode(a), macRomanToUnicode(b)) == 0;
}

void putName(StringPtr out, const std::string& name)
{
    size_t n = std::min<size_t>(name.size(), 255);
    memcpy(out + 1, name.data(), n);
    out[0] = (uint8_t)n;
}

const uint16_t kFilesOpen = 1 << 6;          /* vcbAtrb */

int fcbIndex(short refNum)
{
    return (refNum - 2) / 94;
}
} // namespace

/* A fork as all its open paths see it */
struct HFSPlusVolume::ForkState
{
    uint32_t cnid;
    bool resource;
    std::unique_ptr<Fork> fork;
    uint64_t size = 0, allocated = 0;
};

struct HFSPlusVolume::OpenFork
{
    filecontrolblock *fcb;
    short refNum;
    std::shared_ptr<ForkState> state;
    bool forWriting;            /* opened with a write permission */
    bool canWrite;              /* ... and may (a writable volume, not locked) */
    bool shared;                /* fsRdWrShPerm */
    bool written = false;       /* the file's date is set at close or flush */
    uint64_t eof() const { return std::min(state->size, kMaxEOF); }
};

HFSPlusVolume::HFSPlusVolume(VCB& vcb, std::unique_ptr<Reader> reader)
    : Volume(vcb), reader_(std::move(reader))
{
    updateVCB();
}

HFSPlusVolume::~HFSPlusVolume() = default;

uint32_t HFSPlusVolume::localDate(uint32_t gmt) const
{
    return gmt ? (uint32_t)hostToMacTime(int64_t(gmt) - 2082844800) : 0;
}

/* The VCB's classic fields as Mac OS 9.0.4 reports them for an HFS+ volume
   (tests/guest/hfsplus/oracle-9.0.4/voldump-*.txt): signature 'BD'; the
   real allocation block size; free blocks as they are; ioVNmFls the root
   folder's valence (files and folders); and a total that only fits 16
   bits: the volume's 512-byte sectors divided by the smallest k that
   brings them under 65536 (32 MB of 4 KB blocks: 32768; 64 MB: 43690),
   or, in an HFS wrapper, the embedded volume's block count. */
void HFSPlusVolume::updateVCB()
{
    const VolumeHeader& h = reader_->header();
    vcb.vcbSigWord = 0x4244;
    vcb.vcbCrDate = h.createDate;               /* local time already */
    vcb.vcbLsMod = localDate(h.modifyDate);
    vcb.vcbVolBkUp = localDate(h.backupDate);
    vcb.vcbAlBlkSiz = h.blockSize;
    if(reader_->wrapped())
        vcb.vcbNmAlBlks = (uint16_t)std::min<uint32_t>(h.totalBlocks, 0xFFFF);
    else
    {
        uint64_t sectors = uint64_t(h.totalBlocks) * h.blockSize / 512;
        uint64_t k = (sectors + 0xFFFE) / 0xFFFF;
        vcb.vcbNmAlBlks = (uint16_t)(k ? sectors / k : 0);
    }
    vcb.vcbFreeBks = (uint16_t)std::min<uint32_t>(h.freeBlocks, 0xFFFF);
    vcb.vcbClpSiz = h.dataClumpSize;
    vcb.vcbNxtCNID = h.nextCatalogID;
    vcb.vcbFilCnt = h.fileCount;
    vcb.vcbDirCnt = h.folderCount;
    vcb.vcbWrCnt = h.writeCount;
    for(int i = 0; i < 8; i++)
        vcb.vcbFndrInfo[i] = h.finderInfo[i];
    vcb.vcbNmFls = (uint16_t)std::min<uint32_t>(root().valence, 0xFFFF);
    std::string name = unicodeToMacRoman(reader_->volumeName()).substr(0, 27);
    putName((StringPtr)vcb.vcbVN, name);
}

std::string HFSPlusVolume::classicName(const Entry& e)
{
    if(e.cnid == kRootFolderID)
        return pstr((ConstStringPtr)vcb.vcbVN);
    return hfsplus::classicName(e.name, e.cnid);
}

/* ---- names to catalog entries ------------------------------------------ */

CatalogEntry HFSPlusVolume::root()
{
    auto r = reader_->byID(kRootFolderID);
    if(!r)
        throw OSErrorException(ioErr);
    return *r;
}

CatalogEntry HFSPlusVolume::resolveDir(short vRef, long dirID)
{
    if(!dirID)
    {
        if(ISWDNUM(vRef))
            dirID = WDNUMTOWDP(vRef)->dirid;
        else if(vRef == 0)
            dirID = DefDirID;
        else
            dirID = kRootFolderID;
    }
    auto e = reader_->byID((uint32_t)dirID);
    if(!e || !e->folder)
        throw OSErrorException(dirNFErr);
    return *e;
}

/* An item in a folder by its classic name: its MacRoman name, or the short
   "#<CNID>" form 9.0.4 gives a long or unconvertible one. Hard links are
   not followed: 9.0.4 shows a link as its empty 'hlnk' file and the
   metadata folder as a folder (oracle-9.0.4/voldump-hardlinks.txt). */
std::optional<CatalogEntry> HFSPlusVolume::child(uint32_t dirID, const std::string& macName)
{
    if(macName.empty() || macName.size() > 255)
        return std::nullopt;
    if(auto e = reader_->lookup(dirID, macRomanToUnicode(macName)))
        return *e;
    if(auto cnid = cnidInClassicName(macName))
        if(auto e = reader_->byID(*cnid))
            if(e->parentID == dirID && classicName(*e) == macName)    /* exactly, as 9.0.4 */
                return *e;
    return std::nullopt;
}

CatalogEntry HFSPlusVolume::resolve(StringPtr namePtr, short vRef, long dirID)
{
    std::string name = pstr(namePtr);
    if(name.empty())
        return resolveDir(vRef, dirID);
    size_t colon = name.find(':');

    if(dirID == 1 && (colon == 0 || colon == std::string::npos))
    {
        /* directory 1, the volume's parent: "Vol" or ":Vol:..." */
        size_t from = colon == 0 ? 1 : 0;
        size_t rest = colon == 0 ? name.find(':', 1) : std::string::npos;
        if(!equalNames(name.substr(from, rest - from), pstr((ConstStringPtr)vcb.vcbVN)))
            throw OSErrorException(rest != std::string::npos ? dirNFErr : fnfErr);
        CatalogEntry r = root();
        return rest == std::string::npos ? r : resolveRelative(r, name.substr(rest));
    }
    if(colon != std::string::npos)
    {
        /* "Vol:..." (ROMlib_findvcb picked this volume by the name) or ":..." */
        CatalogEntry base = colon == 0 ? resolveDir(vRef, dirID) : root();
        return resolveRelative(base, name.substr(colon));
    }
    CatalogEntry dir = resolveDir(vRef, dirID);
    if(auto e = child(dir.cnid, name))
        return *e;
    throw OSErrorException(fnfErr);
}

/* rest starts with ':'; "::" is the parent. */
CatalogEntry HFSPlusVolume::resolveRelative(Entry base, const std::string& rest)
{
    size_t p = 0;
    while(p < rest.size())
    {
        if(rest[p] != ':')
            throw OSErrorException(bdNamErr);
        ++p;
        if(p == rest.size())
            return base;
        size_t colon = rest.find(':', p);
        bool last = colon == std::string::npos;
        if(colon == p)
        {
            if(base.cnid == kRootFolderID)
                throw OSErrorException(dirNFErr);
            base = resolveDir(0, base.parentID);
            continue;
        }
        if(!base.folder)
            throw OSErrorException(dirNFErr);
        auto e = child(base.cnid, rest.substr(p, last ? std::string::npos : colon - p));
        if(!e)
            throw OSErrorException(last ? fnfErr : dirNFErr);
        base = *e;
        if(last)
            return base;
        p = colon;
    }
    return base;
}

CatalogEntry HFSPlusVolume::resolveForInfo(StringPtr name, short vRef, long dirID, short index, bool includeDirectories)
{
    if(index > 0)
    {
        CatalogEntry dir = resolveDir(vRef, dirID);
        if(includeDirectories)
        {
            if(auto e = reader_->childAt(dir.cnid, (uint32_t)index))
                return *e;
            throw OSErrorException(fnfErr);
        }
        std::optional<CatalogEntry> found;       /* files only */
        int n = 0;
        reader_->forEachChild(dir.cnid, [&](const CatalogEntry& e) {
            if(!e.folder && ++n == index)
                found = e;
            return !found;
        });
        if(!found)
            throw OSErrorException(fnfErr);
        return *found;
    }
    if(index == 0 && name && name[0])
        return resolve(name, vRef, dirID);
    return resolveDir(vRef, dirID);
}

/* ---- information --------------------------------------------------------- */

/* vcbAtrb bit 6: files are open on the volume */
void HFSPlusVolume::PBGetVInfo(ParmBlkPtr pb)
{
    if(writable())
        updateVCB();                            /* free blocks, counts, name */
    bool open = std::any_of(forks_.begin(), forks_.end(), [](const auto& f) { return bool(f); });
    vcb.vcbAtrb = (vcb.vcbAtrb & ~kFilesOpen) | (open ? kFilesOpen : 0);
    Volume::PBGetVInfo(pb);
}

void HFSPlusVolume::getInfoCommon(CInfoPBPtr pb, InfoKind kind)
{
    short index = kind == InfoKind::CatInfo ? (short)pb->hFileInfo.ioFDirIndex : std::max<short>((short)pb->hFileInfo.ioFDirIndex, 0);
    CatalogEntry e = resolveForInfo(pb->hFileInfo.ioNamePtr, pb->hFileInfo.ioVRefNum,
                                    kind == InfoKind::FInfo ? 0 : pb->hFileInfo.ioDirID.get(), index,
                                    kind == InfoKind::CatInfo);
    if(e.folder && kind != InfoKind::CatInfo)
        throw OSErrorException(fnfErr);

    if(index != 0 && pb->hFileInfo.ioNamePtr)
        putName(pb->hFileInfo.ioNamePtr, classicName(e));

    pb->hFileInfo.ioDirID = e.cnid;               /* ioDrDirID */
    if(kind == InfoKind::CatInfo)
        pb->hFileInfo.ioFlParID = e.parentID;     /* ioDrParID */

    if(e.folder)
    {
        pb->dirInfo.ioFlAttrib = ATTRIB_ISADIR;
        pb->dirInfo.ioACUser = 0;
        memcpy(&pb->dirInfo.ioDrUsrWds, e.userInfo, 16);
        memcpy(&pb->dirInfo.ioDrFndrInfo, e.finderInfo, 16);
        pb->dirInfo.ioDrNmFls = (uint16_t)std::min<uint32_t>(e.valence, 0xFFFF);
        pb->dirInfo.ioDrCrDat = localDate(e.createDate);
        pb->dirInfo.ioDrMdDat = localDate(e.contentModDate);
        pb->dirInfo.ioDrBkDat = localDate(e.backupDate);
        return;
    }

    /* the catalog record's flags (bit 0 locked, bit 1 "thread exists": 9.0.4
       shows both), and the open bits */
    pb->hFileInfo.ioFlAttrib = (Byte)(e.flags & 0xFF) | open_attrib_bits(e.cnid, &vcb, &pb->hFileInfo.ioFRefNum);
    pb->hFileInfo.ioFVersNum = 0;
    memcpy(&pb->hFileInfo.ioFlFndrInfo, e.userInfo, 16);
    const uint64_t bs = reader_->header().blockSize;
    auto lengths = [&](const ForkData& f, GUEST<INTEGER>& stBlk, GUEST<LONGINT>& lg, GUEST<LONGINT>& py) {
        stBlk = (INTEGER)(f.extents[0].count ? f.extents[0].start / blockScale_ : 0);
        lg = (LONGINT)std::min(f.logicalSize, kMaxEOF);
        py = (LONGINT)std::min(uint64_t(f.totalBlocks) * bs, kMaxEOF);
    };
    lengths(e.dataFork, pb->hFileInfo.ioFlStBlk, pb->hFileInfo.ioFlLgLen, pb->hFileInfo.ioFlPyLen);
    lengths(e.resourceFork, pb->hFileInfo.ioFlRStBlk, pb->hFileInfo.ioFlRLgLen, pb->hFileInfo.ioFlRPyLen);
    pb->hFileInfo.ioFlCrDat = localDate(e.createDate);
    pb->hFileInfo.ioFlMdDat = localDate(e.contentModDate);
    if(kind == InfoKind::CatInfo)
    {
        pb->hFileInfo.ioFlBkDat = localDate(e.backupDate);
        memcpy(&pb->hFileInfo.ioFlXFndrInfo, e.finderInfo, 16);
        pb->hFileInfo.ioFlClpSiz = 0;
    }
}

void HFSPlusVolume::PBGetCatInfo(CInfoPBPtr pb)
{
    getInfoCommon(pb, InfoKind::CatInfo);
}
void HFSPlusVolume::PBHGetFInfo(HParmBlkPtr pb)
{
    getInfoCommon(reinterpret_cast<CInfoPBPtr>(pb), InfoKind::HFInfo);
}
void HFSPlusVolume::PBGetFInfo(ParmBlkPtr pb)
{
    getInfoCommon(reinterpret_cast<CInfoPBPtr>(pb), InfoKind::FInfo);
}

/* ---- writing: helpers ---------------------------------------------------- */

uint32_t HFSPlusVolume::gmtDate(uint32_t local) const
{
    return local ? (uint32_t)(macToHostTime(local) + 2082844800) : 0;
}

void HFSPlusVolume::check(hfsplus::Error e, OSErr notFound)
{
    switch(e)
    {
        case hfsplus::Error::none:
            return;
        case hfsplus::Error::readOnly:
            throw OSErrorException(wPrErr);
        case hfsplus::Error::diskFull:
            throw OSErrorException(dskFulErr);
        case hfsplus::Error::exists:
            throw OSErrorException(dupFNErr);
        case hfsplus::Error::notFound:
            throw OSErrorException(notFound);
        case hfsplus::Error::notEmpty:
            throw OSErrorException(fBsyErr);
        case hfsplus::Error::badMove:
            throw OSErrorException(badMovErr);
        case hfsplus::Error::io:
            throw OSErrorException(ioErr);
    }
}

/* a new classic name: 1 to max MacRoman characters, no ':' */
std::u16string HFSPlusVolume::newName(ConstStringPtr p, size_t max)
{
    std::string name = pstr(p);
    if(name.empty() || name.size() > max || name.find(':') != std::string::npos)
        throw OSErrorException(bdNamErr);
    return macRomanToUnicode(name);
}

/* the folder a new item goes in, and its name ("Vol:Folder:Name" or "Name") */
std::pair<CatalogEntry, std::string> HFSPlusVolume::resolveForCreate(StringPtr namePtr, short vRef, long dirID)
{
    std::string name = pstr(namePtr);
    size_t colon = name.rfind(':');
    if(colon == std::string::npos)
        return { resolveDir(vRef, dirID), name };
    Str255 dir;
    putName(dir, name.substr(0, colon + 1));
    CatalogEntry parent = resolve(dir, vRef, dirID);
    if(!parent.folder)
        throw OSErrorException(dirNFErr);
    return { parent, name.substr(colon + 1) };
}

bool HFSPlusVolume::isOpen(uint32_t cnid)
{
    for(auto& f : forks_)
        if(f && f->state->cnid == cnid)
            return true;
    return false;
}

/* a fork's state again from the catalog, and its paths' FCBs */
void HFSPlusVolume::refresh(ForkState& st)
{
    auto e = reader_->byID(st.cnid);
    if(!e)
        return;
    st.fork = reader_->openFork(*e, st.resource);
    st.size = st.fork->size();
    st.allocated = st.fork->allocatedBytes();
    for(auto& f : forks_)
        if(f && f->state.get() == &st)
        {
            f->fcb->fcbEOF = (LONGINT)f->eof();
            f->fcb->fcbPLen = (LONGINT)std::min(st.allocated, kMaxEOF);
            f->fcb->fcbDirID = e->parentID;
            std::string name = classicName(*e).substr(0, 31);
            f->fcb->fcbCName[0] = (uint8_t)name.size();
            memcpy(&f->fcb->fcbCName[1], name.data(), name.size());
        }
}

void HFSPlusVolume::refreshFile(uint32_t cnid)
{
    for(bool rsrc : { false, true })
    {
        auto it = states_.find(uint64_t(cnid) << 1 | rsrc);
        if(it != states_.end())
            if(auto st = it->second.lock())
                refresh(*st);
    }
}

/* ---- open files ---------------------------------------------------------- */

void HFSPlusVolume::openCommon(GUEST<short>& refNum, const Entry& item, bool resource, int8_t permission)
{
    refNum = 0;
    if(item.folder)
        throw OSErrorException(fnfErr);
    bool locked = item.flags & CatalogEntry::kLocked;
    bool wantsWrite = permission == fsWrPerm || permission == fsRdWrPerm || permission == fsRdWrShPerm;
    bool shared = permission == fsRdWrShPerm;
    bool canWrite = false;
    if(writable())
    {
        /* as HFS: a locked file opens for reading only; one exclusive
           writer per fork (fsCurPerm takes writing when it can) */
        short writer = 0;
        bool sharedWriters = false;
        for(auto& f : forks_)
            if(f && f->state->cnid == item.cnid && f->state->resource == resource && f->canWrite)
            {
                writer = f->refNum;
                sharedWriters = f->shared;
            }
        if(wantsWrite && locked)
            throw OSErrorException(permErr);
        if(wantsWrite && writer && !(shared && sharedWriters))
        {
            refNum = writer;
            throw OSErrorException(opWrErr);
        }
        canWrite = wantsWrite || (permission == fsCurPerm && !locked && !writer);
    }

    filecontrolblock *fcb = ROMlib_getfreefcbp();
    if(!fcb)
        throw OSErrorException(tmfoErr);
    short ref = (char *)fcb - (char *)LM(FCBSPtr);
    memset(fcb, 0, sizeof(*fcb));
    auto f = std::make_unique<OpenFork>();
    f->fcb = fcb;
    f->refNum = ref;
    uint64_t key = uint64_t(item.cnid) << 1 | resource;
    f->state = states_[key].lock();
    if(!f->state)
    {
        f->state = std::make_shared<ForkState>();
        f->state->cnid = item.cnid;
        f->state->resource = resource;
        states_[key] = f->state;
    }
    f->forWriting = wantsWrite;
    f->canWrite = canWrite;
    f->shared = shared;

    fcb->fcbFlNum = item.cnid;
    fcb->fcbMdRByt = (resource ? RESOURCEBIT : 0) | (locked ? FLOCKEDBIT : 0) | (canWrite ? WRITEBIT : 0)
                     | (canWrite && shared ? SHAREDBIT : 0);
    const ForkData& fd = resource ? item.resourceFork : item.dataFork;
    fcb->fcbClmpSize = fd.clumpSize;
    fcb->fcbVPtr = &vcb;

    size_t i = fcbIndex(ref);
    if(forks_.size() <= i)
        forks_.resize(i + 1);
    forks_[i] = std::move(f);
    refresh(*forks_[i]->state);
    refNum = ref;
}

HFSPlusVolume::OpenFork& HFSPlusVolume::openFork(short refNum)
{
    size_t i = fcbIndex(refNum);
    if(refNum < 0 || refNum % 94 != 2 || i >= forks_.size() || !forks_[i])
        throw OSErrorException(rfNumErr);
    return *forks_[i];
}

void HFSPlusVolume::PBHOpenDF(HParmBlkPtr pb)
{
    openCommon(pb->ioParam.ioRefNum, resolve(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID),
               false, pb->ioParam.ioPermssn);
}
void HFSPlusVolume::PBHOpenRF(HParmBlkPtr pb)
{
    openCommon(pb->ioParam.ioRefNum, resolve(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID),
               true, pb->ioParam.ioPermssn);
}
void HFSPlusVolume::PBOpenDF(ParmBlkPtr pb)
{
    openCommon(pb->ioParam.ioRefNum, resolve(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, 0), false,
               pb->ioParam.ioPermssn);
}
void HFSPlusVolume::PBOpenRF(ParmBlkPtr pb)
{
    openCommon(pb->ioParam.ioRefNum, resolve(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, 0), true,
               pb->ioParam.ioPermssn);
}

void HFSPlusVolume::PBOpenWD(WDPBPtr pb)
{
    CatalogEntry e = resolve(pb->ioNamePtr, pb->ioVRefNum, pb->ioWDDirID);
    OSErr err = ROMlib_mkwd(pb, &vcb, e.folder ? e.cnid : e.parentID, pb->ioWDProcID);
    if(err)
        throw OSErrorException(err);
}

void HFSPlusVolume::setFPosCommon(ParmBlkPtr pb, bool checkEOF)
{
    OpenFork& f = openFork(pb->ioParam.ioRefNum);
    int64_t eof = (int64_t)f.eof();
    int64_t pos = f.fcb->fcbCrPs;
    switch(pb->ioParam.ioPosMode & 0x3)       /* the rest: noCacheBit, newline mode */
    {
        case fsAtMark:
            break;
        case fsFromStart:
            pos = pb->ioParam.ioPosOffset;
            break;
        case fsFromLEOF:
            pos = eof + pb->ioParam.ioPosOffset;
            break;
        case fsFromMark:
            pos += pb->ioParam.ioPosOffset;
            break;
    }
    OSErr err = noErr;
    if(pos > eof && checkEOF)
    {
        pos = eof;
        err = eofErr;
    }
    if(pos < 0)
    {
        pos = f.fcb->fcbCrPs;
        err = posErr;
    }
    pb->ioParam.ioPosOffset = f.fcb->fcbCrPs = (LONGINT)pos;
    if(err)
        throw OSErrorException(err);
}

void HFSPlusVolume::PBSetFPos(ParmBlkPtr pb)
{
    setFPosCommon(pb, true);
}

void HFSPlusVolume::PBGetFPos(ParmBlkPtr pb)
{
    OpenFork& f = openFork(pb->ioParam.ioRefNum);
    pb->ioParam.ioReqCount = 0;
    pb->ioParam.ioActCount = 0;
    pb->ioParam.ioPosMode = 0;
    pb->ioParam.ioPosOffset = f.fcb->fcbCrPs;
}

void HFSPlusVolume::PBRead(ParmBlkPtr pb)
{
    pb->ioParam.ioActCount = 0;
    setFPosCommon(pb, true);
    OpenFork& f = openFork(pb->ioParam.ioRefNum);
    uint64_t pos = (uint32_t)f.fcb->fcbCrPs;
    size_t want = (size_t)std::max<LONGINT>(pb->ioParam.ioReqCount, 0);
    size_t room = (size_t)std::min<uint64_t>(want, f.eof() - pos);
    size_t got = room ? f.state->fork->read(pos, pb->ioParam.ioBuffer, room) : 0;
    pb->ioParam.ioActCount = (LONGINT)got;
    pb->ioParam.ioPosOffset = f.fcb->fcbCrPs = (LONGINT)(pos + got);
    if(got < room)
        throw OSErrorException(ioErr);
    if(got < want)
        throw OSErrorException(eofErr);
}

void HFSPlusVolume::PBGetEOF(ParmBlkPtr pb)
{
    pb->ioParam.ioMisc = (LONGINT)openFork(pb->ioParam.ioRefNum).eof();
}

/* Writing through a path that can't: wPrErr where it was opened for
   writing on a locked volume, wrPermErr where read-only (9.0.4). */
void HFSPlusVolume::refuseWrite(OpenFork& f)
{
    throw OSErrorException(f.forWriting && !writable() ? wPrErr : wrPermErr);
}

void HFSPlusVolume::PBWrite(ParmBlkPtr pb)
{
    pb->ioParam.ioActCount = 0;
    OpenFork& f = openFork(pb->ioParam.ioRefNum);
    if(!f.canWrite)
        refuseWrite(f);
    setFPosCommon(pb, false);                   /* past the end: the fork grows */
    uint64_t pos = (uint32_t)f.fcb->fcbCrPs;
    LONGINT n = std::max<LONGINT>(pb->ioParam.ioReqCount, 0);
    if(pos + uint64_t(n) > kMaxEOF)
        throw OSErrorException(dskFulErr);
    if(pos + n > f.state->size)
    {
        /* whole blocks as needed (9.0.4: no clumps), never fewer than it has */
        check(reader_->setForkSize(f.state->cnid, f.state->resource, pos + n, f.state->allocated));
        refreshFile(f.state->cnid);
    }
    if(n && !f.state->fork->write(pos, pb->ioParam.ioBuffer, (size_t)n))
        throw OSErrorException(ioErr);
    f.written = true;
    pb->ioParam.ioActCount = n;
    pb->ioParam.ioPosOffset = f.fcb->fcbCrPs = (LONGINT)(pos + n);
}

void HFSPlusVolume::PBSetEOF(ParmBlkPtr pb)
{
    OpenFork& f = openFork(pb->ioParam.ioRefNum);
    if(!f.canWrite)
        refuseWrite(f);
    LONGINT eof = pb->ioParam.ioMisc;
    if(eof < 0)
        throw OSErrorException(paramErr);
    check(reader_->setForkSize(f.state->cnid, f.state->resource, (uint64_t)eof));
    refreshFile(f.state->cnid);
    for(auto& g : forks_)
        if(g && g->state == f.state && (uint32_t)g->fcb->fcbCrPs > (uint32_t)eof)
            g->fcb->fcbCrPs = eof;
    f.written = true;
}

/* Allocate, AllocContig: ioReqCount bytes more allocated to the fork (its
   length unchanged); ioActCount what was. */
void HFSPlusVolume::PBAllocate(ParmBlkPtr pb)
{
    OpenFork& f = openFork(pb->ioParam.ioRefNum);
    pb->ioParam.ioActCount = 0;
    if(!f.canWrite)
        refuseWrite(f);
    uint64_t before = f.state->allocated;
    LONGINT want = std::max<LONGINT>(pb->ioParam.ioReqCount, 0);
    check(reader_->setForkSize(f.state->cnid, f.state->resource, f.state->size, before + want));
    refreshFile(f.state->cnid);
    pb->ioParam.ioActCount = (LONGINT)std::min<uint64_t>(f.state->allocated - before, kMaxEOF);
}
void HFSPlusVolume::PBAllocContig(ParmBlkPtr pb)
{
    PBAllocate(pb);
}

/* As HFS: a path that wrote sets its file's modification date when
   flushed or closed. */
void HFSPlusVolume::stampWritten(OpenFork& f)
{
    if(!f.written || !writable())
        return;
    f.written = false;
    if(auto e = reader_->byID(f.state->cnid))
    {
        e->contentModDate = Reader::now();
        reader_->update(*e);
    }
}

void HFSPlusVolume::PBClose(ParmBlkPtr pb)
{
    OpenFork& f = openFork(pb->ioParam.ioRefNum);
    stampWritten(f);
    f.fcb->fcbFlNum = 0;
    forks_[fcbIndex(f.refNum)].reset();
}

void HFSPlusVolume::PBFlushFile(ParmBlkPtr pb)
{
    stampWritten(openFork(pb->ioParam.ioRefNum));
}

void HFSPlusVolume::PBFlushVol(ParmBlkPtr)
{
    for(auto& f : forks_)
        if(f)
            stampWritten(*f);
    check(reader_->flush());
}

/* ---- the catalog ---------------------------------------------------------- */

void HFSPlusVolume::createCommon(StringPtr name, short vRef, long dirID, bool folder, GUEST<LONGINT> *newDirID)
{
    if(!writable())
        throw OSErrorException(wPrErr);
    auto [parent, leaf] = resolveForCreate(name, vRef, dirID);
    Str255 p;
    putName(p, leaf);
    std::u16string uname = newName(p);
    CatalogEntry made;
    check(reader_->create(parent.cnid, uname, folder, nullptr, &made), dirNFErr);
    if(newDirID)
        *newDirID = made.cnid;
    updateVCB();
}

void HFSPlusVolume::PBHCreate(HParmBlkPtr pb)
{
    createCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID, false, nullptr);
}
void HFSPlusVolume::PBCreate(ParmBlkPtr pb)
{
    createCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, 0, false, nullptr);
}
void HFSPlusVolume::PBDirCreate(HParmBlkPtr pb)
{
    createCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID, true, &pb->fileParam.ioDirID);
}

void HFSPlusVolume::deleteCommon(StringPtr name, short vRef, long dirID)
{
    if(!writable())
        throw OSErrorException(wPrErr);
    CatalogEntry e = resolve(name, vRef, dirID);
    if(e.cnid == kRootFolderID)
        throw OSErrorException(bdNamErr);
    if(!e.folder && isOpen(e.cnid))
        throw OSErrorException(fBsyErr);
    if(!e.folder && (e.flags & CatalogEntry::kLocked))
        throw OSErrorException(fLckdErr);
    check(reader_->remove(e.cnid));
    updateVCB();
}

void HFSPlusVolume::PBHDelete(HParmBlkPtr pb)
{
    deleteCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID);
}
void HFSPlusVolume::PBDelete(ParmBlkPtr pb)
{
    deleteCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, 0);
}

void HFSPlusVolume::renameCommon(StringPtr name, short vRef, long dirID, ConstStringPtr to)
{
    if(!writable())
        throw OSErrorException(wPrErr);
    CatalogEntry e = resolve(name, vRef, dirID);
    if(e.cnid == kRootFolderID)
    {
        check(reader_->renameVolume(newName(to, 27)));
        updateVCB();
        return;
    }
    std::u16string uname = newName(to);
    if(!e.folder && (e.flags & CatalogEntry::kLocked))
        throw OSErrorException(fLckdErr);
    check(reader_->rename(e.cnid, uname));
    refreshFile(e.cnid);
}

void HFSPlusVolume::PBHRename(HParmBlkPtr pb)
{
    renameCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID,
                 guest_cast<StringPtr>(pb->ioParam.ioMisc));
}
void HFSPlusVolume::PBRename(ParmBlkPtr pb)
{
    renameCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, 0, guest_cast<StringPtr>(pb->ioParam.ioMisc));
}

void HFSPlusVolume::PBCatMove(CMovePBPtr pb)
{
    if(!writable())
        throw OSErrorException(wPrErr);
    CatalogEntry e = resolve(pb->ioNamePtr, pb->ioVRefNum, pb->ioDirID);
    CatalogEntry to = resolve(pb->ioNewName, pb->ioVRefNum, pb->ioNewDirID);
    if(!to.folder)
        throw OSErrorException(dirNFErr);
    if(e.cnid == kRootFolderID)
        throw OSErrorException(badMovErr);
    check(reader_->move(e.cnid, to.cnid), dirNFErr);
    refreshFile(e.cnid);
}

/* SetCatInfo, SetFInfo: Finder info and dates (local, stored GMT) */
void HFSPlusVolume::setInfoCommon(CInfoPBPtr pb, long dirID, InfoKind kind)
{
    if(!writable())
        throw OSErrorException(wPrErr);
    CatalogEntry e = resolve(pb->hFileInfo.ioNamePtr, pb->hFileInfo.ioVRefNum, dirID);
    if(e.folder)
    {
        if(kind != InfoKind::CatInfo)
            throw OSErrorException(fnfErr);
        memcpy(e.userInfo, &pb->dirInfo.ioDrUsrWds, 16);
        memcpy(e.finderInfo, &pb->dirInfo.ioDrFndrInfo, 16);
        e.createDate = gmtDate(pb->dirInfo.ioDrCrDat);
        e.contentModDate = gmtDate(pb->dirInfo.ioDrMdDat);
        e.backupDate = gmtDate(pb->dirInfo.ioDrBkDat);
    }
    else
    {
        memcpy(e.userInfo, &pb->hFileInfo.ioFlFndrInfo, 16);
        e.createDate = gmtDate(pb->hFileInfo.ioFlCrDat);
        e.contentModDate = gmtDate(pb->hFileInfo.ioFlMdDat);
        if(kind == InfoKind::CatInfo)
        {
            memcpy(e.finderInfo, &pb->hFileInfo.ioFlXFndrInfo, 16);
            e.backupDate = gmtDate(pb->hFileInfo.ioFlBkDat);
        }
    }
    check(reader_->update(e));
}

void HFSPlusVolume::PBSetCatInfo(CInfoPBPtr pb)
{
    setInfoCommon(pb, pb->hFileInfo.ioDirID, InfoKind::CatInfo);
}
void HFSPlusVolume::PBHSetFInfo(HParmBlkPtr pb)
{
    setInfoCommon(reinterpret_cast<CInfoPBPtr>(pb), pb->fileParam.ioDirID, InfoKind::HFInfo);
}
void HFSPlusVolume::PBSetFInfo(ParmBlkPtr pb)
{
    setInfoCommon(reinterpret_cast<CInfoPBPtr>(pb), 0, InfoKind::FInfo);
}

void HFSPlusVolume::lockCommon(StringPtr name, short vRef, long dirID, bool lock)
{
    if(!writable())
        throw OSErrorException(wPrErr);
    CatalogEntry e = resolve(name, vRef, dirID);
    if(e.folder)
        throw OSErrorException(fnfErr);
    e.flags = lock ? (e.flags | CatalogEntry::kLocked) : (e.flags & ~CatalogEntry::kLocked);
    check(reader_->update(e));
}

void HFSPlusVolume::PBSetFLock(ParmBlkPtr pb) { lockCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, 0, true); }
void HFSPlusVolume::PBHSetFLock(HParmBlkPtr pb)
{
    lockCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID, true);
}
void HFSPlusVolume::PBRstFLock(ParmBlkPtr pb) { lockCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, 0, false); }
void HFSPlusVolume::PBHRstFLock(HParmBlkPtr pb)
{
    lockCommon(pb->ioParam.ioNamePtr, pb->ioParam.ioVRefNum, pb->fileParam.ioDirID, false);
}
void HFSPlusVolume::PBSetFVers(ParmBlkPtr) { throw OSErrorException(writable() ? paramErr : wPrErr); }

/* SetVInfo: the volume's name and Finder info (the blessed folder) */
void HFSPlusVolume::PBSetVInfo(HParmBlkPtr pb)
{
    if(!writable())
        throw OSErrorException(wPrErr);
    if(pb->volumeParam.ioNamePtr && pb->volumeParam.ioNamePtr[0]
       && pstr(pb->volumeParam.ioNamePtr) != pstr((ConstStringPtr)vcb.vcbVN))
        check(reader_->renameVolume(newName(pb->volumeParam.ioNamePtr, 27)));
    uint32_t info[8];
    for(int i = 0; i < 8; i++)
        info[i] = (uint32_t)(LONGINT)pb->volumeParam.ioVFndrInfo[i];
    check(reader_->setFinderInfo(info));
    updateVCB();
}

void HFSPlusVolume::PBLockRange(ParmBlkPtr) { throw OSErrorException(paramErr); }
void HFSPlusVolume::PBUnlockRange(ParmBlkPtr) { throw OSErrorException(paramErr); }

/* ---- unmounting ---------------------------------------------------------- */

void HFSPlusVolume::PBUnmountVol(ParmBlkPtr)
{
    for(auto& f : forks_)
        if(f)
            throw OSErrorException(fBsyErr);
    reader_->flush();
    VCBExtra *vp = (VCBExtra *)&vcb;
    Dequeue((QElemPtr)vp, &LM(VCBQHdr));
    if(LM(DefVCBPtr) == (VCB *)vp)
    {
        VCB *next = (VCB *)LM(VCBQHdr).qHead;
        LM(DefVCBPtr) = next;
        LM(DefVRefNum) = next ? (short)next->vcbVRefNum : 0;
        DefDirID = 2;
    }
    fprintf(stderr, "HFS+ volume %.*s unmounted\n", vcb.vcbVN[0], (const char *)vcb.vcbVN + 1);
    vp->volume = nullptr;
    DisposePtr((Ptr)vp);
    delete this;                                /* nothing of it is touched after this */
}

/* Off line: the VCB stays (the Finder unmounts it next), the drive goes. */
void HFSPlusVolume::PBOffLine(ParmBlkPtr)
{
    for(auto& f : forks_)
        if(f)
            throw OSErrorException(fBsyErr);
    if(vcb.vcbDrvNum)
    {
        vcb.vcbDRefNum = -vcb.vcbDrvNum;
        vcb.vcbDrvNum = 0;
    }
}

void HFSPlusVolume::PBEject(ParmBlkPtr pb)
{
    PBOffLine(pb);
}

/* ---- mounting ------------------------------------------------------------ */

bool Executor::isHFSPlusVolume(int fd, uint64_t offset)
{
    FdImageReader image(fd, false);
    return isHFSPlus(image, offset);
}

OSErr Executor::hfsPlusMountVol(ParmBlkPtr pb, int fd, uint64_t offset, drive_flags_t flags, DrvQExtra *dqp)
{
    OSErr err = noErr;
    if(ROMlib_vcbbydrive(pb->volumeParam.ioVRefNum))
        err = volOnLinErr;
    std::string why;
    std::unique_ptr<Reader> reader;
    bool canWrite = !(flags & DRIVE_FLAGS_LOCKED);
    if(err == noErr)
    {
        reader = Reader::open(std::make_shared<FdImageReader>(fd, false, canWrite), offset, &why);
        if(!reader)
        {
            warning_fs_log("no HFS+ volume at %llu: %s", (unsigned long long)offset, why.c_str());
            err = noMacDskErr;
        }
    }
    if(err == noErr)
    {
        if(reader->header().attributes & VolumeHeader::kJournaled)
            fprintf(stderr, "HFS+ volume journaled: mounted read-only, the journal not replayed\n");
        else if(canWrite && !reader->writable())
            fprintf(stderr, "HFS+ volume not cleanly unmounted (or locked): mounted read-only\n");

        GUEST<THz> saveZone = LM(TheZone);
        LM(TheZone) = LM(SysZone);
        VCBExtra *vp = (VCBExtra *)NewPtr(sizeof(VCBExtra));
        LM(TheZone) = saveZone;
        if(!vp)
            err = MemError();
        else
        {
            memset(vp, 0, sizeof(VCBExtra));
            vp->u.hfs.fd = fd;
            vp->vcb.vcbDrvNum = pb->volumeParam.ioVRefNum;
            vp->vcb.vcbDRefNum = OURHFSDREF;
            vp->vcb.vcbVRefNum = --ROMlib_nextvrn;
            vp->vcb.vcbAtrb = (reader->writable() ? 0 : VHARDLOCKBIT) | ((flags & DRIVE_FLAGS_FIXED) ? VNONEJECTABLEBIT : 0);
            bool wrapped = reader->wrapped();
            uint64_t bytes = uint64_t(reader->header().totalBlocks) * reader->header().blockSize;
            auto *volume = new HFSPlusVolume(vp->vcb, std::move(reader));
            vp->volume = volume;

            uint64_t sectors = bytes / 512;
            dqp->dq.dQDrvSz = (uint16_t)sectors;
            dqp->dq.dQDrvSz2 = (uint16_t)(sectors >> 16);
            dqp->dq.qType = 1;

            Enqueue((QElemPtr)vp, &LM(VCBQHdr));
            pb->volumeParam.ioVRefNum = vp->vcb.vcbVRefNum;
            if(!LM(DefVCBPtr))
            {
                LM(DefVCBPtr) = (VCB *)vp;
                LM(DefVRefNum) = vp->vcb.vcbVRefNum;
                DefDirID = 2;
            }
            fprintf(stderr, "HFS+ volume %.*s mounted%s, %s, vref %d\n", vp->vcb.vcbVN[0],
                    (const char *)vp->vcb.vcbVN + 1, wrapped ? " (in an HFS wrapper)" : "",
                    volume->reader().writable() ? "read-write" : "read-only", int(vp->vcb.vcbVRefNum));
        }
    }
    pb->ioParam.ioResult = err;
    return err;
}
