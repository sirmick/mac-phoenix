/* MacPhoenix: the File System Manager and the HFS utility routines
   (docs/executor/FSM.md), written from Apple's FSM.h interface and the
   behaviour of a foreign file system (PC Exchange 2.x) under 7.6.1.

   A foreign file system installs an FSD record (InstallFS), and the File
   Manager then
   - offers it every drive no volume is on (the sector its HFS component's
     idSector names, with ffsIDDiskMessage; then kFSMMountVol),
   - hands it every call whose volume it mounted (ROMlib_fsm_route: the
     HFS component gets the VCB, the call's selectCode and parameter block,
     its globals and FSID; ioTrap says which trap it was),
   and it builds its volumes from the utility routines ($A824): VCBs in
   the VCB queue, FCBs in the FCB table, working directories, and blocks
   read through the drive's driver.

   _FSMDispatch selectors (D0; arguments in A0, D3, D4): 0 InstallFS,
   1 RemoveFS, 2 GetFSInfo, 5 SetFSInfo, 6 InformFSM, identified from
   their callers (PC Exchange's INIT, the System's drivers).  Selector 4
   (A0 = 1, from the disk drivers) is not known. */

#include <base/common.h>
#include <file/fsm.h>
#include <file/volume.h>
#include <hfs/hfs.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <OSEvent.h>
#include <ResourceMgr.h>
#include <Finder.h>
#include <mman/mman.h>
#include <base/cpu.h>
#include <base/traps.h>
#include <base/emustubs.h>
#include <base/functions.impl.h>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>

using namespace Executor;

namespace
{
enum
{
    /* compInterfMask bits */
    fsmComponentEnableMask = 0x80000000,
    hfsCIDoesDynamicLoadMask = 0x00100000,
    hfsCIResourceLoadedMask = 0x00080000,
    hfsCIHasHLL2PProcMask = 0x00040000,

    /* messages to the file system's comm proc */
    ffsIDDiskMessage = 2,
    ffsLoadMessage = 3,

    /* InformFSM messages */
    fsmNopMessage = 0,
    fsmDrvQElChangedMessage = 1,
    fsmGetFSIconMessage = 2,

    fsmFFSNotFoundErr = -431,
    fsmBusyFFSErr = -432,
    fsmBadFFSNameErr = -433,
    fsmBadFSDLenErr = -434,
    fsmDuplicateFSIDErr = -435,
    fsmBadFSDVersionErr = -436,
    fsmUnknownFSMMessageErr = -438,

    fsmGetFSInfoByIndex = -1,
    fsmGetFSInfoByFSID = 0,
    fsmGetFSInfoByRefNum = 1,

    kFSMMountVol = 0xA00F,

    /* UTDetermineVol status */
    dtmvError = 0,
    dtmvFullPathname = 1,
    dtmvVRefNum = 2,
    dtmvWDRefNum = 3,
    dtmvDriveNum = 4,
    dtmvDefault = 5,

    /* cache options and answers */
    gbReadMask = 0x01,
    gbExistMask = 0x02,
    gbNoReadMask = 0x04,
    gbReleaseMask = 0x08,
    rbWriteMask = 0x01,
    rbTrashMask = 0x02,
    rbDirtyMask = 0x04,
    fcTrashMask = 0x02,
    fcFreeMask = 0x08,
    chNoBuf = 1,
    chnotfound = 3,
    chNotInUse = 4,

