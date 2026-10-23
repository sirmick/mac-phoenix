/*****************************************************************************
 *                                                                           *
 *                                 FontMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     Fixed, Handle, INTEGER, LONGINT, Boolean, Byte,        *
 *                    SignedByte, StringPtr, ConstStringPtr, OSErr, Ptr,     *
 *                    Rect                                                   *
 *     QuickDraw.h    Style                                                  *
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

#define MODULE_NAME FontMgr
#include <base/api-module.h>


namespace Executor {

enum {systemFont = 0,
applFont = 1,
};

enum {kFontIDNewYork = 2,
#if OLDROUTINENAMES
#define newYork kFontIDNewYork
#endif
kFontIDGeneva = 3,
#if OLDROUTINENAMES
#define geneva kFontIDGeneva
#endif
kFontIDMonaco = 4,
#if OLDROUTINENAMES
#define monaco kFontIDMonaco
#endif
kFontIDVenice = 5,
#if OLDROUTINENAMES
#define venice kFontIDVenice
#endif
kFontIDLondon = 6,
#if OLDROUTINENAMES
#define london kFontIDLondon
#endif
kFontIDAthens = 7,
#if OLDROUTINENAMES
#define athens kFontIDAthens
#endif
kFontIDSanFrancisco = 8,
#if OLDROUTINENAMES
#define sanFran kFontIDSanFrancisco
#endif
kFontIDToronto = 9,
#if OLDROUTINENAMES
#define toronto kFontIDToronto
#endif
kFontIDCairo = 11,
#if OLDROUTINENAMES
#define cairo kFontIDCairo
#endif
kFontIDLosAngeles = 12,
#if OLDROUTINENAMES
#define losAngeles kFontIDLosAngeles
#endif
kFontIDTimes = 20,
#if OLDROUTINENAMES
#define times kFontIDTimes
#endif
kFontIDHelvetica = 21,
#if OLDROUTINENAMES
#define helvetica kFontIDHelvetica
#endif
kFontIDCourier = 22,
#if OLDROUTINENAMES
#define courier kFontIDCourier
#endif
kFontIDSymbol = 23,
#if OLDROUTINENAMES
#define symbol kFontIDSymbol
#endif
kFontIDTaliesin = 24,
#if OLDROUTINENAMES
#define taliesin kFontIDTaliesin
#endif
};

enum {commandMark = 17,
checkMark = 18,
diamondMark = 19,
appleMark = 20,
};

enum {propFont = 36864,
prpFntH = 36865,
prpFntW = 36866,
prpFntHW = 36867,
};

enum {fixedFont = 45056,
fxdFntH = 45057,
fxdFntW = 45058,
fxdFntHW = 45059,
};

enum {fontWid = 44208,
};

struct FMetricRec{GUEST_STRUCT;GUEST<Fixed> ascent;GUEST<Fixed> descent;GUEST<Fixed> leading;GUEST<Fixed> widMax;GUEST<Handle> wTabHandle;};static_assert(sizeof(FMetricRec) == 20);void describeStruct(const FMetricRec&, std::ostream&);

// FontAssoc ffAssoc;
// WidTable ffWidthTab;
// StyleTable ffStyTab;
// KernTable ffKernTab;
struct FamRec{GUEST_STRUCT;GUEST<INTEGER> ffFlags;GUEST<INTEGER> ffFamID;GUEST<INTEGER> ffFirstChar;GUEST<INTEGER> ffLastChar;GUEST<INTEGER> ffAscent;GUEST<INTEGER> ffDescent;GUEST<INTEGER> ffLeading;GUEST<INTEGER> ffWidMax;GUEST<LONGINT> ffWTabOff;GUEST<LONGINT> ffKernOff;GUEST<LONGINT> ffStylOff;GUEST<INTEGER> ffProperty[9];GUEST<INTEGER> ffIntl[2];GUEST<INTEGER> ffVersion;};static_assert(sizeof(FamRec) == 52);void describeStruct(const FamRec&, std::ostream&);

struct WidthTable{GUEST_STRUCT;GUEST<Fixed> tabData[256];GUEST<Handle> tabFont;GUEST<LONGINT> sExtra;GUEST<LONGINT> style;GUEST<INTEGER> fID;GUEST<INTEGER> fSize;GUEST<INTEGER> face;GUEST<INTEGER> device;GUEST<Point> inNumer;GUEST<Point> inDenom;GUEST<INTEGER> aFID;GUEST<Handle> fHand;Boolean usedFam;Byte aFace;GUEST<INTEGER> vOutput;GUEST<INTEGER> hOutput;GUEST<INTEGER> vFactor;GUEST<INTEGER> hFactor;GUEST<INTEGER> aSize;GUEST<INTEGER> tabSize;};static_assert(sizeof(WidthTable) == 1072);void describeStruct(const WidthTable&, std::ostream&);

struct FMInput{GUEST_STRUCT;GUEST<INTEGER> family;GUEST<INTEGER> size;Style face;Boolean needBits;GUEST<INTEGER> device;GUEST<Point> numer;GUEST<Point> denom;};static_assert(sizeof(FMInput) == 16);void describeStruct(const FMInput&, std::ostream&);

struct FMOutput{GUEST_STRUCT;GUEST<INTEGER> errNum; // 0x00
GUEST<Handle> fontHandle; // 0x02
Byte bold; // 0x06
Byte italic; // 0x07
Byte ulOffset; // 0x08
Byte ulShadow; // 0x09
Byte ulThick; // 0x0A
Byte shadow; // 0x0B
SignedByte extra; // 0x0C
Byte ascent; // 0x0D
Byte descent; // 0x0E
Byte widMax; // 0x0F
SignedByte leading; // 0x10
Byte unused; // 0x11
GUEST<Point> numer; // 0x12
GUEST<Point> denom; // 0x16
};static_assert(sizeof(FMOutput) == 26);void describeStruct(const FMOutput&, std::ostream&);

typedef FMOutput *FMOutPtr;

// more stuff is usually appended here ... bitImage, locTable, owTable
struct FontRec{GUEST_STRUCT;GUEST<INTEGER> fontType;GUEST<INTEGER> firstChar;GUEST<INTEGER> lastChar;GUEST<INTEGER> widMax;GUEST<INTEGER> kernMax;GUEST<INTEGER> nDescent;GUEST<INTEGER> fRectWidth;GUEST<INTEGER> fRectHeight;GUEST<INTEGER> owTLoc;GUEST<INTEGER> ascent;GUEST<INTEGER> descent;GUEST<INTEGER> leading;GUEST<INTEGER> rowWords;};static_assert(sizeof(FontRec) == 26);void describeStruct(const FontRec&, std::ostream&);

typedef FamRec *FamRecPtr;

typedef GUEST<FamRecPtr> *FamRecHandle;

typedef WidthTable *WidthTablePtr;

typedef GUEST<WidthTablePtr> *WidthTableHandle;

// FontMgr Private.a (true-b);
const LowMemGlobal<ProcPtr > JSwapFont{ 0x8E0 };
inline ProcPtr  LMGetJSwapFont() { return LM(JSwapFont); }
inline void LMSetJSwapFont(ProcPtr val) { LM(JSwapFont) = val; }
NOTRAP_FUNCTION2(LMGetJSwapFont);
NOTRAP_FUNCTION2(LMSetJSwapFont);


// FontMgr IMIV-42 (true);
const LowMemGlobal<Handle > WidthListHand{ 0x8E4 };
inline Handle  LMGetWidthListHand() { return LM(WidthListHand); }
inline void LMSetWidthListHand(Handle val) { LM(WidthListHand) = val; }
NOTRAP_FUNCTION2(LMGetWidthListHand);
NOTRAP_FUNCTION2(LMSetWidthListHand);


// FontMgr IMI-233 (true);
const LowMemGlobal<Handle > ROMFont0{ 0x980 };
inline Handle  LMGetROMFont0() { return LM(ROMFont0); }
inline void LMSetROMFont0(Handle val) { LM(ROMFont0) = val; }
NOTRAP_FUNCTION2(LMGetROMFont0);
NOTRAP_FUNCTION2(LMSetROMFont0);


// FontMgr IMIV-31 (true);
const LowMemGlobal<INTEGER > ApFontID{ 0x984 };
inline INTEGER  LMGetApFontID() { return LM(ApFontID); }
inline void LMSetApFontID(INTEGER val) { LM(ApFontID) = val; }
NOTRAP_FUNCTION2(LMGetApFontID);
NOTRAP_FUNCTION2(LMSetApFontID);


// FontMgr ToolEqu.a (true);
const LowMemGlobal<FMInput > ROMlib_myfmi{ 0x988 };
inline void LMGetROMlib_myfmi(FMInput *val) { *val = LM(ROMlib_myfmi); }
inline void LMSetROMlib_myfmi(const FMInput *val) { LM(ROMlib_myfmi) = *val; }
NOTRAP_FUNCTION2(LMGetROMlib_myfmi);
NOTRAP_FUNCTION2(LMSetROMlib_myfmi);


// FontMgr Private.a (true);
const LowMemGlobal<FMOutput > ROMlib_fmo{ 0x998 };
inline void LMGetROMlib_fmo(FMOutput *val) { *val = LM(ROMlib_fmo); }
inline void LMSetROMlib_fmo(const FMOutput *val) { LM(ROMlib_fmo) = *val; }
NOTRAP_FUNCTION2(LMGetROMlib_fmo);
NOTRAP_FUNCTION2(LMSetROMlib_fmo);


// FontMgr IMI-222 (true);
const LowMemGlobal<Byte > FScaleDisable{ 0xA63 };
inline Byte  LMGetFScaleDisable() { return LM(FScaleDisable); }
inline void LMSetFScaleDisable(Byte val) { LM(FScaleDisable) = val; }
NOTRAP_FUNCTION2(LMGetFScaleDisable);
NOTRAP_FUNCTION2(LMSetFScaleDisable);


// FontMgr IMIV-42 (true);
const LowMemGlobal<WidthTablePtr > WidthPtr{ 0xB10 };
inline WidthTablePtr  LMGetWidthPtr() { return LM(WidthPtr); }
inline void LMSetWidthPtr(WidthTablePtr val) { LM(WidthPtr) = val; }
NOTRAP_FUNCTION2(LMGetWidthPtr);
NOTRAP_FUNCTION2(LMSetWidthPtr);


// FontMgr IMIV-42 (true);
const LowMemGlobal<WidthTableHandle > WidthTabHandle{ 0xB2A };
inline WidthTableHandle  LMGetWidthTabHandle() { return LM(WidthTabHandle); }
inline void LMSetWidthTabHandle(WidthTableHandle val) { LM(WidthTabHandle) = val; }
NOTRAP_FUNCTION2(LMGetWidthTabHandle);
NOTRAP_FUNCTION2(LMSetWidthTabHandle);


// FontMgr IMIV-42 (true);
const LowMemGlobal<LONGINT > IntlSpec{ 0xBA0 };
inline LONGINT  LMGetIntlSpec() { return LM(IntlSpec); }
inline void LMSetIntlSpec(LONGINT val) { LM(IntlSpec) = val; }
NOTRAP_FUNCTION2(LMGetIntlSpec);
NOTRAP_FUNCTION2(LMSetIntlSpec);


// FontMgr IMIV-31 (true);
const LowMemGlobal<INTEGER > SysFontFam{ 0xBA6 };
inline INTEGER  LMGetSysFontFam() { return LM(SysFontFam); }
inline void LMSetSysFontFam(INTEGER val) { LM(SysFontFam) = val; }
NOTRAP_FUNCTION2(LMGetSysFontFam);
NOTRAP_FUNCTION2(LMSetSysFontFam);


// FontMgr IMIV-31 (true);
const LowMemGlobal<INTEGER > SysFontSiz{ 0xBA8 };
inline INTEGER  LMGetSysFontSiz() { return LM(SysFontSiz); }
inline void LMSetSysFontSiz(INTEGER val) { LM(SysFontSiz) = val; }
NOTRAP_FUNCTION2(LMGetSysFontSiz);
NOTRAP_FUNCTION2(LMSetSysFontSiz);


// FontMgr IMIV-36 (true);
const LowMemGlobal<FamRecHandle > LastFOND{ 0xBC2 };
inline FamRecHandle  LMGetLastFOND() { return LM(LastFOND); }
inline void LMSetLastFOND(FamRecHandle val) { LM(LastFOND) = val; }
NOTRAP_FUNCTION2(LMGetLastFOND);
NOTRAP_FUNCTION2(LMSetLastFOND);


// FontMgr ToolEqu.a (true-b);
const LowMemGlobal<INTEGER > fondid{ 0xBC6 };
inline INTEGER  LMGetfondid() { return LM(fondid); }
inline void LMSetfondid(INTEGER val) { LM(fondid) = val; }
NOTRAP_FUNCTION2(LMGetfondid);
NOTRAP_FUNCTION2(LMSetfondid);


// FontMgr IMIV-32 (true);
const LowMemGlobal<Byte > FractEnable{ 0xBF4 };
inline Byte  LMGetFractEnable() { return LM(FractEnable); }
inline void LMSetFractEnable(Byte val) { LM(FractEnable) = val; }
NOTRAP_FUNCTION2(LMGetFractEnable);
NOTRAP_FUNCTION2(LMSetFractEnable);


// FontMgr IMV-182 (false);
const LowMemGlobal<Handle > SynListHandle{ 0xD32 };
inline Handle  LMGetSynListHandle() { return LM(SynListHandle); }
inline void LMSetSynListHandle(Handle val) { LM(SynListHandle) = val; }
NOTRAP_FUNCTION2(LMGetSynListHandle);
NOTRAP_FUNCTION2(LMSetSynListHandle);


DISPATCHER_TRAP(FontDispatch, 0xA854, D0<0xF>);


void C_InitFonts();PASCAL_TRAP(InitFonts, 0xA8FE);


void C_GetFontName(INTEGER fnum, StringPtr fnam);PASCAL_TRAP(GetFontName, 0xA8FF);


void C_GetFNum(ConstStringPtr fnam, GUEST<INTEGER> *fnum);PASCAL_TRAP(GetFNum, 0xA900);


Boolean C_RealFont(INTEGER fnum, INTEGER sz);PASCAL_TRAP(RealFont, 0xA902);


void C_SetFontLock(Boolean lflag);PASCAL_TRAP(SetFontLock, 0xA903);


FMOutPtr C_FMSwapFont(FMInput *fmip);PASCAL_TRAP(FMSwapFont, 0xA901);


void C_FontMetrics(FMetricRec *metrp);PASCAL_TRAP(FontMetrics, 0xA835);


void C_SetFScaleDisable(Boolean disable);PASCAL_TRAP(SetFScaleDisable, 0xA834);


void C_SetFractEnable(Boolean enable);PASCAL_TRAP(SetFractEnable, 0xA814);


void C_SetOutlinePreferred(Boolean _outline_perferred_p);PASCAL_SUBTRAP(SetOutlinePreferred, 0xA854, 0x0001, FontDispatch);


Boolean C_GetOutlinePreferred();PASCAL_SUBTRAP(GetOutlinePreferred, 0xA854, 0x0009, FontDispatch);


Boolean C_IsOutline(Point numer, Point denom);PASCAL_SUBTRAP(IsOutline, 0xA854, 0x0000, FontDispatch);


OSErr C_OutlineMetrics(int16_t byte_count, Ptr text, Point numer, Point denom, GUEST<int16_t> *y_max, GUEST<int16_t> *y_min, GUEST<Fixed> *aw_array, GUEST<Fixed> *lsb_array, Rect *bounds_array);PASCAL_SUBTRAP(OutlineMetrics, 0xA854, 0x0008, FontDispatch);


void C_SetPreserveGlyph(Boolean preserve_glyph);PASCAL_SUBTRAP(SetPreserveGlyph, 0xA854, 0x000A, FontDispatch);


Boolean C_GetPreserveGlyph();PASCAL_SUBTRAP(GetPreserveGlyph, 0xA854, 0x000B, FontDispatch);


OSErr C_FlushFonts();PASCAL_SUBTRAP(FlushFonts, 0xA854, 0x000C, FontDispatch);


} /* namespace Executor */