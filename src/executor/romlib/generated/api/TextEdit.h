/*****************************************************************************
 *                                                                           *
 *                                 TextEdit.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     Rect, INTEGER, LONGINT, Handle, Byte, Ptr,             *
 *                    CharParameter, Boolean, OSErr                          *
 *     QuickDraw.h    Style, GrafPtr, RGBColor, RgnHandle                    *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "QuickDraw.h"

#define MODULE_NAME TextEdit
#include <base/api-module.h>


namespace Executor {

// new justification defines, accepted by `TESetAlignment ()' and
//    `TETextBox ()'
enum {teFlushDefault = 0,
teCenter = 1,
teFlushRight = -1,
teFlushLeft = -2,
};

// older justification defines
enum {teJustLeft = 0,
teJustCenter = 1,
teJustRight = -1,
teForceLeft = -2,
};

enum {doFont = 1,
doFace = 2,
doSize = 4,
doColor = 8,
doAll = 15,
addSize = 16,
doToggle = 32,
};

enum {teFind = 0,
teHilite = 1,
teDraw = -1,
teCaret = -2,
};

enum {caret_vis = -1,
caret_invis = 255,
hilite_vis = 0,
};

enum {teFAutoScroll = 0,
teFTextBuffering = 1,
teFOutlineHilite = 2,
teFInlineInput = 3,
teFUseTextServices = 4,
};

enum {teBitClear = 0,
teBitSet = 1,
teBitTest = -1,
};

struct TERec{GUEST_STRUCT;Rect destRect;Rect viewRect;Rect selRect;GUEST<INTEGER> lineHeight;GUEST<INTEGER> fontAscent;GUEST<Point> selPoint;GUEST<INTEGER> selStart;GUEST<INTEGER> selEnd;GUEST<INTEGER> active;GUEST<ProcPtr> wordBreak;GUEST<ProcPtr> clikLoop;GUEST<LONGINT> clickTime;GUEST<INTEGER> clickLoc;GUEST<LONGINT> caretTime;GUEST<INTEGER> caretState;GUEST<INTEGER> just;GUEST<INTEGER> teLength;GUEST<Handle> hText;GUEST<INTEGER> recalBack;GUEST<INTEGER> recalLines;GUEST<INTEGER> clikStuff;GUEST<INTEGER> crOnly;GUEST<INTEGER> txFont;Style txFace;Byte filler;GUEST<INTEGER> txMode;GUEST<INTEGER> txSize;GUEST<GrafPtr> inPort;GUEST<ProcPtr> highHook;GUEST<ProcPtr> caretHook;GUEST<INTEGER> nLines;GUEST<INTEGER> lineStarts[1];};static_assert(sizeof(TERec) == 98);void describeStruct(const TERec&, std::ostream&);

typedef TERec *TEPtr;

typedef GUEST<TEPtr> *TEHandle;

struct StyleRun{GUEST_STRUCT;GUEST<INTEGER> startChar;GUEST<INTEGER> styleIndex;};static_assert(sizeof(StyleRun) == 4);void describeStruct(const StyleRun&, std::ostream&);

struct STElement{GUEST_STRUCT;GUEST<INTEGER> stCount;GUEST<INTEGER> stHeight;GUEST<INTEGER> stAscent;GUEST<INTEGER> stFont;Style stFace;Byte filler;GUEST<INTEGER> stSize;RGBColor stColor;};static_assert(sizeof(STElement) == 18);void describeStruct(const STElement&, std::ostream&);

typedef STElement TEStyleTable[1];

typedef STElement *STPtr;

typedef GUEST<STPtr> *STHandle;

struct LHElement{GUEST_STRUCT;GUEST<INTEGER> lhHeight;GUEST<INTEGER> lhAscent;};static_assert(sizeof(LHElement) == 4);void describeStruct(const LHElement&, std::ostream&);

typedef LHElement LHTable[1];

typedef LHElement *LHPtr;

typedef GUEST<LHPtr> *LHHandle;

struct TextStyle{GUEST_STRUCT;GUEST<INTEGER> tsFont;Style tsFace;Byte filler;GUEST<INTEGER> tsSize;RGBColor tsColor;};static_assert(sizeof(TextStyle) == 12);void describeStruct(const TextStyle&, std::ostream&);

struct ScrpSTElement{GUEST_STRUCT;GUEST<LONGINT> scrpStartChar;GUEST<INTEGER> scrpHeight;GUEST<INTEGER> scrpAscent;GUEST<INTEGER> scrpFont;Style scrpFace;Byte filler;GUEST<INTEGER> scrpSize;RGBColor scrpColor;};static_assert(sizeof(ScrpSTElement) == 20);void describeStruct(const ScrpSTElement&, std::ostream&);

typedef ScrpSTElement ScrpSTTable[1];

struct StScrpRec{GUEST_STRUCT;GUEST<INTEGER> scrpNStyles;GUEST<ScrpSTTable> scrpStyleTab;};static_assert(sizeof(StScrpRec) == 22);void describeStruct(const StScrpRec&, std::ostream&);

typedef StScrpRec *StScrpPtr;

typedef GUEST<StScrpPtr> *StScrpHandle;

struct NullSTRec{GUEST_STRUCT;GUEST<LONGINT> TEReserved;GUEST<StScrpHandle> nullScrap;};static_assert(sizeof(NullSTRec) == 8);void describeStruct(const NullSTRec&, std::ostream&);

typedef NullSTRec *NullSTPtr;

typedef GUEST<NullSTPtr> *NullSTHandle;

struct TEStyleRec{GUEST_STRUCT;GUEST<INTEGER> nRuns;GUEST<INTEGER> nStyles;GUEST<STHandle> styleTab;GUEST<LHHandle> lhTab;GUEST<LONGINT> teRefCon;GUEST<NullSTHandle> nullStyle;StyleRun runs[1];};static_assert(sizeof(TEStyleRec) == 24);void describeStruct(const TEStyleRec&, std::ostream&);

typedef TEStyleRec *TEStylePtr;

typedef GUEST<TEStylePtr> *TEStyleHandle;

typedef Byte Chars[1];

typedef Byte *CharsPtr;

typedef GUEST<Byte*> *CharsHandle;

// TextEdit IMI-391 (true);
const LowMemGlobal<ProcPtr > TEDoText{ 0xA70 };
inline ProcPtr  LMGetTEDoText() { return LM(TEDoText); }
inline void LMSetTEDoText(ProcPtr val) { LM(TEDoText) = val; }
NOTRAP_FUNCTION2(LMGetTEDoText);
NOTRAP_FUNCTION2(LMSetTEDoText);


// TextEdit IMI-391 (false);
const LowMemGlobal<ProcPtr > TERecal{ 0xA74 };
inline ProcPtr  LMGetTERecal() { return LM(TERecal); }
inline void LMSetTERecal(ProcPtr val) { LM(TERecal) = val; }
NOTRAP_FUNCTION2(LMGetTERecal);
NOTRAP_FUNCTION2(LMSetTERecal);


// TextEdit IMI-389 (true);
const LowMemGlobal<INTEGER > TEScrpLength{ 0xAB0 };
inline INTEGER  LMGetTEScrpLength() { return LM(TEScrpLength); }
inline void LMSetTEScrpLength(INTEGER val) { LM(TEScrpLength) = val; }
NOTRAP_FUNCTION2(LMGetTEScrpLength);
NOTRAP_FUNCTION2(LMSetTEScrpLength);


// TextEdit IMI-389 (true);
const LowMemGlobal<Handle > TEScrpHandle{ 0xAB4 };
inline Handle  LMGetTEScrpHandle() { return LM(TEScrpHandle); }
inline void LMSetTEScrpHandle(Handle val) { LM(TEScrpHandle) = val; }
NOTRAP_FUNCTION2(LMGetTEScrpHandle);
NOTRAP_FUNCTION2(LMSetTEScrpHandle);


DISPATCHER_TRAP(TEDispatch, 0xA83D, StackW);


void C_TESetText(Ptr p, LONGINT ln, TEHandle teh);PASCAL_TRAP(TESetText, 0xA9CF);


CharsHandle C_TEGetText(TEHandle teh);PASCAL_TRAP(TEGetText, 0xA9CB);


void C_TESetAlignment(INTEGER j, TEHandle teh);PASCAL_TRAP(TESetAlignment, 0xA9DF);


void C_TEUpdate(const Rect *r, TEHandle teh);PASCAL_TRAP(TEUpdate, 0xA9D3);


void C_TETextBox(Ptr p, LONGINT ln, const Rect *r, INTEGER j);PASCAL_TRAP(TETextBox, 0xA9CE);


void C_TEScroll(INTEGER dh, INTEGER dv, TEHandle teh);PASCAL_TRAP(TEScroll, 0xA9DD);


void C_TEKey(CharParameter thec, TEHandle teh);PASCAL_TRAP(TEKey, 0xA9DC);


void C_TECopy(TEHandle teh);PASCAL_TRAP(TECopy, 0xA9D5);


void C_TECut(TEHandle teh);PASCAL_TRAP(TECut, 0xA9D6);


void C_TEPaste(TEHandle teh);PASCAL_TRAP(TEPaste, 0xA9DB);


void C_TEDelete(TEHandle teh);PASCAL_TRAP(TEDelete, 0xA9D7);


void C_TEInsert(Ptr p, LONGINT ln, TEHandle teh);PASCAL_TRAP(TEInsert, 0xA9DE);


void C_TEPinScroll(INTEGER dh, INTEGER dv, TEHandle teh);PASCAL_TRAP(TEPinScroll, 0xA812);


void C_TESelView(TEHandle teh);PASCAL_TRAP(TESelView, 0xA811);


void C_TEAutoView(Boolean autoflag, TEHandle teh);PASCAL_TRAP(TEAutoView, 0xA813);


TEHandle C_TEStyleNew(const Rect *dst, const Rect *view);PASCAL_TRAP(TEStyleNew, 0xA83E);


void C_TESetStyleHandle(TEStyleHandle theHandle, TEHandle teh);PASCAL_SUBTRAP(TESetStyleHandle, 0xA83D, 0x0005, TEDispatch);


TEStyleHandle C_TEGetStyleHandle(TEHandle teh);PASCAL_SUBTRAP(TEGetStyleHandle, 0xA83D, 0x0004, TEDispatch);


StScrpHandle C_TEGetStyleScrapHandle(TEHandle teh);PASCAL_SUBTRAP(TEGetStyleScrapHandle, 0xA83D, 0x0006, TEDispatch);


void C_TEStyleInsert(Ptr text, LONGINT length, StScrpHandle hST, TEHandle teh);PASCAL_SUBTRAP(TEStyleInsert, 0xA83D, 0x0007, TEDispatch);


INTEGER C_TEGetOffset(Point pt, TEHandle teh);PASCAL_TRAP(TEGetOffset, 0xA83C);


LONGINT C_TEGetPoint(INTEGER offset, TEHandle teh);PASCAL_SUBTRAP(TEGetPoint, 0xA83D, 0x0008, TEDispatch);


int32_t C_TEGetHeight(LONGINT endLine, LONGINT startLine, TEHandle teh);PASCAL_SUBTRAP(TEGetHeight, 0xA83D, 0x0009, TEDispatch);


void C_TEGetStyle(INTEGER offset, TextStyle *theStyle, GUEST<INTEGER> *lineHeight, GUEST<INTEGER> *fontAscent, TEHandle teh);PASCAL_SUBTRAP(TEGetStyle, 0xA83D, 0x0003, TEDispatch);


void C_TEStylePaste(TEHandle teh);PASCAL_SUBTRAP(TEStylePaste, 0xA83D, 0x0000, TEDispatch);


void C_TESetStyle(INTEGER mode, TextStyle *newStyle, Boolean redraw, TEHandle teh);PASCAL_SUBTRAP(TESetStyle, 0xA83D, 0x0001, TEDispatch);


void C_TEReplaceStyle(INTEGER mode, TextStyle *oldStyle, TextStyle *newStyle, Boolean redraw, TEHandle teh);PASCAL_SUBTRAP(TEReplaceStyle, 0xA83D, 0x0002, TEDispatch);


Boolean C_TEContinuousStyle(GUEST<INTEGER> *modep, TextStyle *thestyle, TEHandle teh);PASCAL_SUBTRAP(TEContinuousStyle, 0xA83D, 0x000A, TEDispatch);


void C_TEUseStyleScrap(LONGINT start, LONGINT stop, StScrpHandle newstyles, Boolean redraw, TEHandle teh);PASCAL_SUBTRAP(TEUseStyleScrap, 0xA83D, 0x000B, TEDispatch);


void C_TECustomHook(INTEGER sel, GUEST<ProcPtr> *addr, TEHandle teh);PASCAL_SUBTRAP(TECustomHook, 0xA83D, 0x000C, TEDispatch);


LONGINT C_TENumStyles(LONGINT start, LONGINT stop, TEHandle teh);PASCAL_SUBTRAP(TENumStyles, 0xA83D, 0x000D, TEDispatch);


void C_TEInit();PASCAL_TRAP(TEInit, 0xA9CC);


TEHandle C_TENew(const Rect *dst, const Rect *view);PASCAL_TRAP(TENew, 0xA9D2);


void C_TEDispose(TEHandle teh);PASCAL_TRAP(TEDispose, 0xA9CD);


void C_TEIdle(TEHandle teh);PASCAL_TRAP(TEIdle, 0xA9DA);


void C_TEClick(Point p, Boolean ext, TEHandle teh);PASCAL_TRAP(TEClick, 0xA9D4);


void C_TESetSelect(LONGINT start, LONGINT stop, TEHandle teh);PASCAL_TRAP(TESetSelect, 0xA9D1);


void C_TEActivate(TEHandle teh);PASCAL_TRAP(TEActivate, 0xA9D8);


void C_TEDeactivate(TEHandle teh);PASCAL_TRAP(TEDeactivate, 0xA9D9);


void TESetWordBreak(ProcPtr wb, TEHandle teh);NOTRAP_FUNCTION2(TESetWordBreak);


void TESetClickLoop(ProcPtr cp, TEHandle teh);NOTRAP_FUNCTION2(TESetClickLoop);


void C_TECalText(TEHandle teh);PASCAL_TRAP(TECalText, 0xA9D0);


OSErr TEFromScrap();NOTRAP_FUNCTION2(TEFromScrap);


OSErr TEToScrap();NOTRAP_FUNCTION2(TEToScrap);


Handle TEScrapHandle();NOTRAP_FUNCTION2(TEScrapHandle);


LONGINT TEGetScrapLength();NOTRAP_FUNCTION2(TEGetScrapLength);


void TESetScrapLength(LONGINT ln);NOTRAP_FUNCTION2(TESetScrapLength);


int16_t C_TEFeatureFlag(int16_t feature, int16_t action, TEHandle te);PASCAL_SUBTRAP(TEFeatureFlag, 0xA83D, 0x000E, TEDispatch);


// MacPhoenix. The selection's region, in the port's coordinates, clipped
// to the view rectangle; empty with no selection. 7.5.5's Drag Manager
// patch (System 'gpch' 668) adds the selector by drawing the hilite into
// an open region; applications ask it whether a click is on the selection
// before starting a drag.
OSErr C_TEGetHiliteRgn(RgnHandle region, TEHandle te);PASCAL_SUBTRAP(TEGetHiliteRgn, 0xA83D, 0x000F, TEDispatch);


} /* namespace Executor */