    BLOCK = 512,
    CACHE_BLOCKS = 64,
    CLASSIC_VCB_LENGTH = 0xB2, /* what file systems take sysVCBLength to be */
};

std::vector<FSDRec *> fsds;          /* installed, in order */
std::set<const VCB *> fsm_vcbs;      /* VCBs UTAllocateVCB made */
std::set<short> raw_drives;          /* media Executor couldn't read */
std::map<short, short> dt_volumes;   /* desktop refnum -> vRefNum */

struct CacheBlock
{
    Ptr buf;
    short drive;
    short dref;
    uint32_t block;
    int inUse;
    bool dirty;
    uint64_t used;
};
std::vector<CacheBlock> cache;
uint64_t cache_clock;

template<typename T>
T *guest(uint32_t addr)
{
    return addr ? (T *)SYN68K_TO_US(addr) : nullptr;
}

FSDRec *fsd_by_fsid(INTEGER fsid)
{
    for(FSDRec *f : fsds)
        if(f->fileSystemFSID == fsid)
            return f;
    return nullptr;
}

Ptr sys_ptr_clear(Size n)
{
    TheZoneGuard guard(LM(SysZone));
    return NewPtrClear(n);
}

/* Calls into the file system ------------------------------------------ */

OSErr call_comm(FSDRec *f, INTEGER message, Ptr paramBlock)
{
    ProcPtr proc = f->fileSystemCommProc;
    if(!proc)
        return fsmFFSNotFoundErr;
    return UPP<OSErr(INTEGER, Ptr, Ptr)>((void *)proc)(message, paramBlock,
                                                         f->fileSystemGlobalsPtr);
}

/* A dynamically loading file system's code comes in at its first use. */
void ensure_loaded(FSDRec *f)
{
    LONGINT mask = f->fsdHFSCI.compInterfMask;
    if((mask & hfsCIDoesDynamicLoadMask) && !(mask & hfsCIResourceLoadedMask))
        call_comm(f, ffsLoadMessage, nullptr);
}

/* The HFS component runs on the file system's own stack, as on a Mac
   (unless it is on it already: a call it makes back into the File
   Manager). */
OSErr call_hfsci(FSDRec *f, VCB *vcbp, uint16_t selectCode, Ptr pb)
{
    ensure_loaded(f);
    ProcPtr proc = f->fsdHFSCI.compInterfProc;
    if(!proc || !(f->fsdHFSCI.compInterfMask & fsmComponentEnableMask))
        return extFSErr;
    uint32_t top = US_TO_SYN68K(f->fsdHFSCI.stackTop);
    uint32_t size = f->fsdHFSCI.stackSize;
    uint32_t saved = EM_A7;
    bool switch_stack = top && size && !(saved <= top && saved > top - size);
    if(switch_stack)
        EM_A7 = top;
    OSErr err = UPP<OSErr(VCB *, INTEGER, Ptr, Ptr, INTEGER)>((void *)proc)(
        vcbp, (INTEGER)selectCode, pb, f->fileSystemGlobalsPtr, f->fileSystemFSID);
    if(switch_stack)
        EM_A7 = saved;
    return err;
}

/* Drives -------------------------------------------------------------- */

DrvQEl *find_drive(short drive)
{
    for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
        if(dp->dQDrive == drive)
            return dp;
    return nullptr;
}

VCB *vcb_on_drive(short drive)
{
    for(VCB *v = (VCB *)LM(VCBQHdr).qHead; v; v = (VCB *)v->qLink)
        if(v->vcbDrvNum == drive)
            return v;
    return nullptr;
}

/* Bytes at a byte offset of a drive, through its driver (Executor's disk
   images answer the driver refnum's _Read and _Write themselves). */
OSErr drive_io(short drive, short dref, uint32_t offset, uint32_t count,
               Ptr buf, bool write, uint32_t *actual = nullptr)
{
    ParmBlkPtr pb = (ParmBlkPtr)sys_ptr_clear(sizeof(ParamBlockRec));
    if(!pb)
        return memFullErr;
    pb->ioParam.ioRefNum = dref;
    pb->ioParam.ioVRefNum = drive;
    pb->ioParam.ioBuffer = buf;
    pb->ioParam.ioReqCount = count;
    pb->ioParam.ioPosMode = fsFromStart;
    pb->ioParam.ioPosOffset = offset;
    OSErr err = write ? PBWrite(pb, false) : PBRead(pb, false);
    if(actual)
        *actual = pb->ioParam.ioActCount;
    DisposePtr((Ptr)pb);
    return err;
}

/* The volume a refnum names for the cache: an FCB's (a file the file
   system opened on its volume) or a volume's. */
VCB *vcb_of_refnum(short refNum)
{
    if(refNum > 0)
    {
        uint16_t len = *(GUEST<uint16_t> *)LM(FCBSPtr);
        if(refNum < len && refNum % LM(FSFCBLen) == sizeof(short))
        {
            auto *fcbp = (filecontrolblock *)((char *)LM(FCBSPtr) + refNum);
            return fcbp->fcbVPtr;
        }
        return nullptr;
    }
    return ROMlib_findvcb(refNum, nullptr, nullptr, false);
}

/* Cache --------------------------------------------------------------- */

OSErr cache_write(CacheBlock &b)
{
    OSErr err = drive_io(b.drive, b.dref, b.block * BLOCK, BLOCK, b.buf, true);
    if(err == noErr)
        b.dirty = false;
    return err;
}

void cache_drop_if(const std::function<bool(const CacheBlock &)> &pred)
{
    for(auto it = cache.begin(); it != cache.end();)
        if(it->inUse == 0 && pred(*it))
        {
            if(it->dirty)
                cache_write(*it);
            DisposePtr(it->buf);
            it = cache.erase(it);
        }
        else
            ++it;
}

CacheBlock *cache_find(short drive, uint32_t block)
{
    for(auto &b : cache)
        if(b.drive == drive && b.block == block)
            return &b;
    return nullptr;
}

CacheBlock *cache_by_buffer(uint32_t buf)
{
    for(auto &b : cache)
        if(US_TO_SYN68K(b.buf) == buf)
            return &b;
    return nullptr;
}

CacheBlock *cache_new(short drive, short dref, uint32_t block)
{
    if(cache.size() >= CACHE_BLOCKS)
    {
        auto victim = cache.end();
        for(auto it = cache.begin(); it != cache.end(); ++it)
            if(it->inUse == 0 && (victim == cache.end() || it->used < victim->used))
                victim = it;
        if(victim == cache.end())
            return nullptr;
        if(victim->dirty)
            cache_write(*victim);
        DisposePtr(victim->buf);
        cache.erase(victim);
    }
    Ptr buf = sys_ptr_clear(BLOCK);
    if(!buf)
        return nullptr;
    cache.push_back({ buf, drive, dref, block, 0, false, ++cache_clock });
    return &cache.back();
}

/* A file position to a volume byte offset: the file system's log2PhyProc
   (Pascal when it says so), or the position itself when there is none. */
OSErr log_to_phys(uint32_t log2Phy, VCB *vcbp, short refNum, uint32_t pos,
                  uint32_t count, uint32_t *volOffset, uint32_t *contiguous)
{
    if(!log2Phy)
    {
        *volOffset = pos;
        *contiguous = count;
        return noErr;
    }
    FSDRec *f = fsd_by_fsid(vcbp->vcbFSID);
    if(!f || !(f->fsdHFSCI.compInterfMask & hfsCIHasHLL2PProcMask))
    {
        traps::log_unimplemented("HFS utility log2PhyProc without the HLL bit");
        return paramErr;
    }
    filecontrolblock *fcbp = ROMlib_refnumtofcbp(refNum);
    Ptr out = sys_ptr_clear(8);
    OSErr err = UPP<OSErr(Ptr, VCB *, Ptr, INTEGER, LONGINT, LONGINT, Ptr, Ptr)>(
        guest<void>(log2Phy))(f->fileSystemGlobalsPtr, vcbp, (Ptr)fcbp, refNum,
                              pos, count, out, out + 4);
    *volOffset = *(GUEST<uint32_t> *)out;
    *contiguous = *(GUEST<uint32_t> *)(out + 4);
    DisposePtr(out);
    return err;
}

/* Bytes of a file (or the volume, without a log2PhyProc) straight to or
   from the caller's buffer, through the cache's blocks where they are. */
OSErr cache_io_in_place(uint32_t log2Phy, uint32_t pos, Ptr buffer, short refNum,
                        uint32_t count, uint32_t *actual, bool write)
{
    *actual = 0;
    VCB *vcbp = vcb_of_refnum(refNum);
    if(!vcbp)
        return rfNumErr;
    if(vcbp->vcbDrvNum == 0)
        return volOffLinErr;
    short drive = vcbp->vcbDrvNum, dref = vcbp->vcbDRefNum;
    while(count)
    {
        uint32_t phys, contiguous;
        OSErr err = log_to_phys(log2Phy, vcbp, refNum, pos, count, &phys, &contiguous);
        if(err != noErr)
            return err;
        uint32_t n = std::min(count, std::max<uint32_t>(contiguous, 1));
        uint32_t first = phys / BLOCK, last = (phys + n - 1) / BLOCK;
        /* write back what the cache holds of these blocks first, and
           forget them after a write */
        for(auto &b : cache)
            if(b.drive == drive && b.block >= first && b.block <= last && b.dirty)
                cache_write(b);
        if(write)
            cache_drop_if([&](const CacheBlock &b) {
                return b.drive == drive && b.block >= first && b.block <= last;
            });
        if(phys % BLOCK == 0 && n % BLOCK == 0)
        {
            uint32_t got = 0;
            err = drive_io(drive, dref, phys, n, buffer, write, &got);
            *actual += got;
            if(err != noErr)
                return err;
        }
        else
        {
            /* a part of a block: through a block of our own */
            Ptr tmp = sys_ptr_clear(BLOCK);
            uint32_t done = 0;
            while(done < n)
            {
                uint32_t at = phys + done, blk = at / BLOCK, in = at % BLOCK;
                uint32_t part = std::min<uint32_t>(n - done, BLOCK - in);
                err = drive_io(drive, dref, blk * BLOCK, BLOCK, tmp, false);
                if(err == noErr && write)
                {
                    memcpy(tmp + in, buffer + done, part);
                    err = drive_io(drive, dref, blk * BLOCK, BLOCK, tmp, true);
                }
                else if(err == noErr)
                    memcpy(buffer + done, tmp + in, part);
                if(err != noErr)
                    break;
                done += part;
            }
            DisposePtr(tmp);
            *actual += done;
            if(err != noErr)
                return err;
        }
        buffer += n;
        pos += n;
        count -= n;
    }
    return noErr;
}

/* Volumes ------------------------------------------------------------- */

void release_wds_of(VCB *vcbp)
{
    wdentry *wdp = (wdentry *)(LM(WDCBsPtr) + sizeof(INTEGER));
    wdentry *ewdp = (wdentry *)(LM(WDCBsPtr) + *(GUEST<INTEGER> *)LM(WDCBsPtr));
    for(; wdp != ewdp; ++wdp)
        if(wdp->vcbp == (HVCB *)vcbp)
            wdp->vcbp = nullptr;
}

bool full_pathname(StringPtr name, int *volLen)
{
    if(!name || name[0] == 0 || name[1] == ':')
        return false;
    for(int i = 1; i <= name[0]; ++i)
        if(name[i] == ':')
        {
            *volLen = i - 1;
            return true;
        }
    return false;
}

bool vcb_name_is(VCB *v, StringPtr name, int len)
{
    Str255 n;
    n[0] = len;
    memcpy(n + 1, name + 1, len);
    return EqualString(v->vcbVN, n, false, true);
}

/* Each volume of that name after `after` (the first if nil). */
VCB *next_vcb_named(StringPtr name, VCB *after, short *more)
{
    int len = name ? name[0] : 0;
    if(len && name[len] == ':')
        --len;
    VCB *found = nullptr;
    *more = 0;
    VCB *v = after ? (VCB *)after->qLink : (VCB *)LM(VCBQHdr).qHead;
    for(; v; v = (VCB *)v->qLink)
        if(vcb_name_is(v, name, len))
        {
            if(!found)
                found = v;
            else
                ++*more;
        }
    return found;
}

/* The routing ---------------------------------------------------------- */

VCB *target_vcb(void *vpb, FsmTarget target)
{
    auto *pb = (ParmBlkPtr)vpb;
    switch(target)
    {
        case FsmTarget::Path:
            return ROMlib_findvcb(pb->ioParam.ioVRefNum, pb->ioParam.ioNamePtr,
                                  nullptr, true);
        case FsmTarget::Ref:
            return ROMlib_findvcb(pb->ioParam.ioVRefNum, nullptr, nullptr, true);
        case FsmTarget::File:
        {
            filecontrolblock *fcbp = ROMlib_refnumtofcbp(pb->ioParam.ioRefNum);
            return fcbp ? (VCB *)fcbp->fcbVPtr : nullptr;
        }
        case FsmTarget::VolIndex:
        {
            INTEGER index = pb->volumeParam.ioVolIndex;
            if(index > 0)
                return (VCB *)ROMlib_indexqueue(&LM(VCBQHdr), index);
            if(index == 0)
                return ROMlib_findvcb(pb->ioParam.ioVRefNum, nullptr, nullptr, true);
            return ROMlib_findvcb(pb->ioParam.ioVRefNum, pb->ioParam.ioNamePtr,
                                  nullptr, true);
        }
        case FsmTarget::Default:
            return LM(DefVCBPtr);
        case FsmTarget::WD:
        {
            INTEGER v = pb->ioParam.ioVRefNum;
            if(ISWDNUM(v))
                return WDNUMTOWDP(v)->vcbp;
            return ROMlib_findvcb(v, nullptr, nullptr, false);
        }
        case FsmTarget::DTRef:
        {
            short ref = ((DTPBRec *)vpb)->ioDTRefNum;
            auto it = dt_volumes.find(ref);
            if(it != dt_volumes.end())
                return ROMlib_vcbbyvrn(it->second);
            filecontrolblock *fcbp = ROMlib_refnumtofcbp(ref);
            return fcbp ? (VCB *)fcbp->fcbVPtr : nullptr;
        }
    }
    return nullptr;
}

/* Every unrecognised drive with no volume, offered again. */
void offer_drives()
{
    for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
    {
        short drive = dp->dQDrive;
        if(!raw_drives.count(drive) || vcb_on_drive(drive))
            continue;
        ParmBlkPtr pb = (ParmBlkPtr)sys_ptr_clear(sizeof(ParamBlockRec));
        if(!pb)
            return;
        pb->ioParam.ioVRefNum = drive;
        OSErr err;
        if(ROMlib_fsm_mount(drive, pb, &err))
            PPostEvent(diskEvt, ((LONGINT)err << 16) | (uint16_t)drive, nullptr);
        DisposePtr((Ptr)pb);
    }
}

/* _FSMDispatch ---------------------------------------------------------- */

OSErr install_fs(FSDRec *given)
{
    if(!given)
        return paramErr;
    FSDRec *f = given;
    if(f->fsdVersion != 1)
        return fsmBadFSDVersionErr;
    if(f->fsdLength < (INTEGER)sizeof(FSDRec))
        return fsmBadFSDLenErr;
    if(f->fileSystemName[0] < 1 || f->fileSystemName[0] > 31)
        return fsmBadFFSNameErr;
    if(fsd_by_fsid(f->fileSystemFSID))
        return fsmDuplicateFSIDErr;
    /* the File System Manager keeps a copy (PC Exchange disposes of its
       record as soon as InstallFS returns) */
    f = (FSDRec *)sys_ptr_clear(given->fsdLength);
    if(!f)
        return memFullErr;
    memcpy(f, given, given->fsdLength);
    /* the file system's file, if it didn't say: the one its INIT runs
       from (its load message opens it again from here) */
    if(f->fileSystemSpec.vRefNum == 0 && f->fileSystemSpec.name[0] == 0)
    {
        FCBPBRec fpb;
        Str255 name;
        memset(&fpb, 0, sizeof fpb);
        fpb.ioRefNum = LM(CurMap);
        fpb.ioNamePtr = name;
        if(PBGetFCBInfo(&fpb, false) == noErr)
        {
            f->fileSystemSpec.vRefNum = fpb.ioFCBVRefNum;
            f->fileSystemSpec.parID = fpb.ioFCBParID;
            memcpy(f->fileSystemSpec.name, name, name[0] + 1);
        }
    }
    f->fsdLink = nullptr;
    if(!fsds.empty())
        fsds.back()->fsdLink = f;
    fsds.push_back(f);
    fprintf(stderr, "[Executor] FSM: %.*s installed (FSID $%04X)\n",
            f->fileSystemName[0], (char *)f->fileSystemName + 1,
            (uint16_t)f->fileSystemFSID);
    return noErr;
}

OSErr remove_fs(INTEGER fsid)
{
    FSDRec *f = fsd_by_fsid(fsid);
    if(!f)
        return fsmFFSNotFoundErr;
    for(VCB *v = (VCB *)LM(VCBQHdr).qHead; v; v = (VCB *)v->qLink)
        if(ROMlib_fsm_vcb_p(v) && v->vcbFSID == fsid)
            return fsmBusyFFSErr;
    auto it = std::find(fsds.begin(), fsds.end(), f);
    if(it != fsds.begin())
        (*(it - 1))->fsdLink = (it + 1 != fsds.end()) ? *(it + 1) : nullptr;
    fsds.erase(it);
    DisposePtr((Ptr)f);
    return noErr;
}

FSDRec *fsd_for_info(INTEGER selector, INTEGER key)
{
    switch(selector)
    {
        case fsmGetFSInfoByIndex:
            return key >= 1 && key <= (INTEGER)fsds.size() ? fsds[key - 1] : nullptr;
        case fsmGetFSInfoByFSID:
            return fsd_by_fsid(key);
        case fsmGetFSInfoByRefNum:
        {
            VCB *v = vcb_of_refnum(key);
            return v ? fsd_by_fsid(v->vcbFSID) : nullptr;
        }
    }
    return nullptr;
}

OSErr inform_fsm(INTEGER message, uint32_t paramBlock)
{
    switch(message)
    {
        case fsmNopMessage:
            return noErr;
        case fsmDrvQElChangedMessage:
            /* a drive (or, with nil, any) may now be one a file system
               can read: PC Exchange says so once it has installed */
            offer_drives();
            return noErr;
        case fsmGetFSIconMessage:
        {
            /* FSMGetIconRec: refNum is a drive or volume; the file system
               that has it answers */
            short refNum = *guest<GUEST<INTEGER>>(paramBlock);
            VCB *v = refNum > 0 ? vcb_on_drive(refNum)
                                : ROMlib_findvcb(refNum, nullptr, nullptr, false);
            FSDRec *f = v && ROMlib_fsm_vcb_p(v) ? fsd_by_fsid(v->vcbFSID) : nullptr;
            if(!f)
                return fsmFFSNotFoundErr;
            return call_comm(f, 1 /* ffsGetIconMessage */, guest<char>(paramBlock));
        }
    }
    return fsmUnknownFSMMessageErr;
}
}

