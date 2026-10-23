/*****************************************************************************
 *                                                                           *
 *                                QuickDraw.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    SignedByte, INTEGER, Rect, Ptr, Byte, Handle, Fixed,    *
 *                   LONGINT, SmallFract, Boolean, ConstStringPtr,           *
 *                   CharParameter                                           *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME QuickDraw
#include <base/api-module.h>


namespace Executor {

// number of bytes InitGraf needs
enum {grafSize = 206,
};

enum {srcCopy = 0,
srcOr = 1,
srcXor = 2,
srcBic = 3,
notSrcCopy = 4,
notSrcOr = 5,
notSrcXor = 6,
notSrcBic = 7,
};

enum {patCopy = 8,
patOr = 9,
patXor = 10,
patBic = 11,
notPatCopy = 12,
notPatOr = 13,
notPatXor = 14,
notPatBic = 15,
};

enum {grayishTextOr = 49,
};

enum {hilite = 50,
};

enum {blackColor = 33,
whiteColor = 30,
redColor = 205,
greenColor = 341,
blueColor = 409,
cyanColor = 273,
magentaColor = 137,
yellowColor = 69,
};

enum {picLParen = 0,
picRParen = 1,
};

typedef enum {normal = 0,
bold = 1,
italic = 2,
underline = 4,
outline = 8,
shadow = 16,
condense = 32,
extend = 64,
}StyleItem;

typedef SignedByte Style;

struct Region{GUEST_STRUCT;GUEST<INTEGER> rgnSize;Rect rgnBBox;};static_assert(sizeof(Region) == 10);void describeStruct(const Region&, std::ostream&);

typedef Region *RgnPtr;

typedef GUEST<RgnPtr> *RgnHandle;

struct BitMap{GUEST_STRUCT;GUEST<Ptr> baseAddr;GUEST<INTEGER> rowBytes;Rect bounds;};static_assert(sizeof(BitMap) == 14);void describeStruct(const BitMap&, std::ostream&);

struct Pattern{GUEST_STRUCT;Byte pat[8];};void describeStruct(const Pattern&, std::ostream&);

typedef GUEST<INTEGER> Bits16[16];

struct Cursor{GUEST_STRUCT;GUEST<Bits16> data;GUEST<Bits16> mask;GUEST<Point> hotSpot;};static_assert(sizeof(Cursor) == 68);void describeStruct(const Cursor&, std::ostream&);

typedef Cursor *CursPtr;

typedef GUEST<CursPtr> *CursHandle;

typedef SignedByte GrafVerb;

enum {frame = 0,
paint = 1,
erase = 2,
invert = 3,
fill = 4,
};

struct Polygon{GUEST_STRUCT;GUEST<INTEGER> polySize;Rect polyBBox;GUEST<Point> polyPoints[1];};static_assert(sizeof(Polygon) == 14);void describeStruct(const Polygon&, std::ostream&);

typedef Polygon *PolyPtr;

typedef GUEST<PolyPtr> *PolyHandle;

struct FontInfo{GUEST_STRUCT;GUEST<INTEGER> ascent;GUEST<INTEGER> descent;GUEST<INTEGER> widMax;GUEST<INTEGER> leading;};static_assert(sizeof(FontInfo) == 8);void describeStruct(const FontInfo&, std::ostream&);

using QDTextUPP = UPP<void(INTEGER bc, Ptr textb, Point num, Point den)>;


using QDLineUPP = UPP<void(Point drawto)>;


using QDRectUPP = UPP<void(GrafVerb verb, const Rect *rp)>;


using QDRRectUPP = UPP<void(GrafVerb verb, const Rect *rp, INTEGER ow, INTEGER oh)>;


using QDOvalUPP = UPP<void(GrafVerb verb, const Rect *rp)>;


using QDArcUPP = UPP<void(GrafVerb verb, const Rect *rp, INTEGER ang, INTEGER arc)>;


using QDPolyUPP = UPP<void(GrafVerb verb, PolyHandle poly)>;


using QDRgnUPP = UPP<void(GrafVerb verb, RgnHandle rgn)>;


using QDBitsUPP = UPP<void(const BitMap *srcb, const Rect *srcr, const Rect *dstr, INTEGER mod, RgnHandle mask)>;


using QDCommentUPP = UPP<void(INTEGER kind, INTEGER size, Handle data)>;


using QDTexMeasUPP = UPP<INTEGER(INTEGER bc, Ptr texta, GUEST<Point> *numer, GUEST<Point> *denom, FontInfo *info)>;


using QDGetPicUPP = UPP<void(void *data, INTEGER bc)>;


using QDPutPicUPP = UPP<void(const void *data, INTEGER bc)>;


struct QDProcs{GUEST_STRUCT;GUEST<QDTextUPP> textProc;GUEST<QDLineUPP> lineProc;GUEST<QDRectUPP> rectProc;GUEST<QDRRectUPP> rRectProc;GUEST<QDOvalUPP> ovalProc;GUEST<QDArcUPP> arcProc;GUEST<QDPolyUPP> polyProc;GUEST<QDRgnUPP> rgnProc;GUEST<QDBitsUPP> bitsProc;GUEST<QDCommentUPP> commentProc;GUEST<QDTexMeasUPP> txMeasProc;GUEST<QDGetPicUPP> getPicProc;GUEST<QDPutPicUPP> putPicProc;};static_assert(sizeof(QDProcs) == 52);void describeStruct(const QDProcs&, std::ostream&);

typedef QDProcs *QDProcsPtr;

struct GrafPort{GUEST_STRUCT;GUEST<INTEGER> device;BitMap portBits;Rect portRect;GUEST<RgnHandle> visRgn;GUEST<RgnHandle> clipRgn;Pattern bkPat;Pattern fillPat;GUEST<Point> pnLoc;GUEST<Point> pnSize;GUEST<INTEGER> pnMode;Pattern pnPat;GUEST<INTEGER> pnVis;GUEST<INTEGER> txFont;Style txFace;Byte filler;GUEST<INTEGER> txMode;GUEST<INTEGER> txSize;GUEST<Fixed> spExtra;GUEST<LONGINT> fgColor;GUEST<LONGINT> bkColor;GUEST<INTEGER> colrBit;GUEST<INTEGER> patStretch;GUEST<Handle> picSave;GUEST<Handle> rgnSave;GUEST<Handle> polySave;GUEST<QDProcsPtr> grafProcs;};static_assert(sizeof(GrafPort) == 108);void describeStruct(const GrafPort&, std::ostream&);

typedef GrafPort *GrafPtr;

struct Picture{GUEST_STRUCT;GUEST<INTEGER> picSize;Rect picFrame;};static_assert(sizeof(Picture) == 10);void describeStruct(const Picture&, std::ostream&);

typedef Picture *PicPtr;

typedef GUEST<PicPtr> *PicHandle;

struct PenState{GUEST_STRUCT;GUEST<Point> pnLoc;GUEST<Point> pnSize;GUEST<INTEGER> pnMode;Pattern pnPat;};static_assert(sizeof(PenState) == 18);void describeStruct(const PenState&, std::ostream&);

// IMV stuff is used when we parse Version 2 pictures, but the IMV calls
//    are not supported in V1.0
enum {blend = 32,
addPin,
addOver,
subPin,
transparent,
adMax,
subOver,
adMin,
mask = 64,
};

enum {pHiliteBit = 0,
hiliteBit = 7,
};

enum {defQDColors = 127,
};

struct RGBColor{GUEST_STRUCT;GUEST<uint16_t> red;GUEST<uint16_t> green;GUEST<uint16_t> blue;};static_assert(sizeof(RGBColor) == 6);void describeStruct(const RGBColor&, std::ostream&);

struct HSVColor{GUEST_STRUCT;GUEST<SmallFract> hue;GUEST<SmallFract> saturation;GUEST<SmallFract> value;};static_assert(sizeof(HSVColor) == 6);void describeStruct(const HSVColor&, std::ostream&);

struct HSLColor{GUEST_STRUCT;GUEST<SmallFract> hue;GUEST<SmallFract> saturation;GUEST<SmallFract> lightness;};static_assert(sizeof(HSLColor) == 6);void describeStruct(const HSLColor&, std::ostream&);

struct CMYColor{GUEST_STRUCT;GUEST<SmallFract> cyan;GUEST<SmallFract> magenta;GUEST<SmallFract> yellow;};static_assert(sizeof(CMYColor) == 6);void describeStruct(const CMYColor&, std::ostream&);

struct ColorSpec{GUEST_STRUCT;GUEST<INTEGER> value;RGBColor rgb;};static_assert(sizeof(ColorSpec) == 8);void describeStruct(const ColorSpec&, std::ostream&);

// can't use 0
typedef ColorSpec cSpecArray[1];

struct ColorTable{GUEST_STRUCT;GUEST<LONGINT> ctSeed;GUEST<uint16_t> ctFlags;GUEST<INTEGER> ctSize;GUEST<cSpecArray> ctTable;};static_assert(sizeof(ColorTable) == 16);void describeStruct(const ColorTable&, std::ostream&);

typedef ColorTable *CTabPtr;

typedef GUEST<CTabPtr> *CTabHandle;

struct CQDProcs{GUEST_STRUCT;GUEST<QDTextUPP> textProc;GUEST<QDLineUPP> lineProc;GUEST<QDRectUPP> rectProc;GUEST<QDRRectUPP> rRectProc;GUEST<QDOvalUPP> ovalProc;GUEST<QDArcUPP> arcProc;GUEST<QDPolyUPP> polyProc;GUEST<QDRgnUPP> rgnProc;GUEST<QDBitsUPP> bitsProc;GUEST<QDCommentUPP> commentProc;GUEST<QDTexMeasUPP> txMeasProc;GUEST<QDGetPicUPP> getPicProc;GUEST<QDPutPicUPP> putPicProc;GUEST<Ptr> opcodeProc;GUEST<Ptr> newProc1Proc;GUEST<Ptr> newProc2Proc;GUEST<Ptr> newProc3Proc;GUEST<Ptr> newProc4Proc;GUEST<Ptr> newProc5Proc;GUEST<Ptr> newProc6Proc;};static_assert(sizeof(CQDProcs) == 80);void describeStruct(const CQDProcs&, std::ostream&);

typedef CQDProcs *CQDProcsPtr;

struct PixMap{GUEST_STRUCT;GUEST<Ptr> baseAddr;GUEST<INTEGER> rowBytes;Rect bounds;GUEST<INTEGER> pmVersion;GUEST<INTEGER> packType;GUEST<LONGINT> packSize;GUEST<Fixed> hRes;GUEST<Fixed> vRes;GUEST<INTEGER> pixelType;GUEST<INTEGER> pixelSize;GUEST<INTEGER> cmpCount;GUEST<INTEGER> cmpSize;GUEST<LONGINT> planeBytes;GUEST<CTabHandle> pmTable;GUEST<LONGINT> pmReserved;};static_assert(sizeof(PixMap) == 50);void describeStruct(const PixMap&, std::ostream&);

typedef PixMap *PixMapPtr;

typedef GUEST<PixMapPtr> *PixMapHandle;

enum {ROWMASK = 8191,
};

struct PixPat{GUEST_STRUCT;GUEST<INTEGER> patType;GUEST<PixMapHandle> patMap;GUEST<Handle> patData;GUEST<Handle> patXData;GUEST<INTEGER> patXValid;GUEST<Handle> patXMap;Pattern pat1Data;};static_assert(sizeof(PixPat) == 28);void describeStruct(const PixPat&, std::ostream&);

typedef PixPat *PixPatPtr;

typedef GUEST<PixPatPtr> *PixPatHandle;

struct CGrafPort{GUEST_STRUCT;GUEST<INTEGER> device;GUEST<PixMapHandle> portPixMap;GUEST<INTEGER> portVersion;GUEST<Handle> grafVars;GUEST<INTEGER> chExtra;GUEST<INTEGER> pnLocHFrac;Rect portRect;GUEST<RgnHandle> visRgn;GUEST<RgnHandle> clipRgn;GUEST<PixPatHandle> bkPixPat;RGBColor rgbFgColor;RGBColor rgbBkColor;GUEST<Point> pnLoc;GUEST<Point> pnSize;GUEST<INTEGER> pnMode;GUEST<PixPatHandle> pnPixPat;GUEST<PixPatHandle> fillPixPat;GUEST<INTEGER> pnVis;GUEST<INTEGER> txFont;Style txFace;Byte filler;GUEST<INTEGER> txMode;GUEST<INTEGER> txSize;GUEST<Fixed> spExtra;GUEST<LONGINT> fgColor;GUEST<LONGINT> bkColor;GUEST<INTEGER> colrBit;GUEST<INTEGER> patStretch;GUEST<Handle> picSave;GUEST<Handle> rgnSave;GUEST<Handle> polySave;GUEST<CQDProcsPtr> grafProcs;};static_assert(sizeof(CGrafPort) == 108);void describeStruct(const CGrafPort&, std::ostream&);

typedef CGrafPort *CGrafPtr;


struct CCrsr{GUEST_STRUCT;GUEST<INTEGER> crsrType;GUEST<PixMapHandle> crsrMap;GUEST<Handle> crsrData;GUEST<Handle> crsrXData;GUEST<INTEGER> crsrXValid;GUEST<Handle> crsrXHandle;GUEST<Bits16> crsr1Data;GUEST<Bits16> crsrMask;GUEST<Point> crsrHotSpot;GUEST<LONGINT> crsrXTable;GUEST<LONGINT> crsrID;};static_assert(sizeof(CCrsr) == 96);void describeStruct(const CCrsr&, std::ostream&);

typedef CCrsr *CCrsrPtr;

typedef GUEST<CCrsrPtr> *CCrsrHandle;

struct MatchRec{GUEST_STRUCT;GUEST<uint16_t> red;GUEST<uint16_t> green;GUEST<uint16_t> blue;GUEST<int32_t> matchData;};static_assert(sizeof(MatchRec) == 10);void describeStruct(const MatchRec&, std::ostream&);

typedef Byte *BytePtr;

struct QDGlobals{GUEST_STRUCT;char privates[76];GUEST<int32_t> randSeed;BitMap screenBits;Cursor arrow;Pattern dkGray;Pattern ltGray;Pattern gray;Pattern black;Pattern white;GUEST<GrafPtr> thePort;};void describeStruct(const QDGlobals&, std::ostream&);

typedef QDGlobals *QDGlobalsPtr;

// QuickDraw IMI-473 (true);
const LowMemGlobal<INTEGER > ScrVRes{ 0x102 };
inline INTEGER  LMGetScrVRes() { return LM(ScrVRes); }
inline void LMSetScrVRes(INTEGER val) { LM(ScrVRes) = val; }
NOTRAP_FUNCTION2(LMGetScrVRes);
NOTRAP_FUNCTION2(LMSetScrVRes);


// QuickDraw IMI-473 (true);
const LowMemGlobal<INTEGER > ScrHRes{ 0x104 };
inline INTEGER  LMGetScrHRes() { return LM(ScrHRes); }
inline void LMSetScrHRes(INTEGER val) { LM(ScrHRes) = val; }
NOTRAP_FUNCTION2(LMGetScrHRes);
NOTRAP_FUNCTION2(LMSetScrHRes);


// QuickDraw ThinkC (true);
const LowMemGlobal<INTEGER > ScreenRow{ 0x106 };
inline INTEGER  LMGetScreenRow() { return LM(ScreenRow); }
inline void LMSetScreenRow(INTEGER val) { LM(ScreenRow) = val; }
NOTRAP_FUNCTION2(LMGetScreenRow);
NOTRAP_FUNCTION2(LMSetScreenRow);


// QuickDraw IMI-195 (true);
const LowMemGlobal<LONGINT > RndSeed{ 0x156 };
inline LONGINT  LMGetRndSeed() { return LM(RndSeed); }
inline void LMSetRndSeed(LONGINT val) { LM(RndSeed) = val; }
NOTRAP_FUNCTION2(LMGetRndSeed);
NOTRAP_FUNCTION2(LMSetRndSeed);


// QuickDraw MPW (false);
const LowMemGlobal<Byte [8]> ScreenVars{ 0x292 };


// * NOTE: Key1Trans in the keyboard translator procedure, and Key2Trans in the
//  * numeric keypad translator procedure (MPW).
// QuickDraw MPW (false);
const LowMemGlobal<Ptr > Key1Trans{ 0x29E };
inline Ptr  LMGetKey1Trans() { return LM(Key1Trans); }
inline void LMSetKey1Trans(Ptr val) { LM(Key1Trans) = val; }
NOTRAP_FUNCTION2(LMGetKey1Trans);
NOTRAP_FUNCTION2(LMSetKey1Trans);


// QuickDraw MPW (false);
const LowMemGlobal<Ptr > Key2Trans{ 0x2A2 };
inline Ptr  LMGetKey2Trans() { return LM(Key2Trans); }
inline void LMSetKey2Trans(Ptr val) { LM(Key2Trans) = val; }
NOTRAP_FUNCTION2(LMGetKey2Trans);
NOTRAP_FUNCTION2(LMSetKey2Trans);


// QuickDraw IMV (true-b);
const LowMemGlobal<ProcPtr > JUnknown574{ 0x574 };
inline ProcPtr  LMGetJUnknown574() { return LM(JUnknown574); }
inline void LMSetJUnknown574(ProcPtr val) { LM(JUnknown574) = val; }
NOTRAP_FUNCTION2(LMGetJUnknown574);
NOTRAP_FUNCTION2(LMSetJUnknown574);


// QuickDraw IMV (false);
const LowMemGlobal<ProcPtr > JADBProc{ 0x6B8 };
inline ProcPtr  LMGetJADBProc() { return LM(JADBProc); }
inline void LMSetJADBProc(ProcPtr val) { LM(JADBProc) = val; }
NOTRAP_FUNCTION2(LMGetJADBProc);
NOTRAP_FUNCTION2(LMSetJADBProc);


// QuickDraw Private.a (true-b);
const LowMemGlobal<ProcPtr > JHideCursor{ 0x800 };
inline ProcPtr  LMGetJHideCursor() { return LM(JHideCursor); }
inline void LMSetJHideCursor(ProcPtr val) { LM(JHideCursor) = val; }
NOTRAP_FUNCTION2(LMGetJHideCursor);
NOTRAP_FUNCTION2(LMSetJHideCursor);


// QuickDraw Private.a (true-b);
const LowMemGlobal<ProcPtr > JShowCursor{ 0x804 };
inline ProcPtr  LMGetJShowCursor() { return LM(JShowCursor); }
inline void LMSetJShowCursor(ProcPtr val) { LM(JShowCursor) = val; }
NOTRAP_FUNCTION2(LMGetJShowCursor);
NOTRAP_FUNCTION2(LMSetJShowCursor);


// QuickDraw Private.a (true-b);
const LowMemGlobal<ProcPtr > JShieldCursor{ 0x808 };
inline ProcPtr  LMGetJShieldCursor() { return LM(JShieldCursor); }
inline void LMSetJShieldCursor(ProcPtr val) { LM(JShieldCursor) = val; }
NOTRAP_FUNCTION2(LMGetJShieldCursor);
NOTRAP_FUNCTION2(LMSetJShieldCursor);


// QuickDraw Private.a (false);
const LowMemGlobal<ProcPtr > JScrnAddr{ 0x80C };
inline ProcPtr  LMGetJScrnAddr() { return LM(JScrnAddr); }
inline void LMSetJScrnAddr(ProcPtr val) { LM(JScrnAddr) = val; }
NOTRAP_FUNCTION2(LMGetJScrnAddr);
NOTRAP_FUNCTION2(LMSetJScrnAddr);


// QuickDraw Private.a (false);
const LowMemGlobal<ProcPtr > JScrnSize{ 0x810 };
inline ProcPtr  LMGetJScrnSize() { return LM(JScrnSize); }
inline void LMSetJScrnSize(ProcPtr val) { LM(JScrnSize) = val; }
NOTRAP_FUNCTION2(LMGetJScrnSize);
NOTRAP_FUNCTION2(LMSetJScrnSize);


// QuickDraw Private.a (true-b);
const LowMemGlobal<ProcPtr > JInitCrsr{ 0x814 };
inline ProcPtr  LMGetJInitCrsr() { return LM(JInitCrsr); }
inline void LMSetJInitCrsr(ProcPtr val) { LM(JInitCrsr) = val; }
NOTRAP_FUNCTION2(LMGetJInitCrsr);
NOTRAP_FUNCTION2(LMSetJInitCrsr);


// QuickDraw Private.a (true-b);
const LowMemGlobal<ProcPtr > JSetCrsr{ 0x818 };
inline ProcPtr  LMGetJSetCrsr() { return LM(JSetCrsr); }
inline void LMSetJSetCrsr(ProcPtr val) { LM(JSetCrsr) = val; }
NOTRAP_FUNCTION2(LMGetJSetCrsr);
NOTRAP_FUNCTION2(LMSetJSetCrsr);


// QuickDraw Private.a (true-b);
const LowMemGlobal<ProcPtr > JCrsrObscure{ 0x81C };
inline ProcPtr  LMGetJCrsrObscure() { return LM(JCrsrObscure); }
inline void LMSetJCrsrObscure(ProcPtr val) { LM(JCrsrObscure) = val; }
NOTRAP_FUNCTION2(LMGetJCrsrObscure);
NOTRAP_FUNCTION2(LMSetJCrsrObscure);


// QuickDraw Private.a (false);
const LowMemGlobal<ProcPtr > JUpdateProc{ 0x820 };
inline ProcPtr  LMGetJUpdateProc() { return LM(JUpdateProc); }
inline void LMSetJUpdateProc(ProcPtr val) { LM(JUpdateProc) = val; }
NOTRAP_FUNCTION2(LMGetJUpdateProc);
NOTRAP_FUNCTION2(LMSetJUpdateProc);


// QuickDraw IMII-19 (true);
const LowMemGlobal<Ptr > ScrnBase{ 0x824 };
inline Ptr  LMGetScrnBase() { return LM(ScrnBase); }
inline void LMSetScrnBase(Ptr val) { LM(ScrnBase) = val; }
NOTRAP_FUNCTION2(LMGetScrnBase);
NOTRAP_FUNCTION2(LMSetScrnBase);


// * MouseLocation used to be 0x830, but that doesn't jibe with what I've
//  * seen of Crystal Quest --ctm
// QuickDraw ThinkC (false);
const LowMemGlobal<Rect > CrsrPin{ 0x834 };
inline void LMGetCrsrPin(Rect *val) { *val = LM(CrsrPin); }
inline void LMSetCrsrPin(const Rect *val) { LM(CrsrPin) = *val; }
NOTRAP_FUNCTION2(LMGetCrsrPin);
NOTRAP_FUNCTION2(LMSetCrsrPin);


// QuickDraw IMV (false);
const LowMemGlobal<Byte > QDColors{ 0x8B0 };
inline Byte  LMGetQDColors() { return LM(QDColors); }
inline void LMSetQDColors(Byte val) { LM(QDColors) = val; }
NOTRAP_FUNCTION2(LMGetQDColors);
NOTRAP_FUNCTION2(LMSetQDColors);


// QuickDraw SysEqu.a (true);
const LowMemGlobal<Boolean > CrsrVis{ 0x8CC };
inline Boolean  LMGetCrsrVis() { return LM(CrsrVis); }
inline void LMSetCrsrVis(Boolean val) { LM(CrsrVis) = val; }
NOTRAP_FUNCTION2(LMGetCrsrVis);
NOTRAP_FUNCTION2(LMSetCrsrVis);


// QuickDraw SysEqu.a (true);
const LowMemGlobal<Byte > CrsrBusy{ 0x8CD };
inline Byte  LMGetCrsrBusy() { return LM(CrsrBusy); }
inline void LMSetCrsrBusy(Byte val) { LM(CrsrBusy) = val; }
NOTRAP_FUNCTION2(LMGetCrsrBusy);
NOTRAP_FUNCTION2(LMSetCrsrBusy);


// QuickDraw SysEqu.a (true);
const LowMemGlobal<INTEGER > CrsrState{ 0x8D0 };
inline INTEGER  LMGetCrsrState() { return LM(CrsrState); }
inline void LMSetCrsrState(INTEGER val) { LM(CrsrState) = val; }
NOTRAP_FUNCTION2(LMGetCrsrState);
NOTRAP_FUNCTION2(LMSetCrsrState);


// QuickDraw .a (true-b);
const LowMemGlobal<LONGINT > mousemask{ 0x8D6 };
inline LONGINT  LMGetmousemask() { return LM(mousemask); }
inline void LMSetmousemask(LONGINT val) { LM(mousemask) = val; }
NOTRAP_FUNCTION2(LMGetmousemask);
NOTRAP_FUNCTION2(LMSetmousemask);


// QuickDraw SysEqu.a (true-b);
const LowMemGlobal<LONGINT > mouseoffset{ 0x8DA };
inline LONGINT  LMGetmouseoffset() { return LM(mouseoffset); }
inline void LMSetmouseoffset(LONGINT val) { LM(mouseoffset) = val; }
NOTRAP_FUNCTION2(LMGetmouseoffset);
NOTRAP_FUNCTION2(LMSetmouseoffset);


// (true);
const LowMemGlobal<ProcPtr > JCrsrTask{ 0x8EE };
inline ProcPtr  LMGetJCrsrTask() { return LM(JCrsrTask); }
inline void LMSetJCrsrTask(ProcPtr val) { LM(JCrsrTask) = val; }
NOTRAP_FUNCTION2(LMGetJCrsrTask);
NOTRAP_FUNCTION2(LMSetJCrsrTask);


// QuickDraw IMV (true-b);
const LowMemGlobal<Byte > HiliteMode{ 0x938 };
inline Byte  LMGetHiliteMode() { return LM(HiliteMode); }
inline void LMSetHiliteMode(Byte val) { LM(HiliteMode) = val; }
NOTRAP_FUNCTION2(LMGetHiliteMode);
NOTRAP_FUNCTION2(LMSetHiliteMode);


// undocumented; 2-byte count followed by array of GrafPtrs
const LowMemGlobal<Handle > PortList{ 0xD66 };
inline Handle  LMGetPortList() { return LM(PortList); }
inline void LMSetPortList(Handle val) { LM(PortList) = val; }
NOTRAP_FUNCTION2(LMGetPortList);
NOTRAP_FUNCTION2(LMSetPortList);


void C_CopyBits(BitMap *src_bitmap, BitMap *dst_bitmap, const Rect *src_rect, const Rect *dst_rect, INTEGER mode, RgnHandle mask);PASCAL_TRAP(CopyBits, 0xA8EC);


void C_ScrollRect(Rect *rp, INTEGER dh, INTEGER dv, RgnHandle updatergn);PASCAL_TRAP(ScrollRect, 0xA8EF);


void C_ForeColor(LONGINT c);PASCAL_TRAP(ForeColor, 0xA862);


void C_BackColor(LONGINT c);PASCAL_TRAP(BackColor, 0xA863);


void C_ColorBit(INTEGER b);PASCAL_TRAP(ColorBit, 0xA864);


void C_SetCursor(Cursor *cp);PASCAL_TRAP(SetCursor, 0xA851);


void C_InitCursor();PASCAL_TRAP(InitCursor, 0xA850);


void C_HideCursor();PASCAL_TRAP(HideCursor, 0xA852);


void C_ShowCursor();PASCAL_TRAP(ShowCursor, 0xA853);


void C_ObscureCursor();PASCAL_TRAP(ObscureCursor, 0xA856);


void C_ShieldCursor(Rect *rp, Point p);PASCAL_TRAP(ShieldCursor, 0xA855);


void C_InitGraf(GUEST<GrafPtr> *gp);PASCAL_TRAP(InitGraf, 0xA86E);


void C_SetPort(GrafPtr p);PASCAL_TRAP(SetPort, 0xA873);


void C_InitPort(GrafPtr p);PASCAL_TRAP(InitPort, 0xA86D);


void C_OpenPort(GrafPtr p);PASCAL_TRAP(OpenPort, 0xA86F);


void C_ClosePort(GrafPtr p);PASCAL_TRAP(ClosePort, 0xA87D);


void C_GetPort(GUEST<GrafPtr> *pp);PASCAL_TRAP(GetPort, 0xA874);


void C_GrafDevice(INTEGER d);PASCAL_TRAP(GrafDevice, 0xA872);


void C_SetPortBits(BitMap *bm);PASCAL_TRAP(SetPortBits, 0xA875);


void C_PortSize(INTEGER w, INTEGER h);PASCAL_TRAP(PortSize, 0xA876);


void C_MovePortTo(INTEGER lg, INTEGER tg);PASCAL_TRAP(MovePortTo, 0xA877);


void C_SetOrigin(INTEGER h, INTEGER v);PASCAL_TRAP(SetOrigin, 0xA878);


void C_SetClip(RgnHandle r);PASCAL_TRAP(SetClip, 0xA879);


void C_GetClip(RgnHandle r);PASCAL_TRAP(GetClip, 0xA87A);


void C_ClipRect(Rect *r);PASCAL_TRAP(ClipRect, 0xA87B);


void C_BackPat(const Pattern *pp);PASCAL_TRAP(BackPat, 0xA87C);


void C_SeedFill(Ptr srcp, Ptr dstp, INTEGER srcr, INTEGER dstr, INTEGER height, INTEGER width, INTEGER seedh, INTEGER seedv);PASCAL_TRAP(SeedFill, 0xA839);


void C_CalcMask(Ptr srcp, Ptr dstp, INTEGER srcr, INTEGER dstr, INTEGER height, INTEGER width);PASCAL_TRAP(CalcMask, 0xA838);


void C_CopyMask(BitMap *srcbp, BitMap *mskbp, BitMap *dstbp, const Rect *srcrp, const Rect *mskrp, const Rect *dstrp);PASCAL_TRAP(CopyMask, 0xA817);


INTEGER* C_GetMaskTable();PASCAL_TRAP(GetMaskTable, 0xA836);


void C_CharExtra(Fixed Extra);PASCAL_TRAP(CharExtra, 0xAA23);


void C_MakeRGBPat(PixPatHandle ph, RGBColor *colorp);PASCAL_TRAP(MakeRGBPat, 0xAA0D);


INTEGER C_Random();PASCAL_TRAP(Random, 0xA861);


Boolean C_GetPixel(INTEGER h, INTEGER v);PASCAL_TRAP(GetPixel, 0xA865);


// Fills *bm with the main device's pixel map as a bitmap: baseAddr,
// rowBytes with the pixel-map flag cleared, bounds (14 bytes).  The
// Quadra ROM's does exactly that; Date & Time points its menu-bar
// clock port's PixMap at the screen with it.
void C_ScrnBitMap(BitMap *bm);PASCAL_TRAP(ScrnBitMap, 0xA833);


void C_StuffHex(Ptr p, ConstStringPtr s);PASCAL_TRAP(StuffHex, 0xA866);


void C_ScalePt(GUEST<Point> *pt, const Rect *srcr, const Rect *dstr);PASCAL_TRAP(ScalePt, 0xA8F8);


void C_MapPt(GUEST<Point> *pt, const Rect *srcr, const Rect *dstr);PASCAL_TRAP(MapPt, 0xA8F9);


void C_MapRect(Rect *r, const Rect *srcr, const Rect *dstr);PASCAL_TRAP(MapRect, 0xA8FA);


void C_MapRgn(RgnHandle rh, const Rect *srcr, const Rect *dstr);PASCAL_TRAP(MapRgn, 0xA8FB);


void C_MapPoly(PolyHandle poly, const Rect *srcr, const Rect *dstr);PASCAL_TRAP(MapPoly, 0xA8FC);


void C_HidePen();PASCAL_TRAP(HidePen, 0xA896);


void C_ShowPen();PASCAL_TRAP(ShowPen, 0xA897);


void C_GetPen(GUEST<Point> *ptp);PASCAL_TRAP(GetPen, 0xA89A);


void C_GetPenState(PenState *ps);PASCAL_TRAP(GetPenState, 0xA898);


void C_SetPenState(PenState *ps);PASCAL_TRAP(SetPenState, 0xA899);


void C_PenSize(INTEGER w, INTEGER h);PASCAL_TRAP(PenSize, 0xA89B);


void C_PenMode(INTEGER m);PASCAL_TRAP(PenMode, 0xA89C);


void C_PenPat(const Pattern *pp);PASCAL_TRAP(PenPat, 0xA89D);


void C_PenNormal();PASCAL_TRAP(PenNormal, 0xA89E);


void C_MoveTo(INTEGER h, INTEGER v);PASCAL_TRAP(MoveTo, 0xA893);


void C_Move(INTEGER dh, INTEGER dv);PASCAL_TRAP(Move, 0xA894);


void C_LineTo(INTEGER h, INTEGER v);PASCAL_TRAP(LineTo, 0xA891);


void C_Line(INTEGER dh, INTEGER dv);PASCAL_TRAP(Line, 0xA892);


void C_DrawPicture(PicHandle pic, const Rect *destrp);PASCAL_TRAP(DrawPicture, 0xA8F6);


PicHandle C_OpenPicture(const Rect *pf);PASCAL_TRAP(OpenPicture, 0xA8F3);


void C_ClosePicture();PASCAL_TRAP(ClosePicture, 0xA8F4);


void C_PicComment(INTEGER kind, INTEGER size, Handle hand);PASCAL_TRAP(PicComment, 0xA8F2);


void C_KillPicture(PicHandle pic);PASCAL_TRAP(KillPicture, 0xA8F5);


void C_AddPt(Point src, GUEST<Point> *dst);PASCAL_TRAP(AddPt, 0xA87E);


void C_SubPt(Point src, GUEST<Point> *dst);PASCAL_TRAP(SubPt, 0xA87F);


void C_SetPt(GUEST<Point> *pt, INTEGER h, INTEGER v);PASCAL_TRAP(SetPt, 0xA880);


Boolean C_EqualPt(Point p1, Point p2);PASCAL_TRAP(EqualPt, 0xA881);


void C_LocalToGlobal(GUEST<Point> *pt);PASCAL_TRAP(LocalToGlobal, 0xA870);


void C_GlobalToLocal(GUEST<Point> *pt);PASCAL_TRAP(GlobalToLocal, 0xA871);


PolyHandle C_OpenPoly();PASCAL_TRAP(OpenPoly, 0xA8CB);


void C_ClosePoly();PASCAL_TRAP(ClosePoly, 0xA8CC);


void C_KillPoly(PolyHandle poly);PASCAL_TRAP(KillPoly, 0xA8CD);


void C_OffsetPoly(PolyHandle poly, INTEGER dh, INTEGER dv);PASCAL_TRAP(OffsetPoly, 0xA8CE);


void C_SetRect(Rect *r, INTEGER left, INTEGER top, INTEGER right, INTEGER bottom);PASCAL_TRAP(SetRect, 0xA8A7);


void C_OffsetRect(Rect *r, INTEGER dh, INTEGER dv);PASCAL_TRAP(OffsetRect, 0xA8A8);


void C_InsetRect(Rect *r, INTEGER dh, INTEGER dv);PASCAL_TRAP(InsetRect, 0xA8A9);


Boolean C_EmptyRect(const Rect *r);PASCAL_TRAP(EmptyRect, 0xA8AE);


Boolean C_SectRect(const Rect *s1, const Rect *s2, Rect *dest);PASCAL_TRAP(SectRect, 0xA8AA);


void C_UnionRect(const Rect *s1, const Rect *s2, Rect *dest);PASCAL_TRAP(UnionRect, 0xA8AB);


Boolean C_PtInRect(Point p, const Rect *r);PASCAL_TRAP(PtInRect, 0xA8AD);


void C_Pt2Rect(Point p1, Point p2, Rect *dest);PASCAL_TRAP(Pt2Rect, 0xA8AC);


void C_PtToAngle(const Rect *rp, Point p, GUEST<INTEGER> *angle);PASCAL_TRAP(PtToAngle, 0xA8C3);


Boolean C_EqualRect(const Rect *r1, const Rect *r2);PASCAL_TRAP(EqualRect, 0xA8A6);


RgnHandle C_NewRgn();PASCAL_TRAP(NewRgn, 0xA8D8);


void C_OpenRgn();PASCAL_TRAP(OpenRgn, 0xA8DA);


void C_CopyRgn(RgnHandle s, RgnHandle d);PASCAL_TRAP(CopyRgn, 0xA8DC);


void C_CloseRgn(RgnHandle rh);PASCAL_TRAP(CloseRgn, 0xA8DB);


void C_DisposeRgn(RgnHandle rh);PASCAL_TRAP(DisposeRgn, 0xA8D9);


void C_SetEmptyRgn(RgnHandle rh);PASCAL_TRAP(SetEmptyRgn, 0xA8DD);


void C_SetRectRgn(RgnHandle rh, INTEGER left, INTEGER top, INTEGER right, INTEGER bottom);PASCAL_TRAP(SetRectRgn, 0xA8DE);


void C_RectRgn(RgnHandle rh, const Rect *rect);PASCAL_TRAP(RectRgn, 0xA8DF);


void C_OffsetRgn(RgnHandle rh, INTEGER dh, INTEGER dv);PASCAL_TRAP(OffsetRgn, 0xA8E0);


Boolean C_PtInRgn(Point p, RgnHandle rh);PASCAL_TRAP(PtInRgn, 0xA8E8);


void C_InsetRgn(RgnHandle rh, INTEGER dh, INTEGER dv);PASCAL_TRAP(InsetRgn, 0xA8E1);


void C_SectRgn(RgnHandle s1, RgnHandle s2, RgnHandle dest);PASCAL_TRAP(SectRgn, 0xA8E4);


void C_UnionRgn(RgnHandle s1, RgnHandle s2, RgnHandle dest);PASCAL_TRAP(UnionRgn, 0xA8E5);


void C_DiffRgn(RgnHandle s1, RgnHandle s2, RgnHandle dest);PASCAL_TRAP(DiffRgn, 0xA8E6);


void C_XorRgn(RgnHandle s1, RgnHandle s2, RgnHandle dest);PASCAL_TRAP(XorRgn, 0xA8E7);


Boolean C_RectInRgn(const Rect *rp, RgnHandle rh);PASCAL_TRAP(RectInRgn, 0xA8E9);


Boolean C_EqualRgn(RgnHandle r1, RgnHandle r2);PASCAL_TRAP(EqualRgn, 0xA8E3);


Boolean C_EmptyRgn(RgnHandle rh);PASCAL_TRAP(EmptyRgn, 0xA8E2);


void C_FrameRect(const Rect *r);PASCAL_TRAP(FrameRect, 0xA8A1);


void C_PaintRect(const Rect *r);PASCAL_TRAP(PaintRect, 0xA8A2);


void C_EraseRect(const Rect *r);PASCAL_TRAP(EraseRect, 0xA8A3);


void C_InvertRect(const Rect *r);PASCAL_TRAP(InvertRect, 0xA8A4);


void C_FillRect(const Rect *r, const Pattern *pat);PASCAL_TRAP(FillRect, 0xA8A5);


void C_FrameOval(const Rect *r);PASCAL_TRAP(FrameOval, 0xA8B7);


void C_PaintOval(const Rect *r);PASCAL_TRAP(PaintOval, 0xA8B8);


void C_EraseOval(const Rect *r);PASCAL_TRAP(EraseOval, 0xA8B9);


void C_InvertOval(const Rect *r);PASCAL_TRAP(InvertOval, 0xA8BA);


void C_FillOval(const Rect *r, const Pattern *pat);PASCAL_TRAP(FillOval, 0xA8BB);


void C_FrameRoundRect(const Rect *r, INTEGER ow, INTEGER oh);PASCAL_TRAP(FrameRoundRect, 0xA8B0);


void C_PaintRoundRect(const Rect *r, INTEGER ow, INTEGER oh);PASCAL_TRAP(PaintRoundRect, 0xA8B1);


void C_EraseRoundRect(const Rect *r, INTEGER ow, INTEGER oh);PASCAL_TRAP(EraseRoundRect, 0xA8B2);


void C_InvertRoundRect(const Rect *r, INTEGER ow, INTEGER oh);PASCAL_TRAP(InvertRoundRect, 0xA8B3);


void C_FillRoundRect(const Rect *r, INTEGER ow, INTEGER oh, const Pattern *pat);PASCAL_TRAP(FillRoundRect, 0xA8B4);


void C_FrameArc(const Rect *r, INTEGER start, INTEGER angle);PASCAL_TRAP(FrameArc, 0xA8BE);


void C_PaintArc(const Rect *r, INTEGER start, INTEGER angle);PASCAL_TRAP(PaintArc, 0xA8BF);


void C_EraseArc(const Rect *r, INTEGER start, INTEGER angle);PASCAL_TRAP(EraseArc, 0xA8C0);


void C_InvertArc(const Rect *r, INTEGER start, INTEGER angle);PASCAL_TRAP(InvertArc, 0xA8C1);


void C_FillArc(const Rect *r, INTEGER start, INTEGER angle, const Pattern *pat);PASCAL_TRAP(FillArc, 0xA8C2);


void C_FrameRgn(RgnHandle rh);PASCAL_TRAP(FrameRgn, 0xA8D2);


void C_PaintRgn(RgnHandle rh);PASCAL_TRAP(PaintRgn, 0xA8D3);


void C_EraseRgn(RgnHandle rh);PASCAL_TRAP(EraseRgn, 0xA8D4);


void C_InvertRgn(RgnHandle rh);PASCAL_TRAP(InvertRgn, 0xA8D5);


void C_FillRgn(RgnHandle rh, const Pattern *pat);PASCAL_TRAP(FillRgn, 0xA8D6);


void C_FramePoly(PolyHandle poly);PASCAL_TRAP(FramePoly, 0xA8C6);


void C_PaintPoly(PolyHandle poly);PASCAL_TRAP(PaintPoly, 0xA8C7);


void C_ErasePoly(PolyHandle poly);PASCAL_TRAP(ErasePoly, 0xA8C8);


void C_InvertPoly(PolyHandle poly);PASCAL_TRAP(InvertPoly, 0xA8C9);


void C_FillPoly(PolyHandle poly, const Pattern *pat);PASCAL_TRAP(FillPoly, 0xA8CA);


void C_SetStdProcs(QDProcs *procs);PASCAL_TRAP(SetStdProcs, 0xA8EA);


void C_StdArc(GrafVerb verb, const Rect *r, INTEGER starta, INTEGER arca);PASCAL_TRAP(StdArc, 0xA8BD);


void C_StdBits(const BitMap *srcbmp, const Rect *srcrp, const Rect *dstrp, INTEGER mode, RgnHandle mask);PASCAL_TRAP(StdBits, 0xA8EB);


void C_StdLine(Point p);PASCAL_TRAP(StdLine, 0xA890);


void C_StdOval(GrafVerb v, const Rect *rp);PASCAL_TRAP(StdOval, 0xA8B6);


void C_StdComment(INTEGER kind, INTEGER size, Handle hand);PASCAL_TRAP(StdComment, 0xA8F1);


void C_StdGetPic(void *dp, INTEGER bc);PASCAL_TRAP(StdGetPic, 0xA8EE);


void C_StdPutPic(const void *sp, INTEGER bc);PASCAL_TRAP(StdPutPic, 0xA8F0);


void C_StdPoly(GrafVerb verb, PolyHandle ph);PASCAL_TRAP(StdPoly, 0xA8C5);


void C_StdRRect(GrafVerb verb, const Rect *r, INTEGER width, INTEGER height);PASCAL_TRAP(StdRRect, 0xA8AF);


void C_StdRect(GrafVerb v, const Rect *rp);PASCAL_TRAP(StdRect, 0xA8A0);


void C_StdRgn(GrafVerb verb, RgnHandle rgn);PASCAL_TRAP(StdRgn, 0xA8D1);


void C_StdText(INTEGER n, Ptr textbufp, Point num, Point den);PASCAL_TRAP(StdText, 0xA882);


INTEGER C_StdTxMeas(INTEGER n, Ptr p, GUEST<Point> *nump, GUEST<Point> *denp, FontInfo *finfop);PASCAL_TRAP(StdTxMeas, 0xA8ED);


void C_MeasureText(INTEGER n, Ptr text, Ptr chars);PASCAL_TRAP(MeasureText, 0xA837);


void C_TextFont(INTEGER f);PASCAL_TRAP(TextFont, 0xA887);


void C_TextFace(INTEGER thef);PASCAL_TRAP(TextFace, 0xA888);


void C_TextMode(INTEGER m);PASCAL_TRAP(TextMode, 0xA889);


void C_TextSize(INTEGER s);PASCAL_TRAP(TextSize, 0xA88A);


void C_SpaceExtra(Fixed e);PASCAL_TRAP(SpaceExtra, 0xA88E);


void C_DrawChar(CharParameter thec);PASCAL_TRAP(DrawChar, 0xA883);


void C_DrawString(ConstStringPtr s);PASCAL_TRAP(DrawString, 0xA884);


void C_DrawText(Ptr tb, INTEGER fb, INTEGER bc);PASCAL_TRAP(DrawText, 0xA885);


INTEGER C_CharWidth(CharParameter thec);PASCAL_TRAP(CharWidth, 0xA88D);


INTEGER C_StringWidth(ConstStringPtr s);PASCAL_TRAP(StringWidth, 0xA88C);


INTEGER C_TextWidth(Ptr tb, INTEGER fb, INTEGER bc);PASCAL_TRAP(TextWidth, 0xA886);


void C_GetFontInfo(FontInfo *ip);PASCAL_TRAP(GetFontInfo, 0xA88B);


void C_GetCPixel(INTEGER h, INTEGER v, RGBColor *colorp);PASCAL_TRAP(GetCPixel, 0xAA17);


void C_SetCPixel(INTEGER h, INTEGER v, RGBColor *colorp);PASCAL_TRAP(SetCPixel, 0xAA16);


void C_SeedCFill(BitMap *srcbp, BitMap *dstbp, const Rect *srcrp, const Rect *dstrp, INTEGER seedh, INTEGER seedv, ProcPtr matchprocp, LONGINT matchdata);PASCAL_TRAP(SeedCFill, 0xAA50);


void C_CalcCMask(BitMap *srcbp, BitMap *dstbp, const Rect *srcrp, const Rect *dstrp, RGBColor *seedrgbp, ProcPtr matchprocp, LONGINT matchdata);PASCAL_TRAP(CalcCMask, 0xAA4F);


void C_CopyDeepMask(BitMap *srcBits, BitMap *maskBits, BitMap *dstBits, const Rect *srcRect, const Rect *maskRect, const Rect *dstRect, INTEGER mode, RgnHandle maskRgn);PASCAL_TRAP(CopyDeepMask, 0xAA51);


BEGIN_EXECUTOR_ONLY
inline QDGlobals& qdGlobals()
{
    Ptr thePortPtr = *(GUEST<Ptr> *)SYN68K_TO_US(EM_A5);
    return *(QDGlobals*)(thePortPtr - offsetof(QDGlobals, thePort));
}
END_EXECUTOR_ONLY











} /* namespace Executor */