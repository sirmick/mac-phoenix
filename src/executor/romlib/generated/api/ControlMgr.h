/*****************************************************************************
 *                                                                           *
 *                                ControlMgr.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     WindowMgr.h    ControlHandle, WindowPtr, ControlPtr                   *
 *     MacTypes.h     Rect, Byte, INTEGER, Handle, LONGINT, Str255,          *
 *                    ConstStringPtr, StringPtr, Boolean                     *
 *     QuickDraw.h    cSpecArray, RgnHandle                                  *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "WindowMgr.h"
#include "ExMacTypes.h"
#include "QuickDraw.h"

#define MODULE_NAME ControlMgr
#include <base/api-module.h>


namespace Executor {

enum {pushButProc = 0,
checkBoxProc = 1,
radioButProc = 2,
useWFont = 8,
scrollBarProc = 16,
};

enum {inButton = 10,
inCheckBox = 11,
inUpButton = 20,
inDownButton = 21,
inPageUp = 22,
inPageDown = 23,
inThumb = 129,
};

enum {popupFixedWidth = 1 << 0,
popupUseAddResMenu = 1 << 2,
popupUseWFont = 1 << 3,
};

enum {popupTitleBold = 1 << 8,
popupTitleItalic = 1 << 9,
popupTitleUnderline = 1 << 10,
popupTitleOutline = 1 << 11,
popupTitleShadow = 1 << 12,
popupTitleCondense = 1 << 13,
popupTitleExtend = 1 << 14,
popupTitleNoStyle = 1 << 15,
};

enum {popupTitleLeftJust = 0,
popupTitleCenterJust = 1,
popupTitleRightJust = 255,
};

enum {drawCntl = 0,
testCntl = 1,
calcCRgns = 2,
initCntl = 3,
dispCntl = 4,
posCntl = 5,
thumbCntl = 6,
dragCntl = 7,
autoTrack = 8,
};

enum {calcCntlRgn = 10,
calcThumbRgn = 11,
};

// control color table parts
enum {cFrameColor = 0,
cBodyColor = 1,
cTextColor = 2,
cThumbColor = 3,
};

enum {cArrowsColorLight = 5,
cArrowsColorDark = 6,
cThumbLight = 7,
cThumbDark = 8,
cHiliteLight = 9,
cHiliteDark = 10,
cTitleBarLight = 11,
cTitleBarDark = 12,
cTingeLight = 13,
cTingeDark = 14,
};

using ControlActionUPP = UPP<void(ControlHandle , int16_t )>;


struct ControlRecord{GUEST_STRUCT;GUEST<ControlHandle> nextControl;GUEST<WindowPtr> contrlOwner;Rect contrlRect;Byte contrlVis;Byte contrlHilite;GUEST<INTEGER> contrlValue;GUEST<INTEGER> contrlMin;GUEST<INTEGER> contrlMax;GUEST<Handle> contrlDefProc;GUEST<Handle> contrlData;GUEST<ControlActionUPP> contrlAction;GUEST<LONGINT> contrlRfCon;GUEST<Str255> contrlTitle;};static_assert(sizeof(ControlRecord) == 296);void describeStruct(const ControlRecord&, std::ostream&);

struct CtlCTab{GUEST_STRUCT;GUEST<LONGINT> ccSeed;GUEST<INTEGER> ccReserved;GUEST<INTEGER> ctSize;GUEST<cSpecArray> ctTable;};static_assert(sizeof(CtlCTab) == 16);void describeStruct(const CtlCTab&, std::ostream&);

typedef CtlCTab *CCTabPtr;

typedef GUEST<CCTabPtr> *CCTabHandle;

struct AuxCtlRec;

typedef AuxCtlRec *AuxCtlPtr;

typedef GUEST<AuxCtlPtr> *AuxCtlHandle;

struct AuxCtlRec{GUEST_STRUCT;GUEST<AuxCtlHandle> acNext;GUEST<ControlHandle> acOwner;GUEST<CCTabHandle> acCTable;GUEST<INTEGER> acFlags;GUEST<LONGINT> acReserved;GUEST<LONGINT> acRefCon;};static_assert(sizeof(AuxCtlRec) == 22);void describeStruct(const AuxCtlRec&, std::ostream&);

// ControlMgr IMV-216 (true);
const LowMemGlobal<AuxCtlHandle > AuxCtlHead{ 0xCD4 };
inline AuxCtlHandle  LMGetAuxCtlHead() { return LM(AuxCtlHead); }
inline void LMSetAuxCtlHead(AuxCtlHandle val) { LM(AuxCtlHead) = val; }
NOTRAP_FUNCTION2(LMGetAuxCtlHead);
NOTRAP_FUNCTION2(LMSetAuxCtlHead);


void C_SetControlTitle(ControlHandle c, ConstStringPtr t);PASCAL_TRAP(SetControlTitle, 0xA95F);


void C_GetControlTitle(ControlHandle c, StringPtr t);PASCAL_TRAP(GetControlTitle, 0xA95E);


void C_HideControl(ControlHandle c);PASCAL_TRAP(HideControl, 0xA958);


void C_ShowControl(ControlHandle c);PASCAL_TRAP(ShowControl, 0xA957);


void C_HiliteControl(ControlHandle c, INTEGER state);PASCAL_TRAP(HiliteControl, 0xA95D);


void C_DrawControls(WindowPtr w);PASCAL_TRAP(DrawControls, 0xA969);


void C_Draw1Control(ControlHandle c);PASCAL_TRAP(Draw1Control, 0xA96D);


void C_UpdateControls(WindowPtr wp, RgnHandle rh);PASCAL_TRAP(UpdateControls, 0xA953);


ControlHandle C_NewControl(WindowPtr wst, const Rect *r, ConstStringPtr title, Boolean vis, INTEGER value, INTEGER min, INTEGER max, INTEGER procid, LONGINT rc);PASCAL_TRAP(NewControl, 0xA954);


ControlHandle C_GetNewControl(INTEGER cid, WindowPtr wst);PASCAL_TRAP(GetNewControl, 0xA9BE);


void C_DisposeControl(ControlHandle c);PASCAL_TRAP(DisposeControl, 0xA955);


void C_KillControls(WindowPtr w);PASCAL_TRAP(KillControls, 0xA956);


void C_SetControlReference(ControlHandle c, LONGINT data);PASCAL_TRAP(SetControlReference, 0xA95B);


LONGINT C_GetControlReference(ControlHandle c);PASCAL_TRAP(GetControlReference, 0xA95A);


void C_SetControlAction(ControlHandle c, ControlActionUPP a);PASCAL_TRAP(SetControlAction, 0xA96B);


ControlActionUPP C_GetControlAction(ControlHandle c);PASCAL_TRAP(GetControlAction, 0xA96A);


INTEGER C_GetControlVariant(ControlHandle c);PASCAL_TRAP(GetControlVariant, 0xA809);


Boolean C_GetAuxiliaryControlRecord(ControlHandle c, GUEST<AuxCtlHandle> *acHndl);PASCAL_TRAP(GetAuxiliaryControlRecord, 0xAA44);


INTEGER C_FindControl(Point p, WindowPtr w, GUEST<ControlHandle> *cp);PASCAL_TRAP(FindControl, 0xA96C);


INTEGER C_TrackControl(ControlHandle c, Point p, ControlActionUPP a);PASCAL_TRAP(TrackControl, 0xA968);


INTEGER C_TestControl(ControlHandle c, Point p);PASCAL_TRAP(TestControl, 0xA966);


void C_SetControlValue(ControlHandle c, INTEGER v);PASCAL_TRAP(SetControlValue, 0xA963);


INTEGER C_GetControlValue(ControlHandle c);PASCAL_TRAP(GetControlValue, 0xA960);


void C_SetControlMinimum(ControlHandle c, INTEGER v);PASCAL_TRAP(SetControlMinimum, 0xA964);


INTEGER C_GetControlMinimum(ControlHandle c);PASCAL_TRAP(GetControlMinimum, 0xA961);


void C_SetControlMaximum(ControlHandle c, INTEGER v);PASCAL_TRAP(SetControlMaximum, 0xA965);


INTEGER C_GetControlMaximum(ControlHandle c);PASCAL_TRAP(GetControlMaximum, 0xA962);


void C_MoveControl(ControlHandle c, INTEGER h, INTEGER v);PASCAL_TRAP(MoveControl, 0xA959);


void C_DragControl(ControlHandle c, Point p, const Rect *limit, const Rect *slop, INTEGER axis);PASCAL_TRAP(DragControl, 0xA967);


void C_SizeControl(ControlHandle c, INTEGER width, INTEGER height);PASCAL_TRAP(SizeControl, 0xA95C);


void C_SetControlColor(ControlHandle ctl, CCTabHandle ctab);PASCAL_TRAP(SetControlColor, 0xAA43);


typedef ControlPtr ControlRef;

} /* namespace Executor */