RAW_68K_IMPLEMENTATION(FSMDispatch)
{
    OSErr err;
    INTEGER d3 = EM_D3, d4 = EM_D4;
    switch(EM_D0 & 0xFFFF)
    {
        case 0: /* InstallFS(A0 = fsdPtr) */
            err = install_fs(guest<FSDRec>(EM_A0));
            break;
        case 1: /* RemoveFS(A0 = fsid) */
            err = remove_fs((INTEGER)EM_A0);
            break;
        case 2: /* GetFSInfo(D4 = selector, D3 = key, A0 = fsdPtr whose
                   fsdLength is the buffer's size) -> D4 = size copied
                   (the glue moves D4 to *bufSize) */
        {
            auto *buf = guest<FSDRec>(EM_A0);
            FSDRec *f = fsd_for_info(d4, d3);
            if(!buf)
                err = paramErr;
            else if(!f)
                err = fsmFFSNotFoundErr;
            else
            {
                INTEGER n = std::min<INTEGER>(buf->fsdLength, f->fsdLength);
                memcpy(buf, f, n);
                EM_D4 = n;
                err = noErr;
            }
            break;
        }
        case 5: /* SetFSInfo(D4 = fsid, D3 = size, A0 = fsdPtr): the record
                   the file system installed takes the fields after the
                   link and the FSID */
        {
            auto *src = guest<FSDRec>(EM_A0);
            FSDRec *f = fsd_by_fsid(d4);
            if(!src)
                err = paramErr;
            else if(!f)
                err = fsmFFSNotFoundErr;
            else
            {
                INTEGER n = std::min<INTEGER>(d3, f->fsdLength);
                const size_t keep = offsetof(FSDRec, fileSystemName);
                if(n > (INTEGER)keep)
                    memcpy((char *)f + keep, (char *)src + keep, n - keep);
                err = noErr;
            }
            break;
        }
        case 6: /* InformFSM(D3 = message, A0 = paramBlock) */
            err = inform_fsm(d3, EM_A0);
            break;
        case 4: /* the disk drivers' (A0 = 1): not known */
            traps::log_unimplemented("_FSMDispatch selector 4");
            err = noErr;
            break;
        default:
        {
            char what[64];
            snprintf(what, sizeof what, "_FSMDispatch selector %d", (int)(EM_D0 & 0xFFFF));
            traps::log_unimplemented(what);
            err = paramErr;
            break;
        }
    }
    EM_D0 = err;
    return POPADDR();
}

