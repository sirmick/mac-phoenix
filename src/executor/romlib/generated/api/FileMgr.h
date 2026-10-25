/*****************************************************************************
 *                                                                           *
 *                                 FileMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSType, LONGINT, Rect, INTEGER, SignedByte, Ptr,        *
 *                   ULONGINT, StringPtr, QElemPtr, Byte, Str63, OSErr,      *
 *                   ConstStringPtr, QHdrPtr, Boolean, ScriptCode            *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME FileMgr
#include <base/api-module.h>


namespace Executor {

BEGIN_EXECUTOR_ONLY
//So as to not conflict with OS X defines
#undef PBGetVInfo
#undef PBXGetVolInfo
#undef PBGetVol
#undef PBSetVol
#undef PBFlushVol
#undef PBCreate
#undef PBDelete
#undef PBOpenDF
#undef PBOpenRF
#undef PBRename
#undef PBGetFInfo
#undef PBSetFInf
#undef PBSetFLock
#undef PBRstFLock
#undef PBSetFVers
#undef PBAllocate
#undef PBGetEOF
#undef PBSetEOF
#undef PBGetFPos
#undef PBSetFPos
#undef PBFlushFile
#undef PBCatSearch
#undef PBMakeFSSpec
#undef PBOpenWD
#undef PBCloseWD
#undef PBHSetVol
#undef PBHGetVol
#undef PBCatMove
#undef PBDirCreate
#undef PBGetWDInfo
#undef PBGetFCBInfo
#undef PBGetCatInfo
#undef PBSetCatInfo
#undef PBAllocContig
#undef PBLockRange
#undef PBUnlockRange
#undef PBSetVInfo
#undef PBHGetVInfo
#undef PBHOpen
#undef PBHOpenRF
#undef PBHOpenDF
#undef PBHCreate
#undef PBHDelete
#undef PBHRename
#undef PBHRstFLock
#undef PBHSetFLock
#undef PBHGetFInfo
#undef PBHSetFInfo
#undef PBMakeFSSpec
#undef PBHGetVolParms
#undef PBHGetLogInInfo
#undef PBHGetDirAccess
#undef PBHSetDirAccess
#undef PBHMapID
#undef PBHMapName
#undef PBHCopyFile
#undef PBHMoveRename
#undef PBHOpenDeny
#undef PBHOpenRFDeny
#undef PBExchangeFiles
#undef PBCreateFileIDRef
#undef PBResolveFileIDRef
#undef PBDeleteFileIDRef
#undef PBGetForeignPrivs
#undef PBSetForeignPrivs
#undef PBDTAddIcon
#undef PBDTGetIcon
#undef PBDTGetIconInfo
#undef PBDTAddAPPL
#undef PBDTRemoveAPPL
#undef PBDTGetAPPL
#undef PBDTSetComment
#undef PBDTRemoveComment
#undef PBDTGetComment
#undef PBDTFlush
#undef PBDTReset
#undef PBDTGetInfo
#undef PBDTDelete
#undef PBSetFInfo
#undef PBGetFInfo
END_EXECUTOR_ONLY


enum {fOnDesk = 1,
fHasBundle = 8192,
fInvisible = 16384,
fTrash = -3,
fDesktop = -2,
fDisk = 0,
};

enum {fsCurPerm = 0,
fsRdPerm = 1,
fsWrPerm = 2,
fsRdWrPerm = 3,
fsRdWrShPerm = 4,
fsRdDenyPerm = 16,
fsWrDenyPerm = 32,
};

enum {fsAtMark = 0,
fsFromStart = 1,
fsFromLEOF = 2,
fsFromMark = 3,
rdVerify = 64,
};

enum {badMDBErr = -60,
badMovErr = -122,
bdNamErr = -37,
dirFulErr = -33,
dskFulErr = -34,
dupFNErr = -48,
eofErr = -39,
extFSErr = -58,
fBsyErr = -47,
fLckdErr = -45,
fnfErr = -43,
fnOpnErr = -38,
fsRnErr = -59,
gfpErr = -52,
ioErr = -36,
noMacDskErr = -57,
nsDrvErr = -56,
nsvErr = -35,
opWrErr = -49,
permErr = -54,
posErr = -40,
rfNumErr = -51,
tmfoErr = -42,
volOffLinErr = -53,
volOnLinErr = -55,
vLckdErr = -46,
wrgVolTypErr = -123,
wrPermErr = -61,
wPrErr = -44,
tmwdoErr = -121,
dirNFErr = -120,
fsDSIntErr = -127,
};

enum {wrgVolTypeErr = -123,
diffVolErr = -1303,
};

struct FInfo{GUEST_STRUCT;GUEST<OSType> fdType;GUEST<OSType> fdCreator;GUEST<uint16_t> fdFlags;GUEST<Point> fdLocation;GUEST<uint16_t> fdFldr;};static_assert(sizeof(FInfo) == 16);void describeStruct(const FInfo&, std::ostream&);

struct FXInfo{GUEST_STRUCT;GUEST<uint16_t> fdIconID;GUEST<uint16_t> fdUnused[4];GUEST<uint16_t> fdComment;GUEST<LONGINT> fdPutAway;};static_assert(sizeof(FXInfo) == 16);void describeStruct(const FXInfo&, std::ostream&);

struct DInfo{GUEST_STRUCT;Rect frRect;GUEST<uint16_t> frFlags;GUEST<Point> frLocation;GUEST<uint16_t> frView;};static_assert(sizeof(DInfo) == 16);void describeStruct(const DInfo&, std::ostream&);

struct DXInfo{GUEST_STRUCT;GUEST<Point> frScroll;GUEST<LONGINT> frOpenChain;GUEST<uint16_t> frUnused;GUEST<uint16_t> frComment;GUEST<LONGINT> frPutAway;};static_assert(sizeof(DXInfo) == 16);void describeStruct(const DXInfo&, std::ostream&);

typedef enum {ioParamType,
fileParamType,
volumeParamType,
cntrlParamType,
}ParamBlkType;

BEGIN_EXECUTOR_ONLY
#define COMMONFSQUEUEDEFS        \
    GUEST<QElemPtr> qLink;       \
    GUEST<INTEGER> qType;        \
    GUEST<INTEGER> ioTrap;       \
    GUEST<Ptr> ioCmdAddr;        \
    GUEST<ProcPtr> ioCompletion; \
    GUEST<OSErr> ioResult;       \
    GUEST<StringPtr> ioNamePtr;  \
    GUEST<INTEGER> ioVRefNum
END_EXECUTOR_ONLY


// Executor needs ioMisc to be uint32_t instead of the normal
// Ptr, so this is defined as a separate typedef here.
typedef uint32_t IOMiscType;

struct IOParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<INTEGER> ioRefNum;SignedByte ioVersNum;SignedByte ioPermssn;GUEST<IOMiscType> ioMisc; // should be largest of Ptr, LONGINT
GUEST<Ptr> ioBuffer;GUEST<LONGINT> ioReqCount;GUEST<LONGINT> ioActCount;GUEST<INTEGER> ioPosMode;GUEST<LONGINT> ioPosOffset;};static_assert(sizeof(IOParam) == 50);void describeStruct(const IOParam&, std::ostream&);

struct FileParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<INTEGER> ioFRefNum;SignedByte ioFVersNum;SignedByte filler1;GUEST<INTEGER> ioFDirIndex;SignedByte ioFlAttrib;SignedByte ioFlVersNum;FInfo ioFlFndrInfo;GUEST<LONGINT> ioFlNum;GUEST<INTEGER> ioFlStBlk;GUEST<LONGINT> ioFlLgLen;GUEST<LONGINT> ioFlPyLen;GUEST<INTEGER> ioFlRStBlk;GUEST<LONGINT> ioFlRLgLen;GUEST<LONGINT> ioFlRPyLen;GUEST<ULONGINT> ioFlCrDat;GUEST<ULONGINT> ioFlMdDat;};static_assert(sizeof(FileParam) == 80);void describeStruct(const FileParam&, std::ostream&);

struct VolumeParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<LONGINT> filler2;GUEST<INTEGER> ioVolIndex;GUEST<ULONGINT> ioVCrDate;GUEST<ULONGINT> ioVLsBkUp;GUEST<uint16_t> ioVAtrb;GUEST<uint16_t> ioVNmFls;GUEST<uint16_t> ioVDirSt;GUEST<uint16_t> ioVBlLn;GUEST<uint16_t> ioVNmAlBlks;GUEST<LONGINT> ioVAlBlkSiz;GUEST<LONGINT> ioVClpSiz;GUEST<uint16_t> ioAlBlSt;GUEST<LONGINT> ioVNxtFNum;GUEST<uint16_t> ioVFrBlk;};static_assert(sizeof(VolumeParam) == 64);void describeStruct(const VolumeParam&, std::ostream&);

struct CntrlParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<INTEGER> ioCRefNum;GUEST<INTEGER> csCode;GUEST<INTEGER> csParam[11];};static_assert(sizeof(CntrlParam) == 50);void describeStruct(const CntrlParam&, std::ostream&);

union ParamBlockRec{GUEST_STRUCT;IOParam ioParam;FileParam fileParam;VolumeParam volumeParam;CntrlParam cntrlParam;};static_assert(sizeof(ParamBlockRec) == 80);void describeStruct(const ParamBlockRec&, std::ostream&);

typedef ParamBlockRec *ParmBlkPtr;

struct HIoParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<INTEGER> ioRefNum;SignedByte ioVersNum;SignedByte ioPermssn;GUEST<IOMiscType> ioMisc; // should be largest of Ptr, LONGINT
GUEST<Ptr> ioBuffer;GUEST<LONGINT> ioReqCount;GUEST<LONGINT> ioActCount;GUEST<INTEGER> ioPosMode;GUEST<LONGINT> ioPosOffset;};static_assert(sizeof(HIoParam) == 50);void describeStruct(const HIoParam&, std::ostream&);

struct HFileParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<INTEGER> ioFRefNum;SignedByte ioFVersNum;SignedByte filler1;GUEST<INTEGER> ioFDirIndex;SignedByte ioFlAttrib;SignedByte ioFlVersNum;FInfo ioFlFndrInfo;GUEST<LONGINT> ioDirID; // -->
GUEST<INTEGER> ioFlStBlk;GUEST<LONGINT> ioFlLgLen;GUEST<LONGINT> ioFlPyLen;GUEST<INTEGER> ioFlRStBlk;GUEST<LONGINT> ioFlRLgLen;GUEST<LONGINT> ioFlRPyLen;GUEST<ULONGINT> ioFlCrDat;GUEST<ULONGINT> ioFlMdDat;};static_assert(sizeof(HFileParam) == 80);void describeStruct(const HFileParam&, std::ostream&);

struct HVolumeParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<LONGINT> pfiller2;GUEST<INTEGER> ioVolIndex;GUEST<ULONGINT> ioVCrDate;GUEST<ULONGINT> ioVLsMod; // -->
GUEST<INTEGER> ioVAtrb;GUEST<uint16_t> ioVNmFls;GUEST<uint16_t> ioVBitMap; // -->
GUEST<uint16_t> ioVAllocPtr; // -->
GUEST<uint16_t> ioVNmAlBlks;GUEST<LONGINT> ioVAlBlkSiz;GUEST<LONGINT> ioVClpSiz;GUEST<uint16_t> ioAlBlSt;GUEST<LONGINT> ioVNxtCNID; // -->
GUEST<uint16_t> ioVFrBlk;GUEST<uint16_t> ioVSigWord; // -->
GUEST<INTEGER> ioVDrvInfo; // -->
GUEST<INTEGER> ioVDRefNum; // -->
GUEST<INTEGER> ioVFSID; // -->
GUEST<LONGINT> ioVBkUp; // -->
GUEST<uint16_t> ioVSeqNum; // -->
GUEST<LONGINT> ioVWrCnt; // -->
GUEST<LONGINT> ioVFilCnt; // -->
GUEST<LONGINT> ioVDirCnt; // -->
GUEST<LONGINT> ioVFndrInfo[8]; // -->
};static_assert(sizeof(HVolumeParam) == 122);void describeStruct(const HVolumeParam&, std::ostream&);

union HParamBlockRec{GUEST_STRUCT;HIoParam ioParam;HFileParam fileParam;HVolumeParam volumeParam;};static_assert(sizeof(HParamBlockRec) == 122);void describeStruct(const HParamBlockRec&, std::ostream&);

typedef HParamBlockRec *HParmBlkPtr;

typedef enum {hfileInfo,
dirInfo,
}CInfoType;

BEGIN_EXECUTOR_ONLY
#define COMMONCINFODEFS           \
    COMMONFSQUEUEDEFS;            \
    GUEST<INTEGER> ioFRefNum;     \
    GUEST<SignedByte> ioFVersNum; \
    GUEST<SignedByte> filler1;    \
    GUEST<INTEGER> ioFDirIndex;   \
    GUEST<SignedByte> ioFlAttrib; \
    GUEST<SignedByte> ioACUser
END_EXECUTOR_ONLY


struct HFileInfo{GUEST_STRUCT;COMMONCINFODEFS;FInfo ioFlFndrInfo;GUEST<LONGINT> ioDirID;GUEST<INTEGER> ioFlStBlk;GUEST<LONGINT> ioFlLgLen;GUEST<LONGINT> ioFlPyLen;GUEST<INTEGER> ioFlRStBlk;GUEST<LONGINT> ioFlRLgLen;GUEST<LONGINT> ioFlRPyLen;GUEST<ULONGINT> ioFlCrDat;GUEST<ULONGINT> ioFlMdDat;GUEST<ULONGINT> ioFlBkDat;FXInfo ioFlXFndrInfo;GUEST<LONGINT> ioFlParID;GUEST<LONGINT> ioFlClpSiz;};static_assert(sizeof(HFileInfo) == 108);void describeStruct(const HFileInfo&, std::ostream&);

struct DirInfo{GUEST_STRUCT;COMMONCINFODEFS;DInfo ioDrUsrWds;GUEST<LONGINT> ioDrDirID;GUEST<uint16_t> ioDrNmFls;GUEST<uint16_t> filler3[9];GUEST<ULONGINT> ioDrCrDat;GUEST<ULONGINT> ioDrMdDat;GUEST<ULONGINT> ioDrBkDat;DXInfo ioDrFndrInfo;GUEST<LONGINT> ioDrParID;};static_assert(sizeof(DirInfo) == 104);void describeStruct(const DirInfo&, std::ostream&);

union CInfoPBRec{GUEST_STRUCT;HFileInfo hFileInfo;DirInfo dirInfo;};static_assert(sizeof(CInfoPBRec) == 108);void describeStruct(const CInfoPBRec&, std::ostream&);

typedef CInfoPBRec *CInfoPBPtr;

struct CMovePBRec{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<LONGINT> filler1;GUEST<StringPtr> ioNewName;GUEST<LONGINT> filler2;GUEST<LONGINT> ioNewDirID;GUEST<LONGINT> filler3[2];GUEST<LONGINT> ioDirID;};static_assert(sizeof(CMovePBRec) == 52);void describeStruct(const CMovePBRec&, std::ostream&);

typedef CMovePBRec *CMovePBPtr;

struct WDPBRec{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<uint16_t> filler1;GUEST<INTEGER> ioWDIndex;GUEST<LONGINT> ioWDProcID;GUEST<INTEGER> ioWDVRefNum;GUEST<INTEGER> filler2[7];GUEST<LONGINT> ioWDDirID;};static_assert(sizeof(WDPBRec) == 52);void describeStruct(const WDPBRec&, std::ostream&);

typedef WDPBRec *WDPBPtr;

struct FCBPBRec{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<INTEGER> ioRefNum;GUEST<uint16_t> filler;GUEST<INTEGER> ioFCBIndx;GUEST<INTEGER> ioFCBobnoxiousfiller; // ACK! not in IMIV, but ThinkC+MPW
GUEST<LONGINT> ioFCBFlNm;GUEST<uint16_t> ioFCBFlags;GUEST<INTEGER> ioFCBStBlk;GUEST<LONGINT> ioFCBEOF;GUEST<LONGINT> ioFCBPLen;GUEST<LONGINT> ioFCBCrPs;GUEST<INTEGER> ioFCBVRefNum;GUEST<LONGINT> ioFCBClpSiz;GUEST<LONGINT> ioFCBParID;};static_assert(sizeof(FCBPBRec) == 62);void describeStruct(const FCBPBRec&, std::ostream&);

typedef FCBPBRec *FCBPBPtr;

struct VCB{GUEST_STRUCT;GUEST<QElemPtr> qLink; // 0
GUEST<INTEGER> qType; // 4
GUEST<uint16_t> vcbFlags; // 6
GUEST<uint16_t> vcbSigWord; // 8
GUEST<ULONGINT> vcbCrDate; // 10
GUEST<ULONGINT> vcbLsMod; // 14
GUEST<uint16_t> vcbAtrb; // 18
GUEST<uint16_t> vcbNmFls; // 20
GUEST<uint16_t> vcbVBMSt; // 22
GUEST<uint16_t> vcbAllocPtr; // 24
GUEST<uint16_t> vcbNmAlBlks; // 26
GUEST<LONGINT> vcbAlBlkSiz; // 28
GUEST<LONGINT> vcbClpSiz; // 32
GUEST<uint16_t> vcbAlBlSt; // 36
GUEST<LONGINT> vcbNxtCNID; // 38
GUEST<uint16_t> vcbFreeBks; // 42
Byte vcbVN[28]; // 44
GUEST<INTEGER> vcbDrvNum; // 72
GUEST<INTEGER> vcbDRefNum; // 74
GUEST<INTEGER> vcbFSID; // 76
GUEST<INTEGER> vcbVRefNum; // 78
GUEST<Ptr> vcbMAdr;GUEST<Ptr> vcbBufAdr;GUEST<uint16_t> vcbMLen;GUEST<INTEGER> vcbDirIndex;GUEST<uint16_t> vcbDirBlk;GUEST<LONGINT> vcbVolBkUp;GUEST<uint16_t> vcbVSeqNum;GUEST<LONGINT> vcbWrCnt;GUEST<LONGINT> vcbXTClpSiz;GUEST<LONGINT> vcbCTClpSiz;GUEST<uint16_t> vcbNmRtDirs;GUEST<LONGINT> vcbFilCnt;GUEST<LONGINT> vcbDirCnt;GUEST<LONGINT> vcbFndrInfo[8];GUEST<uint16_t> vcbVCSize;GUEST<uint16_t> vcbVBMCSiz;GUEST<uint16_t> vcbCtlCSiz;GUEST<uint16_t> vcbXTAlBlks;GUEST<uint16_t> vcbCTAlBlks;GUEST<INTEGER> vcbXTRef;GUEST<INTEGER> vcbCTRef;GUEST<Ptr> vcbCtlBuf;GUEST<LONGINT> vcbDirIDM;GUEST<uint16_t> vcbOffsM;};static_assert(sizeof(VCB) == 178);void describeStruct(const VCB&, std::ostream&);

typedef VCB *VCBPtr;

struct DrvQEl{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> dQDrive;GUEST<INTEGER> dQRefNum;GUEST<INTEGER> dQFSID;GUEST<uint16_t> dQDrvSz;GUEST<uint16_t> dQDrvSz2;};static_assert(sizeof(DrvQEl) == 16);void describeStruct(const DrvQEl&, std::ostream&);

// data types introduced by the new high level file system dispatch
//    traps
struct FSSpec{GUEST_STRUCT;GUEST<INTEGER> vRefNum;GUEST<LONGINT> parID;GUEST<Str63> name;};void describeStruct(const FSSpec&, std::ostream&);

struct FSSpec;static_assert(sizeof(FSSpec) == 70);

typedef FSSpec *FSSpecPtr;

typedef FSSpecPtr FSSpecArrayPtr;

// FileMgr IMIV-212 (true);
const LowMemGlobal<INTEGER > BootDrive{ 0x210 };
inline INTEGER  LMGetBootDrive() { return LM(BootDrive); }
inline void LMSetBootDrive(INTEGER val) { LM(BootDrive) = val; }
NOTRAP_FUNCTION2(LMGetBootDrive);
NOTRAP_FUNCTION2(LMSetBootDrive);


// MacPhoenix: the File System Manager's variables (System heap, $74
// bytes on 7.5.5, length word first). Apple Menu Options' _Open patch
// reads fields 10 and 56 of it.
const LowMemGlobal<Ptr > FSVars{ 0xBB8 };
inline Ptr  LMGetFSVars() { return LM(FSVars); }
inline void LMSetFSVars(Ptr val) { LM(FSVars) = val; }
NOTRAP_FUNCTION2(LMGetFSVars);
NOTRAP_FUNCTION2(LMSetFSVars);


// FileMgr IMIV-182 (true);
const LowMemGlobal<QHdr > DrvQHdr{ 0x308 };
inline void LMGetDrvQHdr(QHdr *val) { *val = LM(DrvQHdr); }
inline void LMSetDrvQHdr(const QHdr *val) { LM(DrvQHdr) = *val; }
NOTRAP_FUNCTION2(LMGetDrvQHdr);
NOTRAP_FUNCTION2(LMSetDrvQHdr);


// FileMgr ThinkC (false);
const LowMemGlobal<ProcPtr > EjectNotify{ 0x338 };
inline ProcPtr  LMGetEjectNotify() { return LM(EjectNotify); }
inline void LMSetEjectNotify(ProcPtr val) { LM(EjectNotify) = val; }
NOTRAP_FUNCTION2(LMGetEjectNotify);
NOTRAP_FUNCTION2(LMSetEjectNotify);


// FileMgr IMIV-179 (true);
const LowMemGlobal<Ptr > FCBSPtr{ 0x34E };
inline Ptr  LMGetFCBSPtr() { return LM(FCBSPtr); }
inline void LMSetFCBSPtr(Ptr val) { LM(FCBSPtr) = val; }
NOTRAP_FUNCTION2(LMGetFCBSPtr);
NOTRAP_FUNCTION2(LMSetFCBSPtr);


// FileMgr IMIV-178 (true);
const LowMemGlobal<VCBPtr > DefVCBPtr{ 0x352 };
inline VCBPtr  LMGetDefVCBPtr() { return LM(DefVCBPtr); }
inline void LMSetDefVCBPtr(VCBPtr val) { LM(DefVCBPtr) = val; }
NOTRAP_FUNCTION2(LMGetDefVCBPtr);
NOTRAP_FUNCTION2(LMSetDefVCBPtr);


// FileMgr IMIV-178 (true);
const LowMemGlobal<QHdr > VCBQHdr{ 0x356 };
inline void LMGetVCBQHdr(QHdr *val) { *val = LM(VCBQHdr); }
inline void LMSetVCBQHdr(const QHdr *val) { LM(VCBQHdr) = *val; }
NOTRAP_FUNCTION2(LMGetVCBQHdr);
NOTRAP_FUNCTION2(LMSetVCBQHdr);


// FileMgr IMIV-176 (true);
const LowMemGlobal<QHdr > FSQHdr{ 0x360 };
inline void LMGetFSQHdr(QHdr *val) { *val = LM(FSQHdr); }
inline void LMSetFSQHdr(const QHdr *val) { LM(FSQHdr) = *val; }
NOTRAP_FUNCTION2(LMGetFSQHdr);
NOTRAP_FUNCTION2(LMSetFSQHdr);


// FileMgr idunno (true);
const LowMemGlobal<Ptr > WDCBsPtr{ 0x372 };
inline Ptr  LMGetWDCBsPtr() { return LM(WDCBsPtr); }
inline void LMSetWDCBsPtr(Ptr val) { LM(WDCBsPtr) = val; }
NOTRAP_FUNCTION2(LMGetWDCBsPtr);
NOTRAP_FUNCTION2(LMSetWDCBsPtr);


// FileMgr MPW (true);
const LowMemGlobal<INTEGER > DefVRefNum{ 0x384 };
inline INTEGER  LMGetDefVRefNum() { return LM(DefVRefNum); }
inline void LMSetDefVRefNum(INTEGER val) { LM(DefVRefNum) = val; }
NOTRAP_FUNCTION2(LMGetDefVRefNum);
NOTRAP_FUNCTION2(LMSetDefVRefNum);


// * Note: MacLinkPC+ loads 0x358 into a register (i.e. the address of the
//  * pointer to the first element on the VCB queue) and then uses
//  * 72 off of it (0x3A0) and 78 off of it (0x3A6). As long as
//  * there are zeros there, that doesn't hurt us, but normally,
//  * we'd have negative ones in there. Hence we describe them
//  * here and set them to zero in executor.
// const LowMemGlobal<Ptr> FmtDefaults { 0x39E }; // FileMgr ThinkC (false);
// FileMgr IMIV-212 (false);
const LowMemGlobal<Ptr > ToExtFS{ 0x3F2 };
inline Ptr  LMGetToExtFS() { return LM(ToExtFS); }
inline void LMSetToExtFS(Ptr val) { LM(ToExtFS) = val; }
NOTRAP_FUNCTION2(LMGetToExtFS);
NOTRAP_FUNCTION2(LMSetToExtFS);


// FileMgr IMIV-97 (true);
const LowMemGlobal<INTEGER > FSFCBLen{ 0x3F6 };
inline INTEGER  LMGetFSFCBLen() { return LM(FSFCBLen); }
inline void LMSetFSFCBLen(INTEGER val) { LM(FSFCBLen) = val; }
NOTRAP_FUNCTION2(LMGetFSFCBLen);
NOTRAP_FUNCTION2(LMSetFSFCBLen);


OSErr FSOpen(ConstStringPtr filen, INTEGER vrn, GUEST<INTEGER> *rn);NOTRAP_FUNCTION2(FSOpen);


OSErr OpenRF(ConstStringPtr filen, INTEGER vrn, GUEST<INTEGER> *rn);NOTRAP_FUNCTION2(OpenRF);


OSErr OpenDF(ConstStringPtr filen, INTEGER vrn, GUEST<INTEGER> *rn);NOTRAP_FUNCTION2(OpenDF);


OSErr FSRead(INTEGER rn, GUEST<LONGINT> *count, void *buffp);NOTRAP_FUNCTION2(FSRead);


OSErr FSWrite(INTEGER rn, GUEST<LONGINT> *count, const void *buffp);NOTRAP_FUNCTION2(FSWrite);


OSErr GetFPos(INTEGER rn, GUEST<LONGINT> *filep);NOTRAP_FUNCTION2(GetFPos);


OSErr SetFPos(INTEGER rn, INTEGER posmode, LONGINT possoff);NOTRAP_FUNCTION2(SetFPos);


OSErr GetEOF(INTEGER rn, GUEST<LONGINT> *eof);NOTRAP_FUNCTION2(GetEOF);


OSErr SetEOF(INTEGER rn, LONGINT eof);NOTRAP_FUNCTION2(SetEOF);


OSErr Allocate(INTEGER rn, GUEST<LONGINT> *count);NOTRAP_FUNCTION2(Allocate);


OSErr AllocContig(INTEGER rn, GUEST<LONGINT> *count);NOTRAP_FUNCTION2(AllocContig);


OSErr FSClose(INTEGER rn);NOTRAP_FUNCTION2(FSClose);


OSErr Create(ConstStringPtr filen, INTEGER vrn, OSType creator, OSType filtyp);NOTRAP_FUNCTION2(Create);


OSErr FSDelete(ConstStringPtr filen, INTEGER vrn);NOTRAP_FUNCTION2(FSDelete);


OSErr GetFInfo(ConstStringPtr filen, INTEGER vrn, FInfo *fndrinfo);NOTRAP_FUNCTION2(GetFInfo);


OSErr HGetFInfo(INTEGER vref, LONGINT dirid, ConstStringPtr name, FInfo *fndrinfo);NOTRAP_FUNCTION2(HGetFInfo);


OSErr SetFInfo(ConstStringPtr filen, INTEGER vrn, FInfo *fndrinfo);NOTRAP_FUNCTION2(SetFInfo);


OSErr SetFLock(ConstStringPtr filen, INTEGER vrn);NOTRAP_FUNCTION2(SetFLock);


OSErr RstFLock(ConstStringPtr filen, INTEGER vrn);NOTRAP_FUNCTION2(RstFLock);


OSErr Rename(ConstStringPtr filen, INTEGER vrn, ConstStringPtr newf);NOTRAP_FUNCTION2(Rename);


void C_FInitQueue();PASCAL_TRAP(FInitQueue, 0xA016);


QHdrPtr C_GetFSQHdr();NOTRAP_FUNCTION(GetFSQHdr);


QHdrPtr C_GetVCBQHdr();NOTRAP_FUNCTION(GetVCBQHdr);


QHdrPtr C_GetDrvQHdr();NOTRAP_FUNCTION(GetDrvQHdr);


OSErr GetVInfo(INTEGER drv, StringPtr voln, GUEST<INTEGER> *vrn, GUEST<LONGINT> *freeb);

OSErr GetVRefNum(INTEGER prn, GUEST<INTEGER> *vrn);

OSErr GetVol(StringPtr voln, GUEST<INTEGER> *vrn);NOTRAP_FUNCTION2(GetVol);


OSErr SetVol(ConstStringPtr voln, INTEGER vrn);NOTRAP_FUNCTION2(SetVol);


OSErr FlushVol(ConstStringPtr voln, INTEGER vrn);NOTRAP_FUNCTION2(FlushVol);


OSErr UnmountVol(ConstStringPtr voln, INTEGER vrn);NOTRAP_FUNCTION2(UnmountVol);


OSErr Eject(ConstStringPtr voln, INTEGER vrn);NOTRAP_FUNCTION2(Eject);


OSErr PBAllocContig(ParmBlkPtr pb, Boolean async);

// TODO: Trap?
OSErr PBOpen(ParmBlkPtr pb, Boolean async);

OSErr PBHOpen(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBOpen, PBHOpen, HParmBlkPtr, 0xA000);

OSErr PBClose(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBClose, ParmBlkPtr, 0xA001);

OSErr PBRead(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBRead, ParmBlkPtr, 0xA002);

OSErr PBWrite(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBWrite, ParmBlkPtr, 0xA003);

OSErr PBGetVInfo(ParmBlkPtr pb, Boolean async);

OSErr PBHGetVInfo(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBGetVInfo, PBHGetVInfo, HParmBlkPtr, 0xA007);

OSErr PBCreate(ParmBlkPtr pb, Boolean async);

OSErr PBHCreate(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBCreate, PBHCreate, HParmBlkPtr, 0xA008);

OSErr PBDelete(ParmBlkPtr pb, Boolean async);

OSErr PBHDelete(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBDelete, PBHDelete, HParmBlkPtr, 0xA009);

OSErr PBOpenRF(ParmBlkPtr pb, Boolean async);

OSErr PBHOpenRF(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBOpenRF, PBHOpenRF, HParmBlkPtr, 0xA00A);

OSErr PBRename(ParmBlkPtr pb, Boolean async);

OSErr PBHRename(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBRename, PBHRename, HParmBlkPtr, 0xA00B);

OSErr PBGetFInfo(ParmBlkPtr pb, Boolean async);

OSErr PBHGetFInfo(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBGetFInfo, PBHGetFInfo, HParmBlkPtr, 0xA00C);

OSErr PBSetFInfo(ParmBlkPtr pb, Boolean async);

OSErr PBHSetFInfo(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBSetFInfo, PBHSetFInfo, HParmBlkPtr, 0xA00D);

OSErr PBUnmountVol(ParmBlkPtr pb);REGISTER_TRAP2(PBUnmountVol, 0xA00E, D0(A0));


OSErr PBMountVol(ParmBlkPtr pb);REGISTER_TRAP2(PBMountVol, 0xA00F, D0(A0));


OSErr PBAllocate(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBAllocate, ParmBlkPtr, 0xA010);

OSErr PBGetEOF(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBGetEOF, ParmBlkPtr, 0xA011);

OSErr PBSetEOF(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBSetEOF, ParmBlkPtr, 0xA012);

OSErr PBFlushVol(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBFlushVol, ParmBlkPtr, 0xA013);

OSErr PBGetVol(ParmBlkPtr pb, Boolean async);

OSErr PBHGetVol(WDPBPtr pb, Boolean async);HFS_TRAP(PBGetVol, PBHGetVol, WDPBPtr, 0xA014);

OSErr PBSetVol(ParmBlkPtr pb, Boolean async);

OSErr PBHSetVol(WDPBPtr pb, Boolean async);HFS_TRAP(PBSetVol, PBHSetVol, WDPBPtr, 0xA015);

OSErr PBEject(ParmBlkPtr pb);REGISTER_TRAP2(PBEject, 0xA017, D0(A0));


OSErr PBGetFPos(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBGetFPos, ParmBlkPtr, 0xA018);

OSErr PBOffLine(ParmBlkPtr pb);REGISTER_TRAP2(PBOffLine, 0xA035, D0(A0));


OSErr PBSetFLock(ParmBlkPtr pb, Boolean async);

OSErr PBHSetFLock(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBSetFLock, PBHSetFLock, HParmBlkPtr, 0xA041);

OSErr PBRstFLock(ParmBlkPtr pb, Boolean async);

OSErr PBHRstFLock(HParmBlkPtr pb, Boolean async);HFS_TRAP(PBRstFLock, PBHRstFLock, HParmBlkPtr, 0xA042);

OSErr PBSetFVers(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBSetFVers, ParmBlkPtr, 0xA043);

OSErr PBSetFPos(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBSetFPos, ParmBlkPtr, 0xA044);

OSErr PBFlushFile(ParmBlkPtr pb, Boolean async);FILE_TRAP(PBFlushFile, ParmBlkPtr, 0xA045);

DISPATCHER_TRAP(FSDispatch, 0xA060, D0W);


OSErr PBOpenWD(WDPBPtr pb, Boolean async);FILE_SUBTRAP(PBOpenWD, WDPBPtr, 0xA260, 0x0001, FSDispatch);

OSErr PBCloseWD(WDPBPtr pb, Boolean async);FILE_SUBTRAP(PBCloseWD, WDPBPtr, 0xA260, 0x0002, FSDispatch);

OSErr PBCatMove(CMovePBPtr pb, Boolean async);FILE_SUBTRAP(PBCatMove, CMovePBPtr, 0xA260, 0x0005, FSDispatch);

OSErr PBDirCreate(HParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBDirCreate, HParmBlkPtr, 0xA260, 0x0006, FSDispatch);

OSErr PBGetWDInfo(WDPBPtr pb, Boolean async);FILE_SUBTRAP(PBGetWDInfo, WDPBPtr, 0xA260, 0x0007, FSDispatch);

OSErr PBGetFCBInfo(FCBPBPtr pb, Boolean async);FILE_SUBTRAP(PBGetFCBInfo, FCBPBPtr, 0xA260, 0x0008, FSDispatch);

OSErr PBGetCatInfo(CInfoPBPtr pb, Boolean async);FILE_SUBTRAP(PBGetCatInfo, CInfoPBPtr, 0xA260, 0x0009, FSDispatch);

OSErr PBSetCatInfo(CInfoPBPtr pb, Boolean async);FILE_SUBTRAP(PBSetCatInfo, CInfoPBPtr, 0xA260, 0x000A, FSDispatch);

OSErr PBSetVInfo(HParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBSetVInfo, HParmBlkPtr, 0xA260, 0x000B, FSDispatch);

OSErr PBLockRange(ParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBLockRange, ParmBlkPtr, 0xA260, 0x0010, FSDispatch);

OSErr PBUnlockRange(ParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBUnlockRange, ParmBlkPtr, 0xA260, 0x0011, FSDispatch);

OSErr PBCreateFileIDRef(ParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBCreateFileIDRef, ParmBlkPtr, 0xA260, 0x0014, FSDispatch);

OSErr PBDeleteFileIDRef(ParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBDeleteFileIDRef, ParmBlkPtr, 0xA260, 0x0015, FSDispatch);

OSErr PBResolveFileIDRef(ParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBResolveFileIDRef, ParmBlkPtr, 0xA260, 0x0016, FSDispatch);

OSErr PBExchangeFiles(ParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBExchangeFiles, ParmBlkPtr, 0xA260, 0x0017, FSDispatch);

OSErr PBCatSearch(ParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBCatSearch, ParmBlkPtr, 0xA260, 0x0018, FSDispatch);

OSErr PBOpenDF(ParmBlkPtr pb, Boolean async);

OSErr PBHOpenDF(HParmBlkPtr pb, Boolean async);HFS_SUBTRAP(PBOpenDF, PBHOpenDF, HParmBlkPtr, 0xA060, 0x001A, FSDispatch);

OSErr PBMakeFSSpec(HParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBMakeFSSpec, HParmBlkPtr, 0xA260, 0x001B, FSDispatch);

OSErr PBHGetVolParms(HParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBHGetVolParms, HParmBlkPtr, 0xA260, 0x0030, FSDispatch);

// MacPhoenix. System 7.5.2's: PBHGetVInfo's record (ioXVersion where
// HVolumeParam has its filler) followed by ioVTotalBytes and ioVFreeBytes
// (UInt64, offset 122). Apple System Profiler (7.6.1) calls it.
OSErr PBXGetVolInfo(HParmBlkPtr pb, Boolean async);FILE_SUBTRAP(PBXGetVolInfo, HParmBlkPtr, 0xA260, 0x0012, FSDispatch);

OSErr PBHGetLogInInfo(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHGetLogInInfo, HParmBlkPtr, 0xA260, 0x0031, FSDispatch);

OSErr PBHGetDirAccess(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHGetDirAccess, HParmBlkPtr, 0xA260, 0x0032, FSDispatch);

OSErr PBHSetDirAccess(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHSetDirAccess, HParmBlkPtr, 0xA260, 0x0033, FSDispatch);

OSErr PBHMapID(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHMapID, HParmBlkPtr, 0xA260, 0x0034, FSDispatch);

OSErr PBHMapName(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHMapName, HParmBlkPtr, 0xA260, 0x0035, FSDispatch);

OSErr PBHCopyFile(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHCopyFile, HParmBlkPtr, 0xA260, 0x0036, FSDispatch);

OSErr PBHMoveRename(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHMoveRename, HParmBlkPtr, 0xA260, 0x0037, FSDispatch);

OSErr PBHOpenDeny(HParmBlkPtr pb, Boolean a);FILE_SUBTRAP(PBHOpenDeny, HParmBlkPtr, 0xA260, 0x0038, FSDispatch);

// prototypes for the high level filesystem dispatch traps
// (MacPhoenix: the low byte, as Mac OS 9.0's HFS Plus calls go to $38;
// the glue loads the selector with MOVE.W, the parameter words above)
DISPATCHER_TRAP(HighLevelFSDispatch, 0xAA52, D0<0xFF>);


OSErr C_FSMakeFSSpec(int16_t vRefNum, int32_t dir_id, ConstStringPtr file_name, FSSpecPtr spec);PASCAL_SUBTRAP(FSMakeFSSpec, 0xAA52, 0x0001, HighLevelFSDispatch);


OSErr C_FSpExchangeFiles(FSSpecPtr src, FSSpecPtr dst);PASCAL_SUBTRAP(FSpExchangeFiles, 0xAA52, 0x000F, HighLevelFSDispatch);


OSErr C_FSpOpenDF(FSSpecPtr spec, SignedByte perms, GUEST<int16_t> *refNum_out);PASCAL_SUBTRAP(FSpOpenDF, 0xAA52, 0x0002, HighLevelFSDispatch);


OSErr C_FSpOpenRF(FSSpecPtr spec, SignedByte perms, GUEST<int16_t> *refNum_out);PASCAL_SUBTRAP(FSpOpenRF, 0xAA52, 0x0003, HighLevelFSDispatch);


OSErr C_FSpCreate(FSSpecPtr spec, OSType creator, OSType file_type, ScriptCode script);PASCAL_SUBTRAP(FSpCreate, 0xAA52, 0x0004, HighLevelFSDispatch);


OSErr C_FSpDirCreate(FSSpecPtr spec, ScriptCode script, GUEST<int32_t> *created_dir_id);PASCAL_SUBTRAP(FSpDirCreate, 0xAA52, 0x0005, HighLevelFSDispatch);


OSErr C_FSpDelete(FSSpecPtr spec);PASCAL_SUBTRAP(FSpDelete, 0xAA52, 0x0006, HighLevelFSDispatch);


OSErr C_FSpGetFInfo(FSSpecPtr spec, FInfo *fndr_info);PASCAL_SUBTRAP(FSpGetFInfo, 0xAA52, 0x0007, HighLevelFSDispatch);


OSErr C_FSpSetFInfo(FSSpecPtr spec, FInfo *fndr_info);PASCAL_SUBTRAP(FSpSetFInfo, 0xAA52, 0x0008, HighLevelFSDispatch);


OSErr C_FSpSetFLock(FSSpecPtr spec);PASCAL_SUBTRAP(FSpSetFLock, 0xAA52, 0x0009, HighLevelFSDispatch);


OSErr C_FSpRstFLock(FSSpecPtr spec);PASCAL_SUBTRAP(FSpRstFLock, 0xAA52, 0x000A, HighLevelFSDispatch);


OSErr C_FSpRename(FSSpecPtr spec, ConstStringPtr new_name);PASCAL_SUBTRAP(FSpRename, 0xAA52, 0x000B, HighLevelFSDispatch);


OSErr C_FSpCatMove(FSSpecPtr src, FSSpecPtr dst);PASCAL_SUBTRAP(FSpCatMove, 0xAA52, 0x000C, HighLevelFSDispatch);


void C_FSpCreateResFile(FSSpecPtr spec, OSType creator, OSType file_type, ScriptCode script);PASCAL_SUBTRAP(FSpCreateResFile, 0xAA52, 0x000E, HighLevelFSDispatch);


INTEGER C_FSpOpenResFile(FSSpecPtr spec, SignedByte perms);PASCAL_SUBTRAP(FSpOpenResFile, 0xAA52, 0x000D, HighLevelFSDispatch);


INTEGER C_HOpenResFile(INTEGER vref, LONGINT dirid, ConstStringPtr file_name, SignedByte perm);PASCAL_TRAP(HOpenResFile, 0xA81A);


void C_HCreateResFile(INTEGER vrefnum, LONGINT parid, ConstStringPtr name);PASCAL_TRAP(HCreateResFile, 0xA81B);


OSErr HCreate(INTEGER vref, LONGINT dirid, ConstStringPtr name, OSType creator, OSType type);NOTRAP_FUNCTION2(HCreate);


OSErr HOpenRF(INTEGER vref, LONGINT dirid, ConstStringPtr name, SignedByte perm, GUEST<INTEGER> *refp);NOTRAP_FUNCTION2(HOpenRF);


OSErr HOpen(INTEGER vref, LONGINT dirid, ConstStringPtr name, SignedByte perm, GUEST<INTEGER> *refp);NOTRAP_FUNCTION2(HOpen);


OSErr HOpenDF(INTEGER vref, LONGINT dirid, ConstStringPtr name, SignedByte perm, GUEST<INTEGER> *refp);NOTRAP_FUNCTION2(HOpenDF);


OSErr HRename(INTEGER vref, LONGINT dirid, ConstStringPtr src, ConstStringPtr dst);NOTRAP_FUNCTION2(HRename);


// NOTRAP_FUNCTION2(HRename);
OSErr HDelete(INTEGER vref, LONGINT dirid, ConstStringPtr name);NOTRAP_FUNCTION2(HDelete);


// NOTRAP_FUNCTION2(HDelete);
OSErr GetWDInfo(INTEGER wd, GUEST<INTEGER> *vrefp, GUEST<LONGINT> *dirp, GUEST<LONGINT> *procp);





typedef uint16_t UniChar;

typedef uint32_t UniCharCount;

typedef uint32_t ItemCount;

typedef uint32_t ByteCount;

typedef uint32_t OptionBits;

typedef uint32_t TextEncoding;

typedef int16_t FSVolumeRefNum;

typedef uint32_t FSCatalogInfoBitmap;

typedef uint32_t FSVolumeInfoBitmap;

typedef uint32_t FSIteratorFlags;

typedef uint16_t FSAllocationFlags;

typedef Ptr FSIterator;

struct wide{GUEST_STRUCT;GUEST<int32_t> hi;GUEST<uint32_t> lo;};static_assert(sizeof(wide) == 8);void describeStruct(const wide&, std::ostream&);

struct UTCDateTime{GUEST_STRUCT;GUEST<uint16_t> highSeconds;GUEST<uint32_t> lowSeconds;GUEST<uint16_t> fraction;};static_assert(sizeof(UTCDateTime) == 8);void describeStruct(const UTCDateTime&, std::ostream&);

struct HFSUniStr255{GUEST_STRUCT;GUEST<uint16_t> length;GUEST<UniChar> unicode[255];};static_assert(sizeof(HFSUniStr255) == 512);void describeStruct(const HFSUniStr255&, std::ostream&);

typedef HFSUniStr255 *HFSUniStr255Ptr;

struct FSRef{GUEST_STRUCT;uint8_t hidden[80];};static_assert(sizeof(FSRef) == 80);void describeStruct(const FSRef&, std::ostream&);

typedef FSRef *FSRefPtr;

struct CatPositionRec{GUEST_STRUCT;GUEST<int32_t> initialize;GUEST<int16_t> priv[6];};static_assert(sizeof(CatPositionRec) == 16);void describeStruct(const CatPositionRec&, std::ostream&);

struct FSCatalogInfo{GUEST_STRUCT;GUEST<uint16_t> nodeFlags;GUEST<FSVolumeRefNum> volume;GUEST<uint32_t> parentDirID;GUEST<uint32_t> nodeID;uint8_t sharingFlags;uint8_t userPrivileges;uint8_t reserved1;uint8_t reserved2;UTCDateTime createDate;UTCDateTime contentModDate;UTCDateTime attributeModDate;UTCDateTime accessDate;UTCDateTime backupDate;GUEST<uint32_t> permissions[4];uint8_t finderInfo[16];uint8_t extFinderInfo[16];GUEST<uint64_t> dataLogicalSize;GUEST<uint64_t> dataPhysicalSize;GUEST<uint64_t> rsrcLogicalSize;GUEST<uint64_t> rsrcPhysicalSize;GUEST<uint32_t> valence;GUEST<TextEncoding> textEncodingHint;};static_assert(sizeof(FSCatalogInfo) == 144);void describeStruct(const FSCatalogInfo&, std::ostream&);

typedef FSCatalogInfo *FSCatalogInfoPtr;

struct FSRefParam{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<int16_t> reserved1;uint8_t reserved2;uint8_t reserved3;GUEST<FSRefPtr> ref;GUEST<FSCatalogInfoBitmap> whichInfo;GUEST<FSCatalogInfoPtr> catInfo;GUEST<UniCharCount> nameLength;GUEST<GUEST<UniChar>*> name;GUEST<int32_t> ioDirID;GUEST<FSSpecPtr> spec;GUEST<FSRefPtr> parentRef;GUEST<FSRefPtr> newRef;GUEST<TextEncoding> textEncodingHint;GUEST<HFSUniStr255Ptr> outName;};static_assert(sizeof(FSRefParam) == 72);void describeStruct(const FSRefParam&, std::ostream&);

typedef FSRefParam *FSRefParamPtr;

struct FSSearchParams{GUEST_STRUCT;GUEST<int32_t> searchTime;GUEST<OptionBits> searchBits;GUEST<UniCharCount> searchNameLength;GUEST<GUEST<UniChar>*> searchName;GUEST<FSCatalogInfoPtr> searchInfo1;GUEST<FSCatalogInfoPtr> searchInfo2;};static_assert(sizeof(FSSearchParams) == 24);void describeStruct(const FSSearchParams&, std::ostream&);

typedef FSSearchParams *FSSearchParamsPtr;

struct FSCatalogBulkParam{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;Boolean containerChanged;uint8_t reserved;GUEST<FSIteratorFlags> iteratorFlags;GUEST<FSIterator> iterator;GUEST<FSRefPtr> container;GUEST<ItemCount> maximumItems;GUEST<ItemCount> actualItems;GUEST<FSCatalogInfoBitmap> whichInfo;GUEST<FSCatalogInfoPtr> catalogInfo;GUEST<FSRefPtr> refs;GUEST<FSSpecPtr> specs;GUEST<HFSUniStr255Ptr> names;GUEST<FSSearchParamsPtr> searchParams;};static_assert(sizeof(FSCatalogBulkParam) == 64);void describeStruct(const FSCatalogBulkParam&, std::ostream&);

typedef FSCatalogBulkParam *FSCatalogBulkParamPtr;

struct FSForkIOParam{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;GUEST<Ptr> reserved1;GUEST<int16_t> reserved2;GUEST<int16_t> forkRefNum;uint8_t reserved3;int8_t permissions;GUEST<FSRefPtr> ref;GUEST<Ptr> buffer;GUEST<uint32_t> requestCount;GUEST<uint32_t> actualCount;GUEST<uint16_t> positionMode;GUEST<int64_t> positionOffset;GUEST<FSAllocationFlags> allocationFlags;GUEST<uint64_t> allocationAmount;GUEST<UniCharCount> forkNameLength;GUEST<GUEST<UniChar>*> forkName;CatPositionRec forkIterator;GUEST<HFSUniStr255Ptr> outForkName;};static_assert(sizeof(FSForkIOParam) == 92);void describeStruct(const FSForkIOParam&, std::ostream&);

typedef FSForkIOParam *FSForkIOParamPtr;

struct FSForkInfo{GUEST_STRUCT;int8_t flags;int8_t permissions;GUEST<FSVolumeRefNum> volume;GUEST<uint32_t> reserved2;GUEST<uint32_t> nodeID;GUEST<uint32_t> forkID;GUEST<uint64_t> currentPosition;GUEST<uint64_t> logicalEOF;GUEST<uint64_t> physicalEOF;GUEST<uint64_t> process;};static_assert(sizeof(FSForkInfo) == 48);void describeStruct(const FSForkInfo&, std::ostream&);

typedef FSForkInfo *FSForkInfoPtr;

struct FSForkCBInfoParam{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;GUEST<int16_t> desiredRefNum;GUEST<int16_t> volumeRefNum;GUEST<int16_t> iterator;GUEST<int16_t> actualRefNum;GUEST<FSRefPtr> ref;GUEST<FSForkInfoPtr> forkInfo;GUEST<HFSUniStr255Ptr> forkName;};static_assert(sizeof(FSForkCBInfoParam) == 38);void describeStruct(const FSForkCBInfoParam&, std::ostream&);

typedef FSForkCBInfoParam *FSForkCBInfoParamPtr;

struct FSVolumeInfo{GUEST_STRUCT;UTCDateTime createDate;UTCDateTime modifyDate;UTCDateTime backupDate;UTCDateTime checkedDate;GUEST<uint32_t> fileCount;GUEST<uint32_t> folderCount;GUEST<uint64_t> totalBytes;GUEST<uint64_t> freeBytes;GUEST<uint32_t> blockSize;GUEST<uint32_t> totalBlocks;GUEST<uint32_t> freeBlocks;GUEST<uint32_t> nextAllocation;GUEST<uint32_t> rsrcClumpSize;GUEST<uint32_t> dataClumpSize;GUEST<uint32_t> nextCatalogID;uint8_t finderInfo[32];GUEST<uint16_t> flags;GUEST<uint16_t> filesystemID;GUEST<uint16_t> signature;GUEST<uint16_t> driveNumber;GUEST<int16_t> driverRefNum;};static_assert(sizeof(FSVolumeInfo) == 126);void describeStruct(const FSVolumeInfo&, std::ostream&);

typedef FSVolumeInfo *FSVolumeInfoPtr;

struct FSVolumeInfoParam{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;GUEST<StringPtr> ioNamePtr;GUEST<FSVolumeRefNum> ioVRefNum;GUEST<uint32_t> volumeIndex;GUEST<FSVolumeInfoBitmap> whichInfo;GUEST<FSVolumeInfoPtr> volumeInfo;GUEST<HFSUniStr255Ptr> volumeName;GUEST<FSRefPtr> ref;};static_assert(sizeof(FSVolumeInfoParam) == 44);void describeStruct(const FSVolumeInfoParam&, std::ostream&);

typedef FSVolumeInfoParam *FSVolumeInfoParamPtr;

// PBReadFork and PBWriteFork (Mac OS 9.0): trap $A2A8 (HFS bit set), the selector in D0.
DISPATCHER_TRAP(FSForkDispatch, 0xA0A8, D0W);


OSErr PBMakeFSRef(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBMakeFSRef, FSRefParamPtr, 0xA260, 0x006E, FSDispatch);

OSErr PBMakeFSRefUnicode(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBMakeFSRefUnicode, FSRefParamPtr, 0xA260, 0x007A, FSDispatch);

OSErr PBCompareFSRefs(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBCompareFSRefs, FSRefParamPtr, 0xA260, 0x007C, FSDispatch);

OSErr PBCreateFileUnicode(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBCreateFileUnicode, FSRefParamPtr, 0xA260, 0x0070, FSDispatch);

OSErr PBCreateDirectoryUnicode(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBCreateDirectoryUnicode, FSRefParamPtr, 0xA260, 0x0071, FSDispatch);

OSErr PBDeleteObject(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBDeleteObject, FSRefParamPtr, 0xA260, 0x0072, FSDispatch);

OSErr PBMoveObject(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBMoveObject, FSRefParamPtr, 0xA260, 0x0073, FSDispatch);

OSErr PBRenameUnicode(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBRenameUnicode, FSRefParamPtr, 0xA260, 0x0074, FSDispatch);

OSErr PBExchangeObjects(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBExchangeObjects, FSRefParamPtr, 0xA260, 0x0075, FSDispatch);

OSErr PBGetCatalogInfo(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBGetCatalogInfo, FSRefParamPtr, 0xA260, 0x0076, FSDispatch);

OSErr PBSetCatalogInfo(FSRefParamPtr pb, Boolean async);FILE_SUBTRAP(PBSetCatalogInfo, FSRefParamPtr, 0xA260, 0x0077, FSDispatch);

OSErr PBOpenIterator(FSCatalogBulkParamPtr pb, Boolean async);FILE_SUBTRAP(PBOpenIterator, FSCatalogBulkParamPtr, 0xA260, 0x0078, FSDispatch);

OSErr PBCloseIterator(FSCatalogBulkParamPtr pb, Boolean async);FILE_SUBTRAP(PBCloseIterator, FSCatalogBulkParamPtr, 0xA260, 0x005B, FSDispatch);

OSErr PBGetCatalogInfoBulk(FSCatalogBulkParamPtr pb, Boolean async);FILE_SUBTRAP(PBGetCatalogInfoBulk, FSCatalogBulkParamPtr, 0xA260, 0x005C, FSDispatch);

OSErr PBCatalogSearch(FSCatalogBulkParamPtr pb, Boolean async);FILE_SUBTRAP(PBCatalogSearch, FSCatalogBulkParamPtr, 0xA260, 0x005D, FSDispatch);

OSErr PBOpenFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBOpenFork, FSForkIOParamPtr, 0xA260, 0x0079, FSDispatch);

OSErr PBCreateFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBCreateFork, FSForkIOParamPtr, 0xA260, 0x007D, FSDispatch);

OSErr PBDeleteFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBDeleteFork, FSForkIOParamPtr, 0xA260, 0x007E, FSDispatch);

OSErr PBIterateForks(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBIterateForks, FSForkIOParamPtr, 0xA260, 0x007F, FSDispatch);

OSErr PBGetForkPosition(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBGetForkPosition, FSForkIOParamPtr, 0xA260, 0x0053, FSDispatch);

OSErr PBSetForkPosition(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBSetForkPosition, FSForkIOParamPtr, 0xA260, 0x0054, FSDispatch);

OSErr PBGetForkSize(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBGetForkSize, FSForkIOParamPtr, 0xA260, 0x0055, FSDispatch);

OSErr PBSetForkSize(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBSetForkSize, FSForkIOParamPtr, 0xA260, 0x0056, FSDispatch);

OSErr PBAllocateFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBAllocateFork, FSForkIOParamPtr, 0xA260, 0x0057, FSDispatch);

OSErr PBFlushFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBFlushFork, FSForkIOParamPtr, 0xA260, 0x0058, FSDispatch);

OSErr PBCloseFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBCloseFork, FSForkIOParamPtr, 0xA260, 0x0059, FSDispatch);

OSErr PBGetForkCBInfo(FSForkCBInfoParamPtr pb, Boolean async);FILE_SUBTRAP(PBGetForkCBInfo, FSForkCBInfoParamPtr, 0xA260, 0x005A, FSDispatch);

OSErr PBGetVolumeInfo(FSVolumeInfoParamPtr pb, Boolean async);FILE_SUBTRAP(PBGetVolumeInfo, FSVolumeInfoParamPtr, 0xA260, 0x001D, FSDispatch);

OSErr PBSetVolumeInfo(FSVolumeInfoParamPtr pb, Boolean async);FILE_SUBTRAP(PBSetVolumeInfo, FSVolumeInfoParamPtr, 0xA260, 0x001E, FSDispatch);

OSErr PBReadFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBReadFork, FSForkIOParamPtr, 0xA2A8, 0x0051, FSForkDispatch);

OSErr PBWriteFork(FSForkIOParamPtr pb, Boolean async);FILE_SUBTRAP(PBWriteFork, FSForkIOParamPtr, 0xA2A8, 0x0052, FSForkDispatch);

OSErr C_FSpMakeFSRef(FSSpecPtr source, FSRefPtr newRef);PASCAL_SUBTRAP(FSpMakeFSRef, 0xAA52, 0x001A, HighLevelFSDispatch);


OSErr C_FSMakeFSRefUnicode(FSRefPtr parentRef, UniCharCount nameLength, GUEST<UniChar> *name, TextEncoding textEncodingHint, FSRefPtr newRef);PASCAL_SUBTRAP(FSMakeFSRefUnicode, 0xAA52, 0x001B, HighLevelFSDispatch);


OSErr C_FSCompareFSRefs(FSRefPtr ref1, FSRefPtr ref2);PASCAL_SUBTRAP(FSCompareFSRefs, 0xAA52, 0x0035, HighLevelFSDispatch);


OSErr C_FSCreateFileUnicode(FSRefPtr parentRef, UniCharCount nameLength, GUEST<UniChar> *name, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo, FSRefPtr newRef, FSSpecPtr newSpec);PASCAL_SUBTRAP(FSCreateFileUnicode, 0xAA52, 0x001C, HighLevelFSDispatch);


OSErr C_FSCreateDirectoryUnicode(FSRefPtr parentRef, UniCharCount nameLength, GUEST<UniChar> *name, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo, FSRefPtr newRef, FSSpecPtr newSpec, GUEST<uint32_t> *newDirID);PASCAL_SUBTRAP(FSCreateDirectoryUnicode, 0xAA52, 0x001D, HighLevelFSDispatch);


OSErr C_FSDeleteObject(FSRefPtr ref);PASCAL_SUBTRAP(FSDeleteObject, 0xAA52, 0x001E, HighLevelFSDispatch);


OSErr C_FSMoveObject(FSRefPtr ref, FSRefPtr destDirectory, FSRefPtr newRef);PASCAL_SUBTRAP(FSMoveObject, 0xAA52, 0x001F, HighLevelFSDispatch);


OSErr C_FSRenameUnicode(FSRefPtr ref, UniCharCount nameLength, GUEST<UniChar> *name, TextEncoding textEncodingHint, FSRefPtr newRef);PASCAL_SUBTRAP(FSRenameUnicode, 0xAA52, 0x0020, HighLevelFSDispatch);


OSErr C_FSExchangeObjects(FSRefPtr ref, FSRefPtr destRef);PASCAL_SUBTRAP(FSExchangeObjects, 0xAA52, 0x0021, HighLevelFSDispatch);


OSErr C_FSGetCatalogInfo(FSRefPtr ref, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo, HFSUniStr255Ptr outName, FSSpecPtr fsSpec, FSRefPtr parentRef);PASCAL_SUBTRAP(FSGetCatalogInfo, 0xAA52, 0x0022, HighLevelFSDispatch);


OSErr C_FSSetCatalogInfo(FSRefPtr ref, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfo);PASCAL_SUBTRAP(FSSetCatalogInfo, 0xAA52, 0x0023, HighLevelFSDispatch);


OSErr C_FSOpenIterator(FSRefPtr container, FSIteratorFlags iteratorFlags, GUEST<FSIterator> *iterator);PASCAL_SUBTRAP(FSOpenIterator, 0xAA52, 0x0024, HighLevelFSDispatch);


OSErr C_FSCloseIterator(FSIterator iterator);PASCAL_SUBTRAP(FSCloseIterator, 0xAA52, 0x0025, HighLevelFSDispatch);


OSErr C_FSGetCatalogInfoBulk(FSIterator iterator, ItemCount maximumObjects, GUEST<ItemCount> *actualObjects, Boolean *containerChanged, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfos, FSRefPtr refs, FSSpecPtr specs, HFSUniStr255Ptr names);PASCAL_SUBTRAP(FSGetCatalogInfoBulk, 0xAA52, 0x0026, HighLevelFSDispatch);


OSErr C_FSCatalogSearch(FSIterator iterator, FSSearchParamsPtr searchCriteria, ItemCount maximumObjects, GUEST<ItemCount> *actualObjects, Boolean *containerChanged, FSCatalogInfoBitmap whichInfo, FSCatalogInfoPtr catalogInfos, FSRefPtr refs, FSSpecPtr specs, HFSUniStr255Ptr names);PASCAL_SUBTRAP(FSCatalogSearch, 0xAA52, 0x0027, HighLevelFSDispatch);


OSErr C_FSCreateFork(FSRefPtr ref, UniCharCount forkNameLength, GUEST<UniChar> *forkName);PASCAL_SUBTRAP(FSCreateFork, 0xAA52, 0x0036, HighLevelFSDispatch);


OSErr C_FSDeleteFork(FSRefPtr ref, UniCharCount forkNameLength, GUEST<UniChar> *forkName);PASCAL_SUBTRAP(FSDeleteFork, 0xAA52, 0x0037, HighLevelFSDispatch);


OSErr C_FSIterateForks(FSRefPtr ref, CatPositionRec *forkIterator, HFSUniStr255Ptr forkName, GUEST<int64_t> *forkSize, GUEST<uint64_t> *forkPhysicalSize);PASCAL_SUBTRAP(FSIterateForks, 0xAA52, 0x0038, HighLevelFSDispatch);


OSErr C_FSOpenFork(FSRefPtr ref, UniCharCount forkNameLength, GUEST<UniChar> *forkName, int8_t permissions, GUEST<int16_t> *forkRefNum);PASCAL_SUBTRAP(FSOpenFork, 0xAA52, 0x0028, HighLevelFSDispatch);


// 68K glue: a Pascal value parameter over 4 bytes (SInt64) comes as a
// pointer to it; PowerPC passes the value (FSReadFork, InterfaceLib only).
OSErr C_FSReadFork68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset, ByteCount requestCount, Ptr buffer, GUEST<ByteCount> *actualCount);PASCAL_SUBTRAP(FSReadFork68K, 0xAA52, 0x0029, HighLevelFSDispatch);


OSErr C_FSReadFork(int16_t forkRefNum, uint16_t positionMode, wide positionOffset, ByteCount requestCount, Ptr buffer, GUEST<ByteCount> *actualCount);NOTRAP_FUNCTION(FSReadFork);


// 68K glue: a Pascal value parameter over 4 bytes (SInt64) comes as a
// pointer to it; PowerPC passes the value (FSWriteFork, InterfaceLib only).
OSErr C_FSWriteFork68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset, ByteCount requestCount, Ptr buffer, GUEST<ByteCount> *actualCount);PASCAL_SUBTRAP(FSWriteFork68K, 0xAA52, 0x002A, HighLevelFSDispatch);


OSErr C_FSWriteFork(int16_t forkRefNum, uint16_t positionMode, wide positionOffset, ByteCount requestCount, Ptr buffer, GUEST<ByteCount> *actualCount);NOTRAP_FUNCTION(FSWriteFork);


OSErr C_FSGetForkPosition(int16_t forkRefNum, GUEST<int64_t> *position);PASCAL_SUBTRAP(FSGetForkPosition, 0xAA52, 0x002B, HighLevelFSDispatch);


// 68K glue: a Pascal value parameter over 4 bytes (SInt64) comes as a
// pointer to it; PowerPC passes the value (FSSetForkPosition, InterfaceLib only).
OSErr C_FSSetForkPosition68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset);PASCAL_SUBTRAP(FSSetForkPosition68K, 0xAA52, 0x002C, HighLevelFSDispatch);


OSErr C_FSSetForkPosition(int16_t forkRefNum, uint16_t positionMode, wide positionOffset);NOTRAP_FUNCTION(FSSetForkPosition);


OSErr C_FSGetForkSize(int16_t forkRefNum, GUEST<int64_t> *forkSize);PASCAL_SUBTRAP(FSGetForkSize, 0xAA52, 0x002D, HighLevelFSDispatch);


// 68K glue: a Pascal value parameter over 4 bytes (SInt64) comes as a
// pointer to it; PowerPC passes the value (FSSetForkSize, InterfaceLib only).
OSErr C_FSSetForkSize68K(int16_t forkRefNum, uint16_t positionMode, wide *positionOffset);PASCAL_SUBTRAP(FSSetForkSize68K, 0xAA52, 0x002E, HighLevelFSDispatch);


OSErr C_FSSetForkSize(int16_t forkRefNum, uint16_t positionMode, wide positionOffset);NOTRAP_FUNCTION(FSSetForkSize);


// 68K glue: a Pascal value parameter over 4 bytes (SInt64) comes as a
// pointer to it; PowerPC passes the value (FSAllocateFork, InterfaceLib only).
OSErr C_FSAllocateFork68K(int16_t forkRefNum, FSAllocationFlags flags, uint16_t positionMode, wide *positionOffset, wide *requestCount, GUEST<uint64_t> *actualCount);PASCAL_SUBTRAP(FSAllocateFork68K, 0xAA52, 0x002F, HighLevelFSDispatch);


OSErr C_FSAllocateFork(int16_t forkRefNum, FSAllocationFlags flags, uint16_t positionMode, wide positionOffset, wide requestCount, GUEST<uint64_t> *actualCount);NOTRAP_FUNCTION(FSAllocateFork);


OSErr C_FSFlushFork(int16_t forkRefNum);PASCAL_SUBTRAP(FSFlushFork, 0xAA52, 0x0030, HighLevelFSDispatch);


OSErr C_FSCloseFork(int16_t forkRefNum);PASCAL_SUBTRAP(FSCloseFork, 0xAA52, 0x0031, HighLevelFSDispatch);


OSErr C_FSGetForkCBInfo(int16_t desiredRefNum, FSVolumeRefNum volume, GUEST<int16_t> *iterator, GUEST<int16_t> *actualRefNum, FSForkInfoPtr forkInfo, FSRefPtr ref, HFSUniStr255Ptr outForkName);PASCAL_SUBTRAP(FSGetForkCBInfo, 0xAA52, 0x0032, HighLevelFSDispatch);


OSErr C_FSGetVolumeInfo(FSVolumeRefNum volume, ItemCount volumeIndex, GUEST<FSVolumeRefNum> *actualVolume, FSVolumeInfoBitmap whichInfo, FSVolumeInfoPtr info, HFSUniStr255Ptr volumeName, FSRefPtr rootDirectory);PASCAL_SUBTRAP(FSGetVolumeInfo, 0xAA52, 0x0033, HighLevelFSDispatch);


OSErr C_FSSetVolumeInfo(FSVolumeRefNum volume, FSVolumeInfoBitmap whichInfo, FSVolumeInfoPtr info);PASCAL_SUBTRAP(FSSetVolumeInfo, 0xAA52, 0x0034, HighLevelFSDispatch);


OSErr C_FSGetDataForkName(HFSUniStr255Ptr dataForkName);PASCAL_SUBTRAP(FSGetDataForkName, 0xAA52, 0x0018, HighLevelFSDispatch);


OSErr C_FSGetResourceForkName(HFSUniStr255Ptr resourceForkName);PASCAL_SUBTRAP(FSGetResourceForkName, 0xAA52, 0x0019, HighLevelFSDispatch);


} /* namespace Executor */