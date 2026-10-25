/*****************************************************************************
 *                                                                           *
 *                                WindowMgr.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     QuickDraw.h    GrafPtr, CGrafPtr, GrafPort, RgnHandle, PicHandle,     *
 *                    CTabHandle, PixPatHandle                               *
 *     MacTypes.h     INTEGER, Boolean, Handle, StringHandle, LONGINT,       *
 *                    Rect, ConstStringPtr, StringPtr, OSErr, SignedByte     *
 *     EventMgr.h     EventRecord                                            *
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
#include "EventMgr.h"

#define MODULE_NAME WindowMgr
#include <base/api-module.h>


namespace Executor {

enum {documentProc = 0,
dBoxProc = 1,
plainDBox = 2,
altDBoxProc = 3,
noGrowDocProc = 4,
movableDBoxProc = 5,
rDocProc = 16,
};

enum {dialogKind = 2,
userKind = 8,
};

enum {inDesk = 0,
inMenuBar = 1,
inSysWindow = 2,
inContent = 3,
inDrag = 4,
inGrow = 5,
inGoAway = 6,
};

enum {noConstraint = 0,
hAxisOnly = 1,
vAxisOnly = 2,
};

enum {wDraw = 0,
wHit = 1,
wCalcRgns = 2,
wNew = 3,
wDispose = 4,
wGrow = 5,
wDrawGIcon = 6,
};

enum {kWindowMsgDraw = 0,
kWindowMsgHitTest = 1,
kWindowMsgCalculateShape = 2,
kWindowMsgInitialize = 3,
kWindowMsgCleanUp = 4,
kWindowMsgDrawGrowOutline = 5,
kWindowMsgDrawGrowBox = 6,
};

enum {wNoHit = 0,
wInContent = 1,
wInDrag = 2,
wInGrow = 3,
wInGoAway = 4,
};

// color table entries
enum {wContentColor = 0,
wFrameColor = 1,
wTextColor = 2,
wHiliteColor = 3,
wTitleBarColor = 4,
wHiliteColorLight = 5,
wHiliteColorDark = 6,
wTitleBarLight = 7,
wTitleBarDark = 8,
wDialogLight = 9,
wDialogDark = 10,
wTingeLight = 11,
wTingeDark = 12,
};

enum {deskPatID = 16,
};

typedef GrafPtr WindowPtr;

typedef CGrafPtr CWindowPtr;

struct ControlRecord;

typedef ControlRecord *ControlPtr;

typedef GUEST<ControlPtr> *ControlHandle;

struct WindowRecord{GUEST_STRUCT;GrafPort port;GUEST<INTEGER> windowKind;Boolean visible;Boolean hilited;Boolean goAwayFlag;Boolean spareFlag;GUEST<RgnHandle> strucRgn;GUEST<RgnHandle> contRgn;GUEST<RgnHandle> updateRgn;GUEST<Handle> windowDefProc;GUEST<Handle> dataHandle;GUEST<StringHandle> titleHandle;GUEST<INTEGER> titleWidth;GUEST<ControlHandle> controlList;GUEST<WindowRecord*> nextWindow;GUEST<PicHandle> windowPic;GUEST<LONGINT> refCon;};static_assert(sizeof(WindowRecord) == 156);void describeStruct(const WindowRecord&, std::ostream&);

typedef WindowRecord *WindowPeek;

struct WStateData{GUEST_STRUCT;Rect userState;Rect stdState;};static_assert(sizeof(WStateData) == 16);void describeStruct(const WStateData&, std::ostream&);

enum {inZoomIn = 7,
inZoomOut = 8,
};

enum {wInZoomIn = 5,
wInZoomOut = 6,
};

struct AuxWinRec;

typedef AuxWinRec *AuxWinPtr;

typedef GUEST<AuxWinPtr> *AuxWinHandle;

struct AuxWinRec{GUEST_STRUCT;GUEST<AuxWinHandle> awNext;GUEST<WindowPtr> awOwner;GUEST<CTabHandle> awCTable;GUEST<Handle> dialogCItem;GUEST<LONGINT> awFlags;GUEST<CTabHandle> awReserved;GUEST<LONGINT> awRefCon;};static_assert(sizeof(AuxWinRec) == 28);void describeStruct(const AuxWinRec&, std::ostream&);

using WindowDefUPP = UPP<LONGINT(INTEGER varcode, WindowPtr wp, INTEGER message, LONGINT param)>;


// WindowMgr IMI-274 (true);
const LowMemGlobal<WindowPeek > WindowList{ 0x9D6 };
inline WindowPeek  LMGetWindowList() { return LM(WindowList); }
inline void LMSetWindowList(WindowPeek val) { LM(WindowList) = val; }
NOTRAP_FUNCTION2(LMGetWindowList);
NOTRAP_FUNCTION2(LMSetWindowList);


// WindowMgr IMI-297 (true);
const LowMemGlobal<INTEGER > SaveUpdate{ 0x9DA };
inline INTEGER  LMGetSaveUpdate() { return LM(SaveUpdate); }
inline void LMSetSaveUpdate(INTEGER val) { LM(SaveUpdate) = val; }
NOTRAP_FUNCTION2(LMGetSaveUpdate);
NOTRAP_FUNCTION2(LMSetSaveUpdate);


// WindowMgr IMI-297 (true);
const LowMemGlobal<INTEGER > PaintWhite{ 0x9DC };
inline INTEGER  LMGetPaintWhite() { return LM(PaintWhite); }
inline void LMSetPaintWhite(INTEGER val) { LM(PaintWhite) = val; }
NOTRAP_FUNCTION2(LMGetPaintWhite);
NOTRAP_FUNCTION2(LMSetPaintWhite);


// WindowMgr IMI-282 (true);
const LowMemGlobal<GrafPtr > WMgrPort{ 0x9DE };
inline GrafPtr  LMGetWMgrPort() { return LM(WMgrPort); }
inline void LMSetWMgrPort(GrafPtr val) { LM(WMgrPort) = val; }
NOTRAP_FUNCTION2(LMGetWMgrPort);
NOTRAP_FUNCTION2(LMSetWMgrPort);


// QuickDraw IMV-205 (false);
const LowMemGlobal<CGrafPtr > WMgrCPort{ 0xD2C };
inline CGrafPtr  LMGetWMgrCPort() { return LM(WMgrCPort); }
inline void LMSetWMgrCPort(CGrafPtr val) { LM(WMgrCPort) = val; }
NOTRAP_FUNCTION2(LMGetWMgrCPort);
NOTRAP_FUNCTION2(LMSetWMgrCPort);


// WindowMgr IMI-296 (true);
const LowMemGlobal<RgnHandle > OldStructure{ 0x9E6 };
inline RgnHandle  LMGetOldStructure() { return LM(OldStructure); }
inline void LMSetOldStructure(RgnHandle val) { LM(OldStructure) = val; }
NOTRAP_FUNCTION2(LMGetOldStructure);
NOTRAP_FUNCTION2(LMSetOldStructure);


// WindowMgr IMI-296 (true);
const LowMemGlobal<RgnHandle > OldContent{ 0x9EA };
inline RgnHandle  LMGetOldContent() { return LM(OldContent); }
inline void LMSetOldContent(RgnHandle val) { LM(OldContent) = val; }
NOTRAP_FUNCTION2(LMGetOldContent);
NOTRAP_FUNCTION2(LMSetOldContent);


// WindowMgr IMI-282 (true);
const LowMemGlobal<RgnHandle > GrayRgn{ 0x9EE };
inline RgnHandle  LMGetGrayRgn() { return LM(GrayRgn); }
inline void LMSetGrayRgn(RgnHandle val) { LM(GrayRgn) = val; }
NOTRAP_FUNCTION2(LMGetGrayRgn);
NOTRAP_FUNCTION2(LMSetGrayRgn);


// WindowMgr IMI-293 (true);
const LowMemGlobal<RgnHandle > SaveVisRgn{ 0x9F2 };
inline RgnHandle  LMGetSaveVisRgn() { return LM(SaveVisRgn); }
inline void LMSetSaveVisRgn(RgnHandle val) { LM(SaveVisRgn) = val; }
NOTRAP_FUNCTION2(LMGetSaveVisRgn);
NOTRAP_FUNCTION2(LMSetSaveVisRgn);


// WindowMgr IMI-324 (true);
const LowMemGlobal<ProcPtr > DragHook{ 0x9F6 };
inline ProcPtr  LMGetDragHook() { return LM(DragHook); }
inline void LMSetDragHook(ProcPtr val) { LM(DragHook) = val; }
NOTRAP_FUNCTION2(LMGetDragHook);
NOTRAP_FUNCTION2(LMSetDragHook);


// WindowMgr IMI-324 (true);
const LowMemGlobal<Pattern > DragPattern{ 0xA34 };
inline void LMGetDragPattern(Pattern *val) { *val = LM(DragPattern); }
inline void LMSetDragPattern(const Pattern *val) { LM(DragPattern) = *val; }
NOTRAP_FUNCTION2(LMGetDragPattern);
NOTRAP_FUNCTION2(LMSetDragPattern);


// WindowMgr IMI-282 (true);
const LowMemGlobal<Pattern > DeskPattern{ 0xA3C };
inline void LMGetDeskPattern(Pattern *val) { *val = LM(DeskPattern); }
inline void LMSetDeskPattern(const Pattern *val) { LM(DeskPattern) = *val; }
NOTRAP_FUNCTION2(LMGetDeskPattern);
NOTRAP_FUNCTION2(LMSetDeskPattern);


// WindowMgr IMI-280 (true);
const LowMemGlobal<WindowPtr > CurActivate{ 0xA64 };
inline WindowPtr  LMGetCurActivate() { return LM(CurActivate); }
inline void LMSetCurActivate(WindowPtr val) { LM(CurActivate) = val; }
NOTRAP_FUNCTION2(LMGetCurActivate);
NOTRAP_FUNCTION2(LMSetCurActivate);


// WindowMgr IMI-280 (true);
const LowMemGlobal<WindowPtr > CurDeactive{ 0xA68 };
inline WindowPtr  LMGetCurDeactive() { return LM(CurDeactive); }
inline void LMSetCurDeactive(WindowPtr val) { LM(CurDeactive) = val; }
NOTRAP_FUNCTION2(LMGetCurDeactive);
NOTRAP_FUNCTION2(LMSetCurDeactive);


// WindowMgr IMI-282 (true);
const LowMemGlobal<ProcPtr > DeskHook{ 0xA6C };
inline ProcPtr  LMGetDeskHook() { return LM(DeskHook); }
inline void LMSetDeskHook(ProcPtr val) { LM(DeskHook) = val; }
NOTRAP_FUNCTION2(LMGetDeskHook);
NOTRAP_FUNCTION2(LMSetDeskHook);


// MacPhoenix. The desktop's port, which 7.5.5's Layer Manager keeps as
// the desktop layer's window record (process.cpp, TakeDesktopLayer).
// Apple's Drag Manager hands it to the drop target's handlers as the
// window when the drop is on the desktop.
const LowMemGlobal<GrafPtr > DeskPort{ 0x9E2 };
inline GrafPtr  LMGetDeskPort() { return LM(DeskPort); }
inline void LMSetDeskPort(GrafPtr val) { LM(DeskPort) = val; }
NOTRAP_FUNCTION2(LMGetDeskPort);
NOTRAP_FUNCTION2(LMSetDeskPort);


// WindowMgr IMI-287 (true);
const LowMemGlobal<WindowPtr > GhostWindow{ 0xA84 };
inline WindowPtr  LMGetGhostWindow() { return LM(GhostWindow); }
inline void LMSetGhostWindow(WindowPtr val) { LM(GhostWindow) = val; }
NOTRAP_FUNCTION2(LMGetGhostWindow);
NOTRAP_FUNCTION2(LMSetGhostWindow);


// WindowMgr IMV-200 (true);
const LowMemGlobal<AuxWinHandle > AuxWinHead{ 0xCD0 };
inline AuxWinHandle  LMGetAuxWinHead() { return LM(AuxWinHead); }
inline void LMSetAuxWinHead(AuxWinHandle val) { LM(AuxWinHead) = val; }
NOTRAP_FUNCTION2(LMGetAuxWinHead);
NOTRAP_FUNCTION2(LMSetAuxWinHead);


// WindowMgr SysEqua.a (true);
const LowMemGlobal<PixPatHandle > DeskCPat{ 0xCD8 };
inline PixPatHandle  LMGetDeskCPat() { return LM(DeskCPat); }
inline void LMSetDeskCPat(PixPatHandle val) { LM(DeskCPat) = val; }
NOTRAP_FUNCTION2(LMGetDeskCPat);
NOTRAP_FUNCTION2(LMSetDeskCPat);


BEGIN_EXECUTOR_ONLY
inline RgnHandle GetGrayRgn() { return LM(GrayRgn); }
NOTRAP_FUNCTION2(GetGrayRgn);
END_EXECUTOR_ONLY


void C_SetWTitle(WindowPtr w, ConstStringPtr t);PASCAL_TRAP(SetWTitle, 0xA91A);


void C_GetWTitle(WindowPtr w, StringPtr t);PASCAL_TRAP(GetWTitle, 0xA919);


WindowPtr C_FrontWindow();PASCAL_TRAP(FrontWindow, 0xA924);


void C_HiliteWindow(WindowPtr w, Boolean flag);PASCAL_TRAP(HiliteWindow, 0xA91C);


void C_BringToFront(WindowPtr w);PASCAL_TRAP(BringToFront, 0xA920);


void C_SelectWindow(WindowPtr w);PASCAL_TRAP(SelectWindow, 0xA91F);


void C_ShowHide(WindowPtr w, Boolean flag);PASCAL_TRAP(ShowHide, 0xA908);


void C_HideWindow(WindowPtr w);PASCAL_TRAP(HideWindow, 0xA916);


void C_ShowWindow(WindowPtr w);PASCAL_TRAP(ShowWindow, 0xA915);


void C_SendBehind(WindowPtr w, WindowPtr behind);PASCAL_TRAP(SendBehind, 0xA921);


void C_DrawGrowIcon(WindowPtr w);PASCAL_TRAP(DrawGrowIcon, 0xA904);


void C_InitWindows();PASCAL_TRAP(InitWindows, 0xA912);


void C_GetWMgrPort(GUEST<GrafPtr> *wp);PASCAL_TRAP(GetWMgrPort, 0xA910);


WindowPtr C_NewWindow(void *wst, const Rect *r, ConstStringPtr title, Boolean vis, INTEGER procid, WindowPtr behind, Boolean gaflag, LONGINT rc);PASCAL_TRAP(NewWindow, 0xA913);


WindowPtr C_GetNewWindow(INTEGER wid, void *wst, WindowPtr behind);PASCAL_TRAP(GetNewWindow, 0xA9BD);


void C_CloseWindow(WindowPtr w);PASCAL_TRAP(CloseWindow, 0xA92D);


WindowPtr C_NewCWindow(void *storage, const Rect *, ConstStringPtr , Boolean , INTEGER , WindowPtr , Boolean , LONGINT );PASCAL_TRAP(NewCWindow, 0xAA45);


WindowPtr C_GetNewCWindow(INTEGER , void *, WindowPtr );PASCAL_TRAP(GetNewCWindow, 0xAA46);


void C_DisposeWindow(WindowPtr w);PASCAL_TRAP(DisposeWindow, 0xA914);


void C_SetWRefCon(WindowPtr w, LONGINT data);PASCAL_TRAP(SetWRefCon, 0xA918);


LONGINT C_GetWRefCon(WindowPtr w);PASCAL_TRAP(GetWRefCon, 0xA917);


void C_SetWindowPic(WindowPtr w, PicHandle p);PASCAL_TRAP(SetWindowPic, 0xA92E);


PicHandle C_GetWindowPic(WindowPtr w);PASCAL_TRAP(GetWindowPic, 0xA92F);


LONGINT C_PinRect(const Rect *r, Point p);PASCAL_TRAP(PinRect, 0xA94E);


LONGINT C_DragTheRgn(RgnHandle rgn, Point startp, const Rect *limit, const Rect *slop, INTEGER axis, ProcPtr proc);PASCAL_TRAP(DragTheRgn, 0xA926);


LONGINT C_DragGrayRgn(RgnHandle rgn, Point startp, const Rect *limit, const Rect *slop, INTEGER axis, ProcPtr proc);PASCAL_TRAP(DragGrayRgn, 0xA905);


void C_ClipAbove(WindowPeek w);PASCAL_TRAP(ClipAbove, 0xA90B);


Boolean C_CheckUpdate(EventRecord *ev);PASCAL_TRAP(CheckUpdate, 0xA911);


void C_SaveOld(WindowPeek w);PASCAL_TRAP(SaveOld, 0xA90E);


void C_PaintOne(WindowPeek w, RgnHandle clobbered);PASCAL_TRAP(PaintOne, 0xA90C);


void C_PaintBehind(WindowPeek w, RgnHandle clobbered);PASCAL_TRAP(PaintBehind, 0xA90D);


void C_CalcVis(WindowPeek w);PASCAL_TRAP(CalcVis, 0xA909);


void C_CalcVisBehind(WindowPeek w, RgnHandle clobbered);PASCAL_TRAP(CalcVisBehind, 0xA90A);


void C_DrawNew(WindowPeek w, Boolean flag);PASCAL_TRAP(DrawNew, 0xA90F);


INTEGER C_GetWVariant(WindowPtr w);PASCAL_TRAP(GetWVariant, 0xA80A);


INTEGER C_FindWindow(Point p, GUEST<WindowPtr> *wpp);PASCAL_TRAP(FindWindow, 0xA92C);


Boolean C_TrackBox(WindowPtr wp, Point pt, INTEGER part);PASCAL_TRAP(TrackBox, 0xA83B);


Boolean C_TrackGoAway(WindowPtr w, Point p);PASCAL_TRAP(TrackGoAway, 0xA91E);


void C_ZoomWindow(WindowPtr wp, INTEGER part, Boolean front);PASCAL_TRAP(ZoomWindow, 0xA83A);


void C_MoveWindow(WindowPtr wp, INTEGER h, INTEGER v, Boolean front);PASCAL_TRAP(MoveWindow, 0xA91B);


void C_DragWindow(WindowPtr wp, Point p, const Rect *rp);PASCAL_TRAP(DragWindow, 0xA925);


LONGINT C_GrowWindow(WindowPtr w, Point startp, const Rect *rp);PASCAL_TRAP(GrowWindow, 0xA92B);


void C_SizeWindow(WindowPtr w, INTEGER width, INTEGER height, Boolean flag);PASCAL_TRAP(SizeWindow, 0xA91D);


void C_InvalRect(const Rect *r);PASCAL_TRAP(InvalRect, 0xA928);


void C_InvalRgn(RgnHandle r);PASCAL_TRAP(InvalRgn, 0xA927);


void C_ValidRect(const Rect *r);PASCAL_TRAP(ValidRect, 0xA92A);


void C_ValidRgn(RgnHandle r);PASCAL_TRAP(ValidRgn, 0xA929);


void C_BeginUpdate(WindowPtr w);PASCAL_TRAP(BeginUpdate, 0xA922);


void C_EndUpdate(WindowPtr w);PASCAL_TRAP(EndUpdate, 0xA923);


void C_SetWinColor(WindowPtr w, CTabHandle new_w_ctab);PASCAL_TRAP(SetWinColor, 0xAA41);


void C_SetDeskCPat(PixPatHandle );PASCAL_TRAP(SetDeskCPat, 0xAA47);


Boolean C_GetAuxWin(WindowPtr , GUEST<AuxWinHandle> *);PASCAL_TRAP(GetAuxWin, 0xAA42);


typedef WindowPtr WindowRef;



// MacPhoenix: 7.5.5's Layer Manager (System 'scod' -16463; its table at
// the trap's address, kind 2, selectors -10..13). A layer is a window
// record ($4A = $DEAD) whose +$94 is its window list; the current one's
// lists are WindowList, AuxWinHead and AuxCtlHead (low memory $A90 is
// the current layer, $9E2 the root). The selectors below are the ones
// Apple's Help Manager uses for its balloon window's floating layer
// (process.cpp); their names are guesses.
DISPATCHER_TRAP(LayerDispatch, 0xA829, D0<0xFF>);


// Selector -1: the top of the current layer's parent chain. The Help
// Manager wants its +$94 (first child) non-nil before it makes its layer.
WindowPtr C_GetRootLayer();PASCAL_SUBTRAP(GetRootLayer, 0xA829, 0x00FF, LayerDispatch);


// Selector -5: the desktop layer, low memory $9E2 (the record
// TakeDesktopLayer gave the Finder, whose desktop window shares its
// visRgn and updateRgn), by reference. The Help Manager tells the
// desktop window by that updateRgn.
OSErr C_GetDesktopLayer(GUEST<WindowPtr> *layer);PASCAL_SUBTRAP(GetDesktopLayer, 0xA829, 0x02FB, LayerDispatch);


// Selector -7: the layer a window is in; a layer's parent; nil: the
// current layer.
WindowPtr C_GetWindowLayer(WindowPtr window);PASCAL_SUBTRAP(GetWindowLayer, 0xA829, 0x02F9, LayerDispatch);


// MacPhoenix (name is a guess). Selector 6: a layer's first child: the
// root's front layer, a process layer's front window. Apple's Drag
// Manager walks a layer's windows from it (nextWindow) to find the
// one under the mouse (System 'proc' -16513 +$4342).
WindowPtr C_GetLayerWindowList(WindowPtr layer);PASCAL_SUBTRAP(GetLayerWindowList, 0xA829, 0x0206, LayerDispatch);


// MacPhoenix (name is a guess). Selector 7: FindWindow in the current
// layer. With the root current, Apple's answers the child layer at the
// point (inContent, 3), and the caller makes it current and asks again
// to descend (Drag Manager, 'proc' -16513 +$43d2; the Process Manager's
// own GetLayerAtPoint). Here too.
int16_t C_FindWindowInLayer(Point pt, GUEST<GUEST<WindowPtr>> *window);PASCAL_SUBTRAP(FindWindowInLayer, 0xA829, 0x0407, LayerDispatch);


// Selector 1. Apple's makes the layer a window of WDEF 127 in the
// current layer (behind as NewWindow's: -1 in front); the Help Manager
// makes its balloon layer in the root with refCon $BADFACE. Here a
// floating layer, in front of every process.
OSErr C_NewLayer(GUEST<WindowPtr> *layer, Boolean visible, Boolean flag, WindowPtr behind, int32_t refcon);PASCAL_SUBTRAP(NewLayer, 0xA829, 0x0801, LayerDispatch);


Boolean C_IsLayer(WindowPtr window);PASCAL_SUBTRAP(IsLayer, 0xA829, 0x0202, LayerDispatch);


WindowPtr C_GetCurLayer();PASCAL_SUBTRAP(GetCurLayer, 0xA829, 0x0003, LayerDispatch);


// Selector 4: WindowList, AuxWinHead and AuxCtlHead become the layer's
// (nil: the root).
void C_SetCurLayer(WindowPtr layer);PASCAL_SUBTRAP(SetCurLayer, 0xA829, 0x0204, LayerDispatch);


// Selector 5: SetCurLayer, answering the layer that was current.
WindowPtr C_SwapCurLayer(WindowPtr layer);PASCAL_SUBTRAP(SwapCurLayer, 0xA829, 0x0205, LayerDispatch);


// Key Caps calls it with (window, false, 1, 2) when CheckWindow says the
// window it restored is off screen.  We centre it on the main screen.
void C_AutoPositionWindow(WindowPtr window, Boolean arg1, SignedByte arg2, SignedByte arg3);PASCAL_SUBTRAP(AutoPositionWindow, 0xA829, 0x0509, LayerDispatch);


// Key Caps calls it with (window, false, 1) on the position it restored
// from its 'kcwn' resource.  True when the window can be reached: its
// title bar (entire: the whole window) is on the desktop.
Boolean C_CheckWindow(WindowPtr window, Boolean entire, SignedByte kind);PASCAL_SUBTRAP(CheckWindow, 0xA829, 0x040C, LayerDispatch);


// Key Caps saves the result in its 'kcwn' resource and reads it back as
// the window's global content rectangle.
void C_GetWindowContentRect(WindowPtr window, Rect *r);PASCAL_SUBTRAP(GetWindowContentRect, 0xA829, 0x04F6, LayerDispatch);


} /* namespace Executor */