/* $A074: an OS trap no interface file names. PC Exchange's INIT calls it
   with D0 = 4 and A0 at an 80-byte block, keeps the long at offset $1C
   on noErr and uses it as a transfer size above which its reads and
   writes go another way; on an error it keeps 0, no limit.  Answered
   with paramErr. */
RAW_68K_IMPLEMENTATION(OSTrapA074)
{
    char what[64];
    snprintf(what, sizeof what, "OS trap $A074 (D0 = %d)", (int)(EM_D0 & 0xFFFF));
    traps::log_unimplemented(what);
    EM_D0 = paramErr;
    return POPADDR();
}

/* The HFS utility routines ---------------------------------------------- */

namespace
{
filecontrolblock *fcb_at(short refNum)
{
    uint16_t len = *(GUEST<uint16_t> *)LM(FCBSPtr);
    if(refNum <= 0 || refNum >= len || refNum % LM(FSFCBLen) != sizeof(short))
        return nullptr;
    return (filecontrolblock *)((char *)LM(FCBSPtr) + refNum);
}

short refnum_of(filecontrolblock *fcbp)
{
    return (char *)fcbp - (char *)LM(FCBSPtr);
}

/* The next FCB in use after `after` (0: from the first) that the
   predicate takes. */
filecontrolblock *next_fcb(short after, const std::function<bool(filecontrolblock *)> &pred)
{
    uint16_t len = *(GUEST<uint16_t> *)LM(FCBSPtr);
    short ref = after > 0 ? after + LM(FSFCBLen) : sizeof(short);
    for(; ref + LM(FSFCBLen) <= len; ref += LM(FSFCBLen))
    {
        auto *fcbp = (filecontrolblock *)((char *)LM(FCBSPtr) + ref);
        if(fcbp->fcbFlNum != 0 && pred(fcbp))
            return fcbp;
    }
    return nullptr;
}

OSErr locate_fcb(VCB *vcbp, uint32_t fileNum, StringPtr name, short after,
                 GUEST<INTEGER> *refp, GUEST<filecontrolblock *> *fcbpp)
{
    filecontrolblock *fcbp = next_fcb(after, [&](filecontrolblock *f) {
        if(vcbp && (VCB *)f->fcbVPtr != vcbp)
            return false;
        if(fileNum)
            return (uint32_t)f->fcbFlNum == fileNum;
        return name && EqualString(name, f->fcbCName, false, true);
    });
    if(!fcbp)
        return fnfErr; /* the end: PC Exchange indexes until fnfErr */
    if(refp)
        *refp = refnum_of(fcbp);
    if(fcbpp)
        *fcbpp = fcbp;
    return noErr;
}

void set_default(VCB *vcbp, short refNum, LONGINT dirID)
{
    if(ISWDNUM(refNum))
        DefDirID = dirID ? dirID : (LONGINT)WDNUMTOWDP(refNum)->dirid;
    else
        DefDirID = dirID ? dirID : 2;
    LM(DefVCBPtr) = vcbp;
    LM(DefVRefNum) = refNum;
}

OSErr determine_vol(ParmBlkPtr pb, short *status, short *more, VCB **vcbpp)
{
    *more = 0;
    *vcbpp = nullptr;
    *status = dtmvError;
    StringPtr name = pb->ioParam.ioNamePtr;
    INTEGER v = pb->ioParam.ioVRefNum;
    int volLen;
    VCB *vcbp;
    /* GetVolInfo by index: the nth volume (its ioNamePtr is the answer's
       buffer) */
    bool getVolInfo = ((uint16_t)pb->ioParam.ioTrap & 0xF9FF) == 0xA007;
    if(getVolInfo && pb->volumeParam.ioVolIndex > 0)
    {
        vcbp = (VCB *)ROMlib_indexqueue(&LM(VCBQHdr), pb->volumeParam.ioVolIndex);
        *status = dtmvVRefNum;
        *vcbpp = vcbp;
        return vcbp ? noErr : nsvErr;
    }
    if(getVolInfo && pb->volumeParam.ioVolIndex == 0)
        name = nullptr;
    if(full_pathname(name, &volLen))
    {
        Str255 vol;
        vol[0] = volLen;
        memcpy(vol + 1, name + 1, volLen);
        vcbp = next_vcb_named(vol, nullptr, more);
        *status = dtmvFullPathname;
    }
    else if(v == 0)
    {
        vcbp = LM(DefVCBPtr);
        *status = dtmvDefault;
    }
    else if(ISWDNUM(v))
    {
        vcbp = WDNUMTOWDP(v)->vcbp;
        *status = dtmvWDRefNum;
    }
    else if(v < 0)
    {
        vcbp = ROMlib_vcbbyvrn(v);
        *status = dtmvVRefNum;
    }
    else
    {
        vcbp = vcb_on_drive(v);
        *status = dtmvDriveNum;
        if(!vcbp)
            return find_drive(v) ? nsvErr : nsDrvErr;
    }
    *vcbpp = vcbp;
    return vcbp ? noErr : nsvErr;
}
}

/* The caller's arguments, last pushed first (Pascal). */
#define ARG_L() POPUL()
#define ARG_W() ((INTEGER)POPUW())
#define OUT(T, addr) (*guest<GUEST<T>>(addr))

