#if !defined(__rsys_file__)
#define __rsys_file__

#include <string>
#include <vector>
#include <map>

/*
 * Copyright 1986 - 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

#include <DeviceMgr.h>
#include <rsys/hook.h>
#include <hfs/drive_flags.h>
#include <rsys/filesystem.h>
#include <base/cpu.h>

#include <optional>
#include <string>

/* relative paths of the system folder */

#define SYSMACNAME "\006System"
namespace Executor
{

typedef struct hashlink_str
{
    struct hashlink_str *next;
    LONGINT dirid;
    LONGINT parid;
    char *dirname;
} hashlink_t;


typedef struct
{
    LONGINT fd;
    LONGINT offset;
    LONGINT bsize;
    LONGINT maxbytes;
} hfs_access_t;

/*
 * Evil guest/host mixed structure.
 * The devicename is a host memory pointer.
 * partition is in big-endian guest byte order.
 * #ifdef alpha seems to mean that alpha is the only 64 bit platform in the world,
 * and that this structure should have a fixed size? Why?
 */
typedef struct
{
    GUEST<LONGINT> flags;
    DrvQEl dq;
    Ptr devicename; /* "/usr"	"/dev/rfd0"	whatever */
#if !defined(__alpha)
    char *filler;
#endif
    GUEST<INTEGER> partition; /* for multiply partitioned drives */
    hfs_access_t hfs; /* currently only for floppies -- ick */
} DrvQExtra;

#define OURUFSDREF (-102)

extern GUEST<LONGINT> DefDirID;


/* Internal structure of access path info */

#define fcdirty (1 << 7)
#define fcclump (1 << 6)
#define fcwriteprot (1 << 5)
#define fcsharedwrite (1 << 4)
#define fclockrange (1 << 2)
#define fcfisres (1 << 1)
#define fcwriteperm (1 << 0)

/*
 * And it is an almost-duplicate of filecontrolblock in hfs.h.
 * This version describes the version of the struct used by the "ufs" code,
 * of which there is little left. A common version should be added to FileMgr.h instead.
 */
typedef struct
{
    GUEST_STRUCT;
    GUEST<LONGINT> fdfnum; /* LONGINT fcbFlNum */
    GUEST<Byte> fcflags; /* Byte fcbMdRByt */
    GUEST<Byte> fcbTypByt;
    GUEST<INTEGER> fcbSBlk;
    GUEST<LONGINT> fcleof; /* LONGINT fcbEOF */
    GUEST<LONGINT> fcPLen;
    GUEST<LONGINT> fcbCrPs;
    GUEST<VCB *> fcvptr; /* VCB *fcbVPtr */
    GUEST<Ptr> fcbBfAdr;
    GUEST<INTEGER> fcbFlPos;
    GUEST<LONGINT> fcbClmpSize;
    GUEST<LONGINT> fcbBTCBPtr;
    GUEST<LONGINT> zero[3]; /* these three fields are fcbExtRec */
    GUEST<LONGINT> fcbFType;
    GUEST<ULONGINT> fcbCatPos;
    GUEST<LONGINT> fcparid; /* LONGINT fcbDirID */
    GUEST<Byte[32]> fcname; /* Str31 fcbCName */
} fcbrec;

#define NFCB 348 /* should be related to NOFILE */

typedef struct
{
    GUEST_STRUCT;
    GUEST<INTEGER> nbytes;
    GUEST<fcbrec[NFCB]> fc;
} fcbhidden;

#define ROMlib_fcblocks (((fcbhidden *)LM(FCBSPtr))->fc)

#if defined(NDEBUG)
#define BADRETURNHOOK(err)
#else /* !defined(NDEBUG) */
#define BADRETURNHOOK(err) \
    if(err != noErr)       \
    ROMlib_hook(file_badreturn)
#endif /* !defined(NDEBUG) */

#define CALLCOMPLETION(pb, compp, err)          \
    do                                          \
    {                                           \
        LONGINT saved0, saved1, saved2, saved3, \
            savea0, savea1, savea2, savea3;     \
                                                \
        saved0 = EM_D0;                         \
        saved1 = EM_D1;                         \
        saved2 = EM_D2;                         \
        saved3 = EM_D3;                         \
        savea0 = EM_A0;                         \
        savea1 = EM_A1;                         \
        savea2 = EM_A2;                         \
        savea3 = EM_A3;                         \
        EM_A0 = US_TO_SYN68K(pb);               \
        EM_D0 = err;                            \
        execute68K(US_TO_SYN68K(compp));     \
        EM_D0 = saved0;                         \
        EM_D1 = saved1;                         \
        EM_D2 = saved2;                         \
        EM_D3 = saved3;                         \
        EM_A0 = savea0;                         \
        EM_A1 = savea1;                         \
        EM_A2 = savea2;                         \
        EM_A3 = savea3;                         \
    } while(0)

#define FAKEASYNC(pb, a, err)                                          \
    do                                                                 \
    {                                                                  \
        ((ParmBlkPtr)(pb))->ioParam.ioResult = err;                \
        if(err != noErr)                                               \
            warning_trap_failure("%d", err);                           \
        BADRETURNHOOK(err);                                            \
        if(a)                                                          \
        {                                                              \
            ProcPtr compp;                                             \
                                                                       \
            if((compp = ((ParmBlkPtr)(pb))->ioParam.ioCompletion)) \
            {                                                          \
                CALLCOMPLETION(pb, compp, err);                        \
            }                                                          \
        }                                                              \
        return err;                                                    \
    } while(0)

extern fcbrec *PRNTOFPERR(INTEGER prn, OSErr *errp);

#define ATTRIB_ISLOCKED (1 << 0)
#define ATTRIB_RESOPEN (1 << 2)
#define ATTRIB_DATAOPEN (1 << 3)
#define ATTRIB_ISADIR (1 << 4)
#define ATTRIB_ISOPEN (1 << 7)

class Volume;
void MountLocalVolumes();
/* MacPhoenix: host folders to mount instead of "/" (empty: mount "/"). */
extern std::vector<std::string> ROMlib_local_volume_roots;
/* MacPhoenix: path settings by variable name (SystemFolder, MacVolumes,
 * ...), consulted before the environment. */
extern std::map<std::string, std::string> ROMlib_path_overrides;
/* MacPhoenix: host path of the System Folder Executor booted from. */
extern std::string ROMlib_system_folder_path();
std::optional<FSSpec> nativePathToFSSpec(const fs::path& p);
std::optional<FSSpec> cmdlinePathToFSSpec(const std::string& p);

void ROMlib_fileinit(void);
void InitSystemFolder(std::string systemFolder);
void InitPaths();

typedef struct
{
    VCB vcb;
    union {
        hfs_access_t hfs;
    } u;
    Volume *volume;
} VCBExtra;

enum
{
    bHasBlankAccessPrivileges = 4,
    bHasBtreeMgr,
    bHasFileIDs,
    bHasCatSearch,
    bHasUserGroupList,
    bHasPersonalAccessPrivileges,
    bHasFolderLock,
    bHasShortName,
    bHasDesktopMgr,
    bHasMoveRename,
    bHasCopyFile,
    bHasOpenDeny,
    bHasExtFSVol,
    bNoSysDir,
    bAccessCntl,
    bNoBootBlks,
    bNoDeskItems,
    bNoSwitchTo = 25,
    bTrshOffLine,
    bNoLclSync,
    bNoVNEdit,
    bNoMiniFndr,
    bLocalWList,
    bLimitFCBs,
};

typedef struct
{
    GUEST_STRUCT;
    GUEST<INTEGER> vMVersion;
    GUEST<ULONGINT> vMAttrib;
    GUEST<LONGINT> vMLocalHand;
    GUEST<LONGINT> vMServerAdr;
    GUEST<LONGINT> vMVolumeGrade;
    GUEST<INTEGER> vMForeignPrivID;
} getvolparams_info_t;

#define HARDLOCKED (1 << 7)
#define SOFTLOCKED (1 << 15)
#define volumenotlocked(vp) (((VCB *)vp)->vcbAtrb & SOFTLOCKED ? vLckdErr : (((VCB *)vp)->vcbAtrb & HARDLOCKED ? wPrErr : noErr))

#if !defined(L_INCR)
#define L_INCR 1
#endif /* L_INCR */

#if !defined(L_SET)
#define L_SET 0
#endif /* L_SET */

#if !defined(L_XTND)
#define L_XTND 2
#endif /* L_XTND */

/*
 * TODO: Below we check for non-zero
 *	 ioFDirIndex.  It should probably be a check for positive.  We
 *	 should also check into what we do when the first byte is zero.
 */

#define UPDATE_IONAMEPTR_P(pb) \
    ((pb).ioNamePtr && ((pb).ioFDirIndex != 0 || !(pb).ioNamePtr[0]))

extern StringPtr ROMlib_exefname;


extern OSErr ROMlib_maperrno(void);

extern OSErr ROMlib_driveropen(ParmBlkPtr pbp, Boolean a);
extern OSErr ROMlib_dispatch(ParmBlkPtr p, Boolean async,
                             DriverRoutineType routine, INTEGER trap);

extern DrvQExtra *ROMlib_addtodq(ULONGINT drvsize, const char *devicename,
                                 INTEGER partition, INTEGER drefnum,
                                 drive_flags_t flags, hfs_access_t *hfsp);

extern Byte open_attrib_bits(LONGINT file_id, VCB *vcbp, GUEST<INTEGER> *refnump);


#if !defined(NDEBUG)

extern void fs_err_hook(OSErr);

#else

#define fs_err_hook(x)

#endif

extern void HCreateResFile_helper(INTEGER vrefnum, LONGINT parid, ConstStringPtr name,
                                  OSType creator, OSType type,
                                  ScriptCode script);

extern OSErr FSReadAll(INTEGER rn, GUEST<LONGINT> *count, Ptr buffp);
extern OSErr FSWriteAll(INTEGER rn, GUEST<LONGINT> *count, Ptr buffp);

extern LONGINT ROMlib_magic_offset;

#if !defined(ST_INO)
#define ST_INO(buf) ((uint32_t)((buf).st_ino))
#endif

extern INTEGER ROMlib_nextvrn;

std::string expandPath(std::string);

static_assert(sizeof(hfs_access_t) == 16);
static_assert(sizeof(fcbrec) == 94);
static_assert(sizeof(fcbhidden) == 32714);
static_assert(sizeof(getvolparams_info_t) == 20);
}
#endif /* !defined(__rsys_file__) */
