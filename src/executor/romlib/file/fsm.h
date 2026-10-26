#pragma once

/* MacPhoenix: the File System Manager (docs/executor/FSM.md): foreign file
   systems (PC Exchange's DOS FAT) install with _FSMDispatch ($A0AC), mount
   the drives Executor can't read, and get every File Manager call for
   their volumes through their HFS component; the HFS utility routines
   ($A824) give them Executor's VCBs, FCBs, working directories and a
   block cache.  Records as in Apple's FSM.h. */

#include <FileMgr.h>
#include <file/file.h>

namespace Executor
{
class Volume;

struct HFSCIRec
{
    GUEST_STRUCT;
    GUEST<LONGINT> compInterfMask;
    GUEST<ProcPtr> compInterfProc; /* HFSCIProc: (vcb, selectCode, pb, globals, fsid) */
    GUEST<ProcPtr> log2PhyProc;
    GUEST<Ptr> stackTop;
    GUEST<LONGINT> stackSize;
    GUEST<Ptr> stackPtr;
    GUEST<LONGINT> reserved3;
    GUEST<LONGINT> idSector; /* -1: not a local volume's */
    GUEST<LONGINT> reserved2;
    GUEST<LONGINT> reserved1;
};

struct DICIRec
{
    GUEST_STRUCT;
    GUEST<LONGINT> compInterfMask;
    GUEST<ProcPtr> compInterfProc;
    GUEST<INTEGER> maxVolNameLength;
    GUEST<uint16_t> blockSize;
    GUEST<LONGINT> reserved3;
    GUEST<LONGINT> reserved2;
    GUEST<LONGINT> reserved1;
};

struct FSDRec
{
    GUEST_STRUCT;
    GUEST<FSDRec *> fsdLink;
    GUEST<INTEGER> fsdLength;
    GUEST<INTEGER> fsdVersion;
    GUEST<INTEGER> fileSystemFSID;
    GUEST<Str31> fileSystemName;
    FSSpec fileSystemSpec;
    GUEST<Ptr> fileSystemGlobalsPtr;
    GUEST<ProcPtr> fileSystemCommProc; /* (message, paramBlock, globals) */
    GUEST<LONGINT> reserved3;
    GUEST<LONGINT> reserved2;
    GUEST<LONGINT> reserved1;
    HFSCIRec fsdHFSCI;
    DICIRec fsdDICI;
};

static_assert(sizeof(HFSCIRec) == 40);
static_assert(sizeof(DICIRec) == 24);
static_assert(sizeof(FSDRec) == 196);

/* A VCB a foreign file system made (UTAllocateVCB): a plain $B2-byte VCB
   and the file system's own fields, not Executor's VCBExtra. */
extern bool ROMlib_fsm_vcb_p(const VCB *vcbp);

/* The Volume behind a VCB; nullptr for HFS (hfs*.cpp) and for a foreign
   file system's VCB. */
extern Volume *ROMlib_volume_of(const VCB *vcbp);

/* How a File Manager call names its volume. */
enum class FsmTarget
{
    Path,      /* ioNamePtr / ioVRefNum (vRefNum, WD refnum, drive or default) */
    Ref,       /* ioVRefNum alone (ioNamePtr is the answer's buffer) */
    File,      /* ioRefNum: an open file's FCB */
    VolIndex,  /* ioVolIndex > 0: the nth volume; 0: ioVRefNum; < 0: Path */
    Default,   /* the default volume (GetVol) */
    WD,        /* ioVRefNum: a working directory */
    DTRef,     /* ioDTRefNum of a desktop database a foreign volume opened */
};

/* If the call's volume is a foreign file system's, call its HFS component
   with ioTrap = trapWord and the selectCode (the trap word, or for
   _HFSDispatch the selector), store the result in *err and answer true. */
extern bool ROMlib_fsm_route(void *pb, uint16_t trapWord, uint16_t selectCode,
                             FsmTarget target, OSErr *err);

/* PBMountVol of a drive no volume is on: offered to each foreign file
   system; false if none took it. */
extern bool ROMlib_fsm_mount(short drive, ParmBlkPtr pb, OSErr *err);

/* A drive whose medium Executor's file systems didn't recognise: kept for
   the foreign file systems, its blocks readable through the driver. */
extern void ROMlib_fsm_unrecognised_drive(short drive);
extern bool ROMlib_fsm_raw_drive_p(short drive);

/* A drive's icon (ICN#, then room for the "where" string) from the
   foreign file system whose volume is on it, as the File System Manager
   asks it (ffsGetIconMessage); nullptr if none has the drive. */
extern Ptr ROMlib_fsm_drive_icon(short drive);

/* Every foreign file system's volume unmounted by it (shutdown): what it
   keeps in memory written back. */
extern void ROMlib_fsm_unmount_all();

/* The File System Manager's own variables, reached from FSVars ($BB8)
   +$12, with its version at +$18 (fileMisc.cpp sets FSVars up). */
extern void ROMlib_fsm_publish_vars(Ptr fsvars);

/* Forget every file system (Executor's file system starting again). */
extern void ROMlib_fsm_reset();
}