RAW_68K_IMPLEMENTATION(HFSUtilDispatch)
{
    syn68k_addr_t ret = POPADDR();
    OSErr err = noErr;
    int sel = EM_D0 & 0xFFFF;
    switch(sel)
    {
        case 0x00: /* UTAllocateFCB(short *refNum, FCBRecPtr *fcb) */
        {
            uint32_t fcbpp = ARG_L(), refp = ARG_L();
            filecontrolblock *fcbp = ROMlib_getfreefcbp();
            if(!fcbp)
                err = tmfoErr;
            else
            {
                memset(fcbp, 0, LM(FSFCBLen));
                if(refp)
                    OUT(INTEGER, refp) = refnum_of(fcbp);
                if(fcbpp)
                    OUT(filecontrolblock *, fcbpp) = fcbp;
            }
            break;
        }
        case 0x01: /* UTReleaseFCB(short refNum) */
        {
            filecontrolblock *fcbp = fcb_at(ARG_W());
            if(!fcbp)
                err = rfNumErr;
            else
                fcbp->fcbFlNum = 0;
            break;
        }
        case 0x02: /* UTLocateFCB(vcb, fileNum, name, short *refNum, FCBRecPtr *fcb) */
        case 0x03: /* UTLocateNextFCB(... *refNum in: where to go on from) */
        {
            uint32_t fcbpp = ARG_L(), refp = ARG_L(), namep = ARG_L();
            uint32_t fileNum = ARG_L(), vcbp = ARG_L();
            short after = sel == 0x03 && refp ? (short)OUT(INTEGER, refp) : 0;
            err = locate_fcb(guest<VCB>(vcbp), fileNum, guest<unsigned char>(namep),
                             after, guest<GUEST<INTEGER>>(refp),
                             guest<GUEST<filecontrolblock *>>(fcbpp));
            break;
        }
        case 0x04: /* UTIndexFCB(vcb, short *refNum (in: after), FCBRecPtr *fcb) */
        {
            uint32_t fcbpp = ARG_L(), refp = ARG_L(), vcbp = ARG_L();
            VCB *v = guest<VCB>(vcbp);
            short after = refp ? (short)OUT(INTEGER, refp) : 0;
            filecontrolblock *fcbp = next_fcb(after, [&](filecontrolblock *f) {
                return !v || (VCB *)f->fcbVPtr == v;
            });
            if(!fcbp)
                err = fnfErr; /* the end: PC Exchange indexes until fnfErr */
            else
            {
                if(refp)
                    OUT(INTEGER, refp) = refnum_of(fcbp);
                if(fcbpp)
                    OUT(filecontrolblock *, fcbpp) = fcbp;
            }
            break;
        }
        case 0x05: /* UTResolveFCB(short refNum, FCBRecPtr *fcb) */
        {
            uint32_t fcbpp = ARG_L();
            filecontrolblock *fcbp = fcb_at(ARG_W());
            if(!fcbp)
                err = rfNumErr;
            else if(fcbpp)
                OUT(filecontrolblock *, fcbpp) = fcbp;
            break;
        }
        case 0x06: /* UTAllocateVCB(ushort *sysVCBLength, VCBPtr *vcb, ushort addSize) */
        {
            uint16_t addSize = POPUW();
            uint32_t vcbpp = ARG_L(), lenp = ARG_L();
            VCB *v = (VCB *)sys_ptr_clear(CLASSIC_VCB_LENGTH + addSize);
            if(!v)
                err = memFullErr;
            else
            {
                fsm_vcbs.insert(v);
                if(lenp)
                    OUT(uint16_t, lenp) = CLASSIC_VCB_LENGTH;
                if(vcbpp)
                    OUT(VCB *, vcbpp) = v;
            }
            break;
        }
        case 0x07: /* UTAddNewVCB(short drive, short *vRefNum, VCBPtr vcb) */
        {
            uint32_t vcbp = ARG_L(), vrefp = ARG_L();
            INTEGER drive = ARG_W();
            VCB *v = guest<VCB>(vcbp);
            if(!v)
            {
                err = paramErr;
                break;
            }
            DrvQEl *dp = find_drive(drive);
            v->vcbDrvNum = drive;
            v->vcbDRefNum = dp ? (INTEGER)dp->dQRefNum : 0;
            v->vcbVRefNum = --ROMlib_nextvrn;
            Enqueue((QElemPtr)v, &LM(VCBQHdr));
            if(!LM(DefVCBPtr))
                set_default(v, v->vcbVRefNum, 2);
            if(vrefp)
                OUT(INTEGER, vrefp) = v->vcbVRefNum;
            break;
        }
        case 0x08: /* UTDisposeVCB(VCBPtr) */
        {
            VCB *v = guest<VCB>(ARG_L());
            if(!v || !fsm_vcbs.count(v))
            {
                err = paramErr;
                break;
            }
            Dequeue((QElemPtr)v, &LM(VCBQHdr));
            release_wds_of(v);
            cache_drop_if([&](const CacheBlock &b) { return b.drive == v->vcbDrvNum; });
            if(LM(DefVCBPtr) == v)
            {
                VCB *first = (VCB *)LM(VCBQHdr).qHead;
                set_default(first, first ? (INTEGER)first->vcbVRefNum : 0, 2);
            }
            for(auto it = dt_volumes.begin(); it != dt_volumes.end();)
                it = it->second == v->vcbVRefNum ? dt_volumes.erase(it) : std::next(it);
            fsm_vcbs.erase(v);
            DisposePtr((Ptr)v);
            break;
        }
        case 0x09: /* UTLocateVCBByRefNum(short refNum, short *vRefNum, VCBPtr *vcb) */
        {
            uint32_t vcbpp = ARG_L(), vrefp = ARG_L();
            INTEGER ref = ARG_W();
            VCB *v = ref > 0 ? vcb_on_drive(ref) : ROMlib_findvcb(ref, nullptr, nullptr, false);
            if(!v)
                err = nsvErr;
            else
            {
                if(vrefp)
                    OUT(INTEGER, vrefp) = v->vcbVRefNum;
                if(vcbpp)
                    OUT(VCB *, vcbpp) = v;
            }
            break;
        }
        case 0x0A: /* UTLocateVCBByName(name, short *more, short *vRefNum, VCBPtr *vcb) */
        case 0x0B: /* UTLocateNextVCB(... *vcb in: the last found) */
        {
            uint32_t vcbpp = ARG_L(), vrefp = ARG_L(), morep = ARG_L(), namep = ARG_L();
            VCB *after = sel == 0x0B && vcbpp ? (VCB *)OUT(VCB *, vcbpp) : nullptr;
            short more;
            VCB *v = next_vcb_named(guest<unsigned char>(namep), after, &more);
            if(morep)
                OUT(INTEGER, morep) = more;
            if(!v)
                err = nsvErr;
            else
            {
                if(vrefp)
                    OUT(INTEGER, vrefp) = v->vcbVRefNum;
                if(vcbpp)
                    OUT(VCB *, vcbpp) = v;
            }
            break;
        }
        case 0x0C: /* UTAllocateWDCB(WDPBPtr): ioVRefNum, ioWDDirID, ioWDProcID
                      -> ioVRefNum the working directory */
        {
            auto *pb = guest<WDPBRec>(ARG_L());
            VCB *v = pb ? ROMlib_findvcb(pb->ioVRefNum, nullptr, nullptr, false) : nullptr;
            if(!v)
                err = nsvErr;
            else
                err = ROMlib_mkwd(pb, (HVCB *)v, pb->ioWDDirID, pb->ioWDProcID);
            break;
        }
        case 0x0D: /* UTReleaseWDCB(short wdRefNum) */
        {
            INTEGER wd = ARG_W();
            if(!ISWDNUM(wd))
                err = rfNumErr;
            else
                WDNUMTOWDP(wd)->vcbp = nullptr;
            break;
        }
        case 0x0E: /* UTResolveWDCB(long procID, short wdIndex, short wdRefNum,
                      WDCBRecPtr *wdcb) */
        {
            uint32_t wdpp = ARG_L();
            INTEGER wdRefNum = ARG_W(), wdIndex = ARG_W();
            LONGINT procID = ARG_L();
            wdentry *found = nullptr;
            if(wdIndex > 0)
            {
                wdentry *wdp = (wdentry *)(LM(WDCBsPtr) + sizeof(INTEGER));
                wdentry *ewdp = (wdentry *)(LM(WDCBsPtr) + *(GUEST<INTEGER> *)LM(WDCBsPtr));
                for(int i = wdIndex; wdp != ewdp; ++wdp)
                    if(wdp->vcbp && (!procID || wdp->procid == procID) && --i == 0)
                    {
                        found = wdp;
                        break;
                    }
            }
            else if(ISWDNUM(wdRefNum) && WDNUMTOWDP(wdRefNum)->vcbp)
                found = WDNUMTOWDP(wdRefNum);
            if(!found)
                err = nsvErr;
            else if(wdpp)
                OUT(wdentry *, wdpp) = found;
            break;
        }
        case 0x0F: /* UTFindDrive(short drive, DrvQElPtr *dqe) */
        {
            uint32_t dqpp = ARG_L();
            DrvQEl *dp = find_drive(ARG_W());
            if(!dp)
                err = nsDrvErr;
            else if(dqpp)
                OUT(DrvQEl *, dqpp) = dp;
            break;
        }
        case 0x10: /* UTAdjustEOF(short refNum): the fork's other paths take
                      this one's length */
        {
            filecontrolblock *fcbp = fcb_at(ARG_W());
            if(!fcbp)
            {
                err = rfNumErr;
                break;
            }
            bool rsrc = fcbp->fcbMdRByt & 2;
            for(filecontrolblock *o = next_fcb(0, [&](filecontrolblock *f) {
                    return f != fcbp && f->fcbVPtr == fcbp->fcbVPtr
                        && f->fcbFlNum == fcbp->fcbFlNum && ((f->fcbMdRByt & 2) != 0) == rsrc;
                });
                o; o = next_fcb(refnum_of(o), [&](filecontrolblock *f) {
                    return f != fcbp && f->fcbVPtr == fcbp->fcbVPtr
                        && f->fcbFlNum == fcbp->fcbFlNum && ((f->fcbMdRByt & 2) != 0) == rsrc;
                }))
            {
                o->fcbEOF = fcbp->fcbEOF;
                o->fcbPLen = fcbp->fcbPLen;
                memcpy(&o->fcbExtRec, &fcbp->fcbExtRec, sizeof o->fcbExtRec);
            }
            break;
        }
        case 0x11: /* UTSetDefaultVol(long nodeHint, ulong dirID, short refNum) */
        {
            INTEGER ref = ARG_W();
            LONGINT dirID = ARG_L();
            ARG_L(); /* nodeHint */
            VCB *v = ref > 0 ? vcb_on_drive(ref) : ROMlib_findvcb(ref, nullptr, nullptr, false);
            if(!v)
                err = nsvErr;
            else
                set_default(v, ref > 0 ? (INTEGER)v->vcbVRefNum : ref, dirID);
            break;
        }
        case 0x12: /* UTGetDefaultVol(WDPBPtr): as PBHGetVol */
        {
            auto *pb = guest<WDPBRec>(ARG_L());
            err = pb ? hfsPBHGetVol(pb, false) : paramErr;
            break;
        }
        case 0x13: /* UTCheckWDRefNum(short) */
        {
            INTEGER wd = ARG_W();
            err = ISWDNUM(wd) && WDNUMTOWDP(wd)->vcbp ? noErr : nsvErr;
            break;
        }
        case 0x14: /* UTCheckFileRefNum(short) */
        {
            filecontrolblock *fcbp = fcb_at(ARG_W());
            err = fcbp && fcbp->fcbFlNum ? noErr : rfNumErr;
            break;
        }
        case 0x15: /* UTCheckVolRefNum(short) */
            err = ROMlib_vcbbyvrn(ARG_W()) ? noErr : nsvErr;
            break;
        case 0x16: /* UTCheckPermission(vcb, short *modByte, ulong fileNum, ParmBlkPtr) */
        {
            uint32_t pbp = ARG_L();
            uint32_t fileNum = ARG_L();
            uint32_t modp = ARG_L();
            VCB *v = guest<VCB>(ARG_L());
            auto *pb = guest<ParamBlockRec>(pbp);
            SignedByte perm = pb ? (SignedByte)pb->ioParam.ioPermssn : fsCurPerm;
            bool wantWrite = perm == fsCurPerm || perm == fsWrPerm || perm == fsRdWrPerm
                || perm == fsRdWrShPerm;
            Byte mod = 0;
            if(wantWrite && v && volumenotlocked(v) != noErr)
            {
                if(perm != fsCurPerm)
                    err = volumenotlocked(v);
                wantWrite = false;
            }
            if(err == noErr && wantWrite && perm != fsRdWrShPerm
               && next_fcb(0, [&](filecontrolblock *f) {
                      return (VCB *)f->fcbVPtr == v && (uint32_t)f->fcbFlNum == fileNum
                          && (f->fcbMdRByt & 1);
                  }))
            {
                if(perm == fsCurPerm)
                    wantWrite = false;
                else
                    err = opWrErr;
            }
            if(wantWrite)
                mod |= 1;
            if(perm == fsRdWrShPerm)
                mod |= 0x10;
            if(modp)
                OUT(INTEGER, modp) = mod;
            break;
        }
        case 0x17: /* UTCheckVolOffline(short vRefNum) */
        {
            VCB *v = ROMlib_vcbbyvrn(ARG_W());
            err = !v ? nsvErr : v->vcbDrvNum == 0 ? volOffLinErr : noErr;
            break;
        }
        case 0x18: /* UTCheckVolModifiable(short vRefNum) */
        {
            VCB *v = ROMlib_vcbbyvrn(ARG_W());
            err = v ? volumenotlocked(v) : nsvErr;
            break;
        }
        case 0x19: /* UTCheckFileModifiable(short refNum) */
        {
            filecontrolblock *fcbp = fcb_at(ARG_W());
            if(!fcbp || !fcbp->fcbFlNum)
                err = rfNumErr;
            else if(!(fcbp->fcbMdRByt & 1))
                err = wrPermErr;
            else
                err = volumenotlocked((VCB *)fcbp->fcbVPtr);
            break;
        }
        case 0x1A: /* UTCheckDirBusy(vcb, ulong dirID) */
        {
            LONGINT dirID = ARG_L();
            VCB *v = guest<VCB>(ARG_L());
            wdentry *wdp = (wdentry *)(LM(WDCBsPtr) + sizeof(INTEGER));
            wdentry *ewdp = (wdentry *)(LM(WDCBsPtr) + *(GUEST<INTEGER> *)LM(WDCBsPtr));
            for(; wdp != ewdp; ++wdp)
                if(wdp->vcbp == (HVCB *)v && wdp->dirid == dirID)
                    err = fBsyErr;
            break;
        }
        case 0x1B: /* UTParsePathname(short *volNameLength, StringPtr name):
                      the volume name's length if it is a full pathname */
        {
            StringPtr name = guest<unsigned char>(ARG_L());
            uint32_t lenp = ARG_L();
            int volLen = 0;
            if(!full_pathname(name, &volLen))
                volLen = 0;
            else if(volLen > 27)
                err = bdNamErr;
            if(lenp)
                OUT(INTEGER, lenp) = volLen;
            break;
        }
        case 0x1C: /* UTGetPathComponentName(ParsePathRecPtr): from startOffset, a
                      0-based index into the name's characters, the component
                      there; a partial path's leading colon is skipped (and
                      startOffset moved past it); an empty component that
                      stops at a colon is "::", the parent. The caller moves
                      on by componentLength + 1 while moreName is set. */
        {
            uint32_t rec = ARG_L();
            StringPtr name = guest<unsigned char>(OUT(uint32_t, rec));
            int len = name ? name[0] : 0;
            int p = (INTEGER)OUT(INTEGER, rec + 4);
            if(p == 0 && len > 0 && name[1] == ':')
                OUT(INTEGER, rec + 4) = p = 1;
            int begin = p;
            while(p < len && name[p + 1] != ':')
                ++p;
            int compLen = p - begin;
            bool delim = p < len;
            bool more = delim && p + 1 < len;
            OUT(INTEGER, rec + 6) = compLen;
            *guest<SignedByte>(rec + 8) = more ? 1 : 0;
            *guest<SignedByte>(rec + 9) = delim ? 1 : 0;
            if(compLen > 31)
                err = bdNamErr;
            break;
        }
        case 0x1D: /* UTDetermineVol(pb, short *status, short *more, short *vRefNum,
                      VCBPtr *vcb) */
        {
            uint32_t vcbpp = ARG_L(), vrefp = ARG_L(), morep = ARG_L(), statusp = ARG_L();
            auto *pb = guest<ParamBlockRec>(ARG_L());
            short status = dtmvError, more = 0;
            VCB *v = nullptr;
            err = pb ? determine_vol(pb, &status, &more, &v) : paramErr;
            if(statusp)
                OUT(INTEGER, statusp) = status;
            if(morep)
                OUT(INTEGER, morep) = more;
            if(v)
            {
                if(vrefp)
                    OUT(INTEGER, vrefp) = v->vcbVRefNum;
                if(vcbpp)
                    OUT(VCB *, vcbpp) = v;
            }
            break;
        }
        case 0x1F: /* UTGetBlock(short refNum, log2PhyProc, ulong blockNum, short option,
                      Ptr *buffer) */
        {
            uint32_t bufp = ARG_L();
            INTEGER opt = ARG_W();
            uint32_t blockNum = ARG_L(), log2Phy = ARG_L();
            INTEGER ref = ARG_W();
            VCB *v = vcb_of_refnum(ref);
            uint32_t phys, contiguous;
            if(!v)
                err = rfNumErr;
            else if(v->vcbDrvNum == 0)
                err = volOffLinErr;
            else if((err = log_to_phys(log2Phy, v, ref, blockNum * BLOCK, BLOCK, &phys, &contiguous))
                    == noErr)
            {
                short drive = v->vcbDrvNum;
                CacheBlock *b = cache_find(drive, phys / BLOCK);
                if(!b && (opt & gbExistMask))
                    err = chnotfound;
                else
                {
                    bool fresh = !b;
                    if(!b)
                        b = cache_new(drive, v->vcbDRefNum, phys / BLOCK);
                    if(!b)
                        err = chNoBuf;
                    else
                    {
                        if((fresh && !(opt & gbNoReadMask)) || (!fresh && (opt & gbReadMask) && !b->dirty))
                            err = drive_io(drive, b->dref, b->block * BLOCK, BLOCK, b->buf, false);
                        if(err != noErr && fresh)
                            cache_drop_if([&](const CacheBlock &c) { return &c == b; });
                        else
                        {
                            b->used = ++cache_clock;
                            if(!(opt & gbReleaseMask))
                                ++b->inUse;
                            if(bufp)
                                OUT(Ptr, bufp) = b->buf;
                        }
                    }
                }
            }
            break;
        }
        case 0x20: /* UTReleaseBlock(Ptr buffer, short option) */
        {
            INTEGER opt = ARG_W();
            uint32_t buf = ARG_L();
            CacheBlock *b = cache_by_buffer(buf);
            if(!b || b->inUse == 0)
                err = chNotInUse;
            else
            {
                --b->inUse;
                if(opt & rbDirtyMask)
                    b->dirty = true;
                if(b->dirty || (opt & rbWriteMask))
                    err = cache_write(*b);
                if(opt & rbTrashMask)
                {
                    Ptr p = b->buf;
                    cache_drop_if([&](const CacheBlock &c) { return c.buf == p; });
                }
            }
            break;
        }
        case 0x21: /* UTFlushCache(short refNum, short option) */
        {
            INTEGER opt = ARG_W();
            VCB *v = vcb_of_refnum(ARG_W());
            if(!v)
            {
                err = nsvErr;
                break;
            }
            short drive = v->vcbDrvNum;
            for(auto &b : cache)
                if(b.drive == drive && b.dirty)
                    if(OSErr e = cache_write(b))
                        err = e;
            if(opt & (fcTrashMask | fcFreeMask))
                cache_drop_if([&](const CacheBlock &b) { return b.drive == drive; });
            break;
        }
        case 0x23: /* UTMarkDirty(Ptr buffer) */
        {
            CacheBlock *b = cache_by_buffer(ARG_L());
            if(!b)
                err = chnotfound;
            else
            {
                b->dirty = true;
                if(b->inUse == 0)
                    err = cache_write(*b);
            }
            break;
        }
        case 0x24: /* UTTrashVolBlocks(vcb) */
        case 0x25: /* UTTrashFileBlocks(vcb, ulong fileNum): blocks aren't kept
                      by file here, so the volume's */
        {
            if(sel == 0x25)
                ARG_L();
            VCB *v = guest<VCB>(ARG_L());
            if(v)
                cache_drop_if([&](const CacheBlock &b) { return b.drive == v->vcbDrvNum; });
            break;
        }
        case 0x26: /* UTTrashBlocks(ulong begin, ulong count, vcb, short refNum,
                      short option) */
        {
            ARG_W();
            ARG_W();
            VCB *v = guest<VCB>(ARG_L());
            ARG_L();
            ARG_L();
            if(v)
                cache_drop_if([&](const CacheBlock &b) { return b.drive == v->vcbDrvNum; });
            break;
        }
        case 0x27: /* UTCacheReadIP(log2PhyProc, ulong pos, Ptr buffer, short refNum,
                      ulong count, ulong *actCount, short option) */
        case 0x28: /* UTCacheWriteIP(the same) */
        {
            ARG_W(); /* option */
            uint32_t actp = ARG_L(), count = ARG_L();
            INTEGER ref = ARG_W();
            uint32_t buffer = ARG_L(), pos = ARG_L(), log2Phy = ARG_L();
            uint32_t actual = 0;
            err = cache_io_in_place(log2Phy, pos, guest<char>(buffer), ref, count,
                                    &actual, sel == 0x28);
            if(actp)
                OUT(uint32_t, actp) = actual;
            break;
        }
        case 0x2B: /* UTEjectVol(vcb): off line; the image stays in its drive */
        {
            VCB *v = guest<VCB>(ARG_L());
            if(!v)
                err = paramErr;
            else if(v->vcbDrvNum)
            {
                cache_drop_if([&](const CacheBlock &b) { return b.drive == v->vcbDrvNum; });
                v->vcbDRefNum = v->vcbDrvNum;
                v->vcbDrvNum = 0;
            }
            break;
        }
        case 0x2C: /* UTBlockInFQHashP(short vRefNum, ulong diskBlock): noErr
                      if the cache has it */
        {
            uint32_t block = ARG_L();
            VCB *v = ROMlib_vcbbyvrn(ARG_W());
            err = v && cache_find(v->vcbDrvNum, block) ? noErr : chnotfound;
            break;
        }
        default:
        {
            /* the argument bytes are unknown: the stack can't be put right */
            char what[64];
            snprintf(what, sizeof what, "HFS utility selector $%02X", sel);
            traps::log_unimplemented(what);
            err = paramErr;
            break;
        }
    }
    WRITEUW(EM_A7, (uint16_t)err);
    return ret;
}

