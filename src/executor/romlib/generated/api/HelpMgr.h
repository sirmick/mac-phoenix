/*****************************************************************************
 *                                                                           *
 *                                 HelpMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     INTEGER, Str255, OSErr, Boolean, RectPtr, Ptr,         *
 *                    LONGINT, ResType, Rect                                 *
 *     TextEdit.h     TEHandle                                               *
 *     QuickDraw.h    PicHandle                                              *
 *     MenuMgr.h      MenuHandle                                             *
 *     WindowMgr.h    WindowPtr                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "TextEdit.h"
#include "QuickDraw.h"
#include "MenuMgr.h"
#include "WindowMgr.h"

#define MODULE_NAME HelpMgr
#include <base/api-module.h>


namespace Executor {

struct HMStringResType{GUEST_STRUCT;GUEST<INTEGER> hmmResID;GUEST<INTEGER> hmmIndex;};static_assert(sizeof(HMStringResType) == 4);void describeStruct(const HMStringResType&, std::ostream&);

struct HMMessageRecord{GUEST_STRUCT;GUEST<INTEGER> hmmHelpType;union{GUEST<Str255> hmmString;GUEST<INTEGER> hmmPict;HMStringResType hmmStringRes;GUEST<TEHandle> hmmTEHandle;GUEST<PicHandle> hmmPictHandle;GUEST<INTEGER> hmmTERes;GUEST<INTEGER> hmmSTRRes;} u;};static_assert(sizeof(HMMessageRecord) == 258);void describeStruct(const HMMessageRecord&, std::ostream&);

typedef HMMessageRecord *HMMessageRecPtr;

enum {hmHelpDisabled = -850,
hmBalloonAborted = -853,
hmSameAsLastBalloon = -854,
hmHelpManagerNotInited = -855,
hmSkippedBalloon = -857,
hmWrongVersion = -858,
hmUnknownHelpType = -859,
hmOperationUnsupported = -861,
hmNoBalloonUp = -862,
hmCloseViewActive = -863,
};

// & 0xFF ? ###
// 
// MacPhoenix: on Apple's System file the trap goes to the System's
// PACK 14 (helppkg.cpp); the C_ functions below answer only without it.
DISPATCHER_TRAP(Pack14, 0xA830, D0W);


// MacPhoenix (name is a guess): 7.5.5's detached handle to PACK 14,
// which its Pack14 trap enters through CallDetachedPackage ($668) with
// the lock count at ExpandMem+$130. In the per-process low-memory
// range ($BC2-$BFD), the same in every process.
const LowMemGlobal<Handle > HelpPackage{ 0xBE0 };
inline Handle  LMGetHelpPackage() { return LM(HelpPackage); }
inline void LMSetHelpPackage(Handle val) { LM(HelpPackage) = val; }
NOTRAP_FUNCTION2(LMGetHelpPackage);
NOTRAP_FUNCTION2(LMSetHelpPackage);


// MacPhoenix (name is a guess): PACK 14's selector -4, which 7.5.5's
// SystemTask and OSEventAvail patches call while balloons are on and
// the current process is the front one; it shows and removes the
// balloons for windows and dialog items. Selector words put the
// parameter size in words in the high byte, so the negative selectors
// are their low byte only.
OSErr C_HMIdle();PASCAL_SUBTRAP(HMIdle, 0xA830, 0x00FC, Pack14);


Boolean C_HMGetBalloons();PASCAL_SUBTRAP(HMGetBalloons, 0xA830, 0x0003, Pack14);


OSErr C_HMSetBalloons(Boolean flag);PASCAL_SUBTRAP(HMSetBalloons, 0xA830, 0x0104, Pack14);


Boolean C_HMIsBalloon();PASCAL_SUBTRAP(HMIsBalloon, 0xA830, 0x0007, Pack14);


OSErr C_HMShowBalloon(HMMessageRecord *msgp, Point tip, RectPtr alternaterectp, Ptr tipprocptr, INTEGER proc, INTEGER variant, INTEGER method);PASCAL_SUBTRAP(HMShowBalloon, 0xA830, 0x0B01, Pack14);


OSErr C_HMShowMenuBalloon(INTEGER item, INTEGER menuid, LONGINT flags, LONGINT itemreserved, Point tip, RectPtr alternaterectp, Ptr tipproc, INTEGER proc, INTEGER variant);PASCAL_SUBTRAP(HMShowMenuBalloon, 0xA830, 0x0E05, Pack14);


OSErr C_HMRemoveBalloon();PASCAL_SUBTRAP(HMRemoveBalloon, 0xA830, 0x0002, Pack14);


OSErr C_HMGetHelpMenuHandle(GUEST<MenuHandle> *mhp);PASCAL_SUBTRAP(HMGetHelpMenuHandle, 0xA830, 0x0200, Pack14);


OSErr C_HMGetFont(GUEST<INTEGER> *fontp);PASCAL_SUBTRAP(HMGetFont, 0xA830, 0x020A, Pack14);


OSErr C_HMGetFontSize(GUEST<INTEGER> *sizep);PASCAL_SUBTRAP(HMGetFontSize, 0xA830, 0x020B, Pack14);


OSErr C_HMSetFont(INTEGER font);PASCAL_SUBTRAP(HMSetFont, 0xA830, 0x0108, Pack14);


OSErr C_HMSetFontSize(INTEGER size);PASCAL_SUBTRAP(HMSetFontSize, 0xA830, 0x0109, Pack14);


OSErr C_HMSetDialogResID(INTEGER resid);PASCAL_SUBTRAP(HMSetDialogResID, 0xA830, 0x010C, Pack14);


OSErr C_HMGetDialogResID(GUEST<INTEGER> *residp);PASCAL_SUBTRAP(HMGetDialogResID, 0xA830, 0x0213, Pack14);


OSErr C_HMSetMenuResID(INTEGER menuid, INTEGER resid);PASCAL_SUBTRAP(HMSetMenuResID, 0xA830, 0x020D, Pack14);


OSErr C_HMGetMenuResID(GUEST<INTEGER> *menuidp, GUEST<INTEGER> *residp);PASCAL_SUBTRAP(HMGetMenuResID, 0xA830, 0x0314, Pack14);


OSErr C_HMScanTemplateItems(INTEGER whichid, INTEGER whicresfile, ResType whictype);PASCAL_SUBTRAP(HMScanTemplateItems, 0xA830, 0x0410, Pack14);


OSErr C_HMBalloonRect(HMMessageRecord *messp, Rect *rectp);PASCAL_SUBTRAP(HMBalloonRect, 0xA830, 0x040E, Pack14);


OSErr C_HMBalloonPict(HMMessageRecord *messp, GUEST<PicHandle> *pictp);PASCAL_SUBTRAP(HMBalloonPict, 0xA830, 0x040F, Pack14);


OSErr C_HMGetBalloonWindow(GUEST<WindowPtr> *windowpp);PASCAL_SUBTRAP(HMGetBalloonWindow, 0xA830, 0x0215, Pack14);


OSErr C_HMExtractHelpMsg(ResType type, INTEGER resid, INTEGER msg, INTEGER state, HMMessageRecord *helpmsgp);PASCAL_SUBTRAP(HMExtractHelpMsg, 0xA830, 0x0711, Pack14);


OSErr C_HMGetIndHelpMsg(ResType type, INTEGER resid, INTEGER msg, INTEGER state, GUEST<LONGINT> *options, Point tip, Rect *altrectp, GUEST<INTEGER> *theprocp, GUEST<INTEGER> *variantp, HMMessageRecord *helpmsgp, GUEST<INTEGER> *count);PASCAL_SUBTRAP(HMGetIndHelpMsg, 0xA830, 0x1306, Pack14);


} /* namespace Executor */