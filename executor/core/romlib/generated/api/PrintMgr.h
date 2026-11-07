/*****************************************************************************
 *                                                                           *
 *                                 PrintMgr.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     QuickDraw.h    GrafPort, QDProcs, PicHandle                           *
 *     MacTypes.h     LONGINT, Boolean, INTEGER, Rect, SignedByte,           *
 *                    StringPtr, Handle, Ptr                                 *
 *     DialogMgr.h    DialogRecord, ModalFilterUPP, UserItemUPP              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "QuickDraw.h"
#include "ExMacTypes.h"
#include "DialogMgr.h"

#define MODULE_NAME PrintMgr
#include <base/api-module.h>


namespace Executor {

enum {bDraftLoop = 0,
bSpoolLoop = 1,
};

enum {bDevCItoh = 1,
bDevLaser = 3,
};

enum {iPFMaxPgs = 128,
};

enum {iPrSavPFil = -1,
iIOAbort = -27,
MemFullErr = -108,
iPrAbort = 128,
};

enum {iPrDevCtl = 7,
lPrReset = 65536,
lPrLineFeed = 196608,
lPrLFSixth = 262143,
lPrPageEnd = 131072,
iPrBitsCtl = 4,
lScreenBits = 0,
lPaintBits = 1,
iPrIOCtl = 5,
};

BEGIN_EXECUTOR_ONLY
const char *const sPrDrvr = ".Print";
END_EXECUTOR_ONLY


enum {iPrDrvrRef = -3,
};

struct TPrPort{GUEST_STRUCT;GrafPort gPort;QDProcs saveprocs;GUEST<LONGINT> spare[4];Boolean fOurPtr;Boolean fOurBits;};static_assert(sizeof(TPrPort) == 178);void describeStruct(const TPrPort&, std::ostream&);

typedef TPrPort *TPPrPort;

struct TPrInfo{GUEST_STRUCT;GUEST<INTEGER> iDev;GUEST<INTEGER> iVRes;GUEST<INTEGER> iHRes;Rect rPage;};static_assert(sizeof(TPrInfo) == 14);void describeStruct(const TPrInfo&, std::ostream&);

typedef enum {feedCut,
feedFanFold,
feedMechCut,
feedOther,
}TFeed;

struct TPrStl{GUEST_STRUCT;GUEST<INTEGER> wDev;GUEST<INTEGER> iPageV;GUEST<INTEGER> iPageH;SignedByte bPort;char feed;};static_assert(sizeof(TPrStl) == 8);void describeStruct(const TPrStl&, std::ostream&);

typedef enum {scanTB,
scanBL,
scanLR,
scanRL,
}TScan;

struct TPrXInfo{GUEST_STRUCT;GUEST<INTEGER> iRowBytes;GUEST<INTEGER> iBandV;GUEST<INTEGER> iBandH;GUEST<INTEGER> iDevBytes;GUEST<INTEGER> iBands;SignedByte bPatScale;SignedByte bULThick;SignedByte bULOffset;SignedByte bULShadow;char scan;SignedByte bXInfoX;};static_assert(sizeof(TPrXInfo) == 16);void describeStruct(const TPrXInfo&, std::ostream&);

struct TPrJob{GUEST_STRUCT;GUEST<INTEGER> iFstPage;GUEST<INTEGER> iLstPage;GUEST<INTEGER> iCopies;SignedByte bJDocLoop;Boolean fFromUsr;GUEST<ProcPtr> pIdleProc;GUEST<StringPtr> pFileName;GUEST<INTEGER> iFileVol;SignedByte bFileVers;SignedByte bJobX;};static_assert(sizeof(TPrJob) == 20);void describeStruct(const TPrJob&, std::ostream&);

struct TPrint{GUEST_STRUCT;GUEST<INTEGER> iPrVersion;TPrInfo prInfo;Rect rPaper;TPrStl prStl;TPrInfo prInfoPT;TPrXInfo prXInfo;TPrJob prJob;GUEST<INTEGER> printX[19];};static_assert(sizeof(TPrint) == 120);void describeStruct(const TPrint&, std::ostream&);

typedef TPrint *TPPrint;

typedef GUEST<TPPrint> *THPrint;

typedef Rect *TPRect;

struct TPrStatus{GUEST_STRUCT;GUEST<INTEGER> iTotPages;GUEST<INTEGER> iCurPage;GUEST<INTEGER> iTotCopies;GUEST<INTEGER> iCurCopy;GUEST<INTEGER> iTotBands;GUEST<INTEGER> iCurBand;Boolean fPgDirty;Boolean fImaging;GUEST<THPrint> hPrint;GUEST<TPPrPort> pPRPort;GUEST<PicHandle> hPic;};static_assert(sizeof(TPrStatus) == 26);void describeStruct(const TPrStatus&, std::ostream&);

struct TPrDlg{GUEST_STRUCT;DialogRecord dlg;GUEST<ModalFilterUPP> pFltrProc;GUEST<UserItemUPP> pItemProc;GUEST<THPrint> hPrintUsr;Boolean fDoIt;Boolean fDone;GUEST<LONGINT> lUser1;GUEST<LONGINT> lUser2;GUEST<LONGINT> lUser3;GUEST<LONGINT> lUser4;GUEST<INTEGER> iNumFst;GUEST<INTEGER> iNumLst;};static_assert(sizeof(TPrDlg) == 204);void describeStruct(const TPrDlg&, std::ostream&);

// From Technote 095
// more stuff may be here
typedef TPrDlg *TPPrDlg;

// PrintMgr IMII-161 (true-b);
const LowMemGlobal<INTEGER > PrintErr{ 0x944 };
inline INTEGER  LMGetPrintErr() { return LM(PrintErr); }
inline void LMSetPrintErr(INTEGER val) { LM(PrintErr) = val; }
NOTRAP_FUNCTION2(LMGetPrintErr);
NOTRAP_FUNCTION2(LMSetPrintErr);


DISPATCHER_TRAP(PrGlue, 0xA8FD, StackL);


INTEGER C_PrError();PASCAL_SUBTRAP(PrError, 0xA8FD, 0xBA000000, PrGlue);


void C_PrSetError(INTEGER iErr);PASCAL_SUBTRAP(PrSetError, 0xA8FD, 0xC0000200, PrGlue);


void C_PrOpen();PASCAL_SUBTRAP(PrOpen, 0xA8FD, 0xC8000000, PrGlue);


void C_PrClose();PASCAL_SUBTRAP(PrClose, 0xA8FD, 0xD0000000, PrGlue);


void C_PrDrvrOpen();PASCAL_SUBTRAP(PrDrvrOpen, 0xA8FD, 0x80000000, PrGlue);


void C_PrDrvrClose();PASCAL_SUBTRAP(PrDrvrClose, 0xA8FD, 0x88000000, PrGlue);


void C_PrCtlCall(INTEGER iWhichCtl, LONGINT lParam1, LONGINT lParam2, LONGINT lParam3);PASCAL_SUBTRAP(PrCtlCall, 0xA8FD, 0xA0000E00, PrGlue);


Handle C_PrDrvrDCE();PASCAL_SUBTRAP(PrDrvrDCE, 0xA8FD, 0x94000000, PrGlue);


INTEGER C_PrDrvrVers();PASCAL_SUBTRAP(PrDrvrVers, 0xA8FD, 0x9A000000, PrGlue);


TPPrDlg C_PrJobInit(THPrint hPrint);PASCAL_SUBTRAP(PrJobInit, 0xA8FD, 0x44040410, PrGlue);


TPPrDlg C_PrStlInit(THPrint hPrint);PASCAL_SUBTRAP(PrStlInit, 0xA8FD, 0x3C04040C, PrGlue);


Boolean C_PrDlgMain(THPrint hPrint, ProcPtr initfptr);PASCAL_SUBTRAP(PrDlgMain, 0xA8FD, 0x4A040894, PrGlue);


void C_PrGeneral(Ptr pData);PASCAL_SUBTRAP(PrGeneral, 0xA8FD, 0x70070480, PrGlue);


TPPrPort C_PrOpenDoc(THPrint hPrint, TPPrPort port, Ptr pIOBuf);PASCAL_SUBTRAP(PrOpenDoc, 0xA8FD, 0x4000C00, PrGlue);


void C_PrOpenPage(TPPrPort port, TPRect pPageFrame);PASCAL_SUBTRAP(PrOpenPage, 0xA8FD, 0x10000808, PrGlue);


void C_PrClosePage(TPPrPort pPrPort);PASCAL_SUBTRAP(PrClosePage, 0xA8FD, 0x1800040C, PrGlue);


void C_PrCloseDoc(TPPrPort port);PASCAL_SUBTRAP(PrCloseDoc, 0xA8FD, 0x8000484, PrGlue);


void C_PrPicFile(THPrint hPrint, TPPrPort pPrPort, Ptr pIOBuf, Ptr pDevBuf, TPrStatus *prStatus);PASCAL_SUBTRAP(PrPicFile, 0xA8FD, 0x60051480, PrGlue);


void C_PrintDefault(THPrint hPrint);PASCAL_SUBTRAP(PrintDefault, 0xA8FD, 0x20040480, PrGlue);


Boolean C_PrValidate(THPrint hPrint);PASCAL_SUBTRAP(PrValidate, 0xA8FD, 0x52040498, PrGlue);


Boolean C_PrStlDialog(THPrint hPrint);PASCAL_SUBTRAP(PrStlDialog, 0xA8FD, 0x2A040484, PrGlue);


Boolean C_PrJobDialog(THPrint hPrint);PASCAL_SUBTRAP(PrJobDialog, 0xA8FD, 0x32040488, PrGlue);


void C_PrJobMerge(THPrint hPrintSrc, THPrint hPrintDst);PASCAL_SUBTRAP(PrJobMerge, 0xA8FD, 0x5804089C, PrGlue);


} /* namespace Executor */