/* The File Manager's side ------------------------------------------------ */

bool Executor::ROMlib_fsm_vcb_p(const VCB *vcbp)
{
    return vcbp && fsm_vcbs.count(vcbp);
}

Volume *Executor::ROMlib_volume_of(const VCB *vcbp)
{
    if(!vcbp || ROMlib_fsm_vcb_p(vcbp))
        return nullptr;
    return ((const VCBExtra *)vcbp)->volume;
}

bool Executor::ROMlib_fsm_route(void *vpb, uint16_t trapWord, uint16_t selectCode,
                                FsmTarget target, OSErr *err)
{
    if(fsm_vcbs.empty())
        return false;
    VCB *vcbp = target_vcb(vpb, target);
    if(!ROMlib_fsm_vcb_p(vcbp))
        return false;
    FSDRec *f = fsd_by_fsid(vcbp->vcbFSID);
    if(!f)
        return false;
    auto *pb = (ParmBlkPtr)vpb;
    /* the file system reads ioTrap for the call; an asynchronous one
       completes here (FAKEASYNC) */
    pb->ioParam.ioTrap = trapWord & ~ASYNCBIT;
    *err = call_hfsci(f, vcbp, selectCode & ~ASYNCBIT, (Ptr)pb);
    /* a file system without OpenDF opens the data fork with HOpen, as the
       HOpenDF glue falls back on a Mac */
    if(trapWord == 0xA260 && selectCode == 0x001A && *err == paramErr)
    {
        pb->ioParam.ioTrap = 0xA200;
        *err = call_hfsci(f, vcbp, 0xA200, (Ptr)pb);
    }
    /* GetVolInfo by name or index answers the volume's refnum, as HFS
       does; PC Exchange sets it only for a call that gave one */
    if((trapWord & 0xF9FF) == 0xA007 && *err == noErr
       && pb->volumeParam.ioVolIndex != 0)
        pb->ioParam.ioVRefNum = vcbp->vcbVRefNum;
    /* the desktop database refnums it gives out lead back to it */
    if(trapWord == 0xA260 && *err == noErr)
    {
        short dtRef = ((DTPBRec *)pb)->ioDTRefNum;
        if(selectCode == 0x0020 || selectCode == 0x002E)
            dt_volumes[dtRef] = vcbp->vcbVRefNum;
        else if(selectCode == 0x0021)
            dt_volumes.erase(dtRef);
    }
    return true;
}

bool Executor::ROMlib_fsm_mount(short drive, ParmBlkPtr pb, OSErr *err)
{
    if(fsds.empty() || vcb_on_drive(drive))
        return false;
    DrvQEl *dp = find_drive(drive);
    if(!dp)
        return false;
    Ptr sector = sys_ptr_clear(BLOCK);
    if(!sector)
        return false;
    bool taken = false;
    for(size_t i = 0; i < fsds.size() && !taken; ++i)
    {
        FSDRec *f = fsds[i];
        ensure_loaded(f);
        if(!(f->fsdHFSCI.compInterfMask & fsmComponentEnableMask)
           || !f->fsdHFSCI.compInterfProc || f->fsdHFSCI.idSector == -1)
            continue;
        if(drive_io(drive, dp->dQRefNum, (uint32_t)f->fsdHFSCI.idSector * BLOCK, BLOCK,
                    sector, false)
           != noErr)
            continue;
        OSErr id = call_comm(f, ffsIDDiskMessage, sector);
        if(id != noErr)
        {
            fprintf(stderr, "[Executor] FSM: drive %d: not %.*s's (%d)\n", drive,
                    f->fileSystemName[0], (char *)f->fileSystemName + 1, id);
            continue;
        }
        pb->ioParam.ioVRefNum = drive;
        pb->ioParam.ioTrap = kFSMMountVol;
        *err = call_hfsci(f, nullptr, kFSMMountVol, (Ptr)pb);
        if(*err == noErr)
            dp->dQFSID = f->fileSystemFSID;
        fprintf(stderr, "[Executor] FSM: drive %d mounted by %.*s: %d\n", drive,
                f->fileSystemName[0], (char *)f->fileSystemName + 1, *err);
        taken = true;
    }
    DisposePtr(sector);
    return taken;
}

/* FSMGetIconRec */
struct FSMGetIconRec
{
    GUEST_STRUCT;
    GUEST<INTEGER> refNum;
    GUEST<Ptr> iconBufferPtr;
    GUEST<LONGINT> requestSize;
    GUEST<LONGINT> actualSize;
    GUEST<SignedByte> iconType; /* 1 ICN#, 2 icl4, 3 icl8, 4 ics#, 5 ics4, 6 ics8 */
    GUEST<Boolean> isEjectable;
    GUEST<DrvQEl *> driveQElemPtr;
    GUEST<FSSpecPtr> fileSystemSpecPtr;
    GUEST<LONGINT> reserved1;
};
static_assert(sizeof(FSMGetIconRec) == 28);

Ptr Executor::ROMlib_fsm_drive_icon(short drive)
{
    DrvQEl *dp = find_drive(drive);
    FSDRec *f = dp && dp->dQFSID ? fsd_by_fsid(dp->dQFSID) : nullptr;
    if(!f)
        return nullptr;
    /* the icon and its mask, then the where string (FSMGetIconInfoRec),
       kept per drive as a driver keeps its icon */
    static std::map<short, Ptr> icons;
    Ptr &buf = icons[drive];
    if(!buf)
        buf = sys_ptr_clear(2 * 128 + 256);
    auto *rec = (FSMGetIconRec *)sys_ptr_clear(sizeof(FSMGetIconRec));
    if(!buf || !rec)
        return nullptr;
    rec->refNum = drive;
    rec->iconBufferPtr = buf;
    rec->requestSize = 2 * 128;
    rec->iconType = 1;
    rec->isEjectable = false;
    rec->driveQElemPtr = dp;
    rec->fileSystemSpecPtr = &f->fileSystemSpec;
    OSErr err = call_comm(f, 1 /* ffsGetIconMessage */, (Ptr)rec);
    bool got = err == noErr && rec->actualSize >= 2 * 128;
    DisposePtr((Ptr)rec);
    return got ? buf : nullptr;
}

void Executor::ROMlib_fsm_unrecognised_drive(short drive)
{
    fprintf(stderr, "[Executor] FSM: drive %d: no Mac volume, kept for foreign file systems\n",
            drive);
    raw_drives.insert(drive);
}

bool Executor::ROMlib_fsm_raw_drive_p(short drive)
{
    return raw_drives.count(drive) != 0;
}

void Executor::ROMlib_fsm_unmount_all()
{
    for(VCB *v = (VCB *)LM(VCBQHdr).qHead, *next; v; v = next)
    {
        next = (VCB *)v->qLink;
        if(!ROMlib_fsm_vcb_p(v))
            continue;
        ParmBlkPtr pb = (ParmBlkPtr)sys_ptr_clear(sizeof(ParamBlockRec));
        if(!pb)
            return;
        pb->ioParam.ioVRefNum = v->vcbVRefNum;
        OSErr err = PBUnmountVol(pb);
        if(err != noErr)
        {
            /* busy (files open): flushed at least */
            pb->ioParam.ioVRefNum = v->vcbVRefNum;
            PBFlushVol(pb, false);
        }
        DisposePtr((Ptr)pb);
    }
}

/* PC Exchange 2.0.5 (7.5.5) brings a File System Manager of its own and
   installs it unless the one in FSVars ($BB8) +$12 says version $012F or
   later at +$18; its record is $54 bytes, the length word first. The rest
   is left zero (nothing read it so far). */
void Executor::ROMlib_fsm_publish_vars(Ptr fsvars)
{
    enum { kFSMVarsSize = 0x54, kFSMVersion = 0x012F };
    Ptr vars = sys_ptr_clear(kFSMVarsSize);
    if(!fsvars || !vars)
        return;
    *(GUEST<INTEGER> *)vars = kFSMVarsSize;
    *(GUEST<INTEGER> *)(vars + 0x18) = kFSMVersion;
    *(GUEST<Ptr> *)(fsvars + 0x12) = vars;
}

void Executor::ROMlib_fsm_reset()
{
    fsds.clear();
    fsm_vcbs.clear();
    raw_drives.clear();
    dt_volumes.clear();
    cache.clear();
}
