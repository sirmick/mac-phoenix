/*****************************************************************************
 *                                                                           *
 *                                CQuickDraw.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     LONGINT, INTEGER, Rect, Handle, Fixed, Boolean,        *
 *                    OSErr, SmallFract, Str255, Ptr                         *
 *     QuickDraw.h    PixMapHandle, RGBColor, GrafPtr, CGrafPort,            *
 *                    CTabHandle, CQDProcs, CGrafPtr, PixPatHandle,          *
 *                    RgnHandle, PolyHandle, ColorSpec, CMYColor,            *
 *                    HSLColor, HSVColor, CCrsrHandle, BitMap, PicHandle     *
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
#include "QuickDraw.h"
#include "WindowMgr.h"

#define MODULE_NAME CQuickDraw
#include <base/api-module.h>


namespace Executor {

BEGIN_EXECUTOR_ONLY
#define theCPort  (**(GUEST<GUEST<CGrafPtr> *> *)SYN68K_TO_US(EM_A5))
END_EXECUTOR_ONLY


enum {minSeed = 1024,
};

struct ITab{GUEST_STRUCT;GUEST<LONGINT> iTabSeed;GUEST<INTEGER> iTabRes;uint8_t iTTable[1];};static_assert(sizeof(ITab) == 8);void describeStruct(const ITab&, std::ostream&);

// can't use [0];
//      make this an unsigned char even tho the mac has SignedByte;
//      it is treated as unsigned
typedef ITab *ITabPtr;

typedef GUEST<ITabPtr> *ITabHandle;

struct GDevice;

typedef GDevice *GDPtr;

typedef GDPtr GDevicePtr;

typedef GUEST<GDevicePtr> *GDHandle;

struct SProcRec;

typedef SProcRec *SProcPtr;

typedef GUEST<SProcPtr> *SProcHndl;

struct SProcRec{GUEST_STRUCT;GUEST<SProcHndl> nxtSrch;GUEST<ProcPtr> srchProc;};static_assert(sizeof(SProcRec) == 8);void describeStruct(const SProcRec&, std::ostream&);

struct CProcRec;

typedef CProcRec *CProcPtr;

typedef GUEST<CProcPtr> *CProcHndl;

struct CProcRec{GUEST_STRUCT;GUEST<CProcHndl> nxtComp;GUEST<ProcPtr> compProc;};static_assert(sizeof(CProcRec) == 8);void describeStruct(const CProcRec&, std::ostream&);

using DeviceLoopDrawingUPP = UPP<void(INTEGER depth, INTEGER deviceFlags, GDHandle targetDevice, LONGINT userData)>;


struct GDevice{GUEST_STRUCT;GUEST<INTEGER> gdRefNum;GUEST<INTEGER> gdID;GUEST<INTEGER> gdType;GUEST<ITabHandle> gdITable;GUEST<INTEGER> gdResPref;GUEST<SProcHndl> gdSearchProc;GUEST<CProcHndl> gdCompProc;GUEST<INTEGER> gdFlags;GUEST<PixMapHandle> gdPMap;GUEST<LONGINT> gdRefCon;GUEST<GDHandle> gdNextGD;Rect gdRect;GUEST<LONGINT> gdMode;GUEST<INTEGER> gdCCBytes;GUEST<INTEGER> gdCCDepth;GUEST<Handle> gdCCXData;GUEST<Handle> gdCCXMask;GUEST<LONGINT> gdReserved;};static_assert(sizeof(GDevice) == 62);void describeStruct(const GDevice&, std::ostream&);

typedef uint32_t DeviceLoopFlags;

// DeviceLoop flags.
enum {singleDevices = 1 << 0,
dontMatchSeeds = 1 << 1,
allDevices = 1 << 2,
};

struct ColorInfo{GUEST_STRUCT;RGBColor ciRGB;GUEST<INTEGER> ciUsage;GUEST<INTEGER> ciTolerance;GUEST<INTEGER> ciFlags;GUEST<LONGINT> ciPrivate;};static_assert(sizeof(ColorInfo) == 16);void describeStruct(const ColorInfo&, std::ostream&);

struct Palette{GUEST_STRUCT;GUEST<INTEGER> pmEntries;GUEST<GrafPtr> pmWindow;GUEST<INTEGER> pmPrivate;GUEST<LONGINT> pmDevices; // Handle?
GUEST<Handle> pmSeeds;ColorInfo pmInfo[1];};static_assert(sizeof(Palette) == 32);void describeStruct(const Palette&, std::ostream&);

typedef Palette *PalettePtr;

enum {CI_USAGE_TYPE_BITS = 15,
};

typedef enum {pmCourteous = 0,
pmDithered = 1,
pmTolerant = 2,
pmAnimated = 4,
pmExplicit = 8,
pmInhibitG2 = 256,
pmInhibitC2 = 512,
pmInhibitG4 = 1024,
pmInhibitC4 = 2048,
pmInhibitG8 = 4096,
pmInhibitC8 = 8192,
}pmColorUsage;

typedef enum {pmNoUpdates = 32768,
pmBkUpdates = 40960,
pmFgUpdates = 49152,
pmAllUpdates = 57344,
}pmUpdates;

typedef GUEST<PalettePtr> *PaletteHandle;

BEGIN_EXECUTOR_ONLY
/* return true if `maybe_graphics_world' points to a graphics world,
   and not a graf port or cgraf port */
#define GRAPHICS_WORLD_P(maybe_graphics_world) \
    (CGrafPort_p(maybe_graphics_world)         \
     && CPORT_VERSION(maybe_graphics_world) & GW_FLAG_BIT)
#define GW_CPORT(graphics_world) ((CGrafPtr)(graphics_world))
END_EXECUTOR_ONLY


typedef LONGINT GWorldFlags;

typedef CGrafPort GWorld;

typedef CGrafPort *GWorldPtr;

struct ReqListRec{GUEST_STRUCT;GUEST<INTEGER> reqLSize;GUEST<INTEGER> reqLData[1];};static_assert(sizeof(ReqListRec) == 4);void describeStruct(const ReqListRec&, std::ostream&);

// extended version 2 picture datastructures
struct OpenCPicParams{GUEST_STRUCT;Rect srcRect;GUEST<Fixed> hRes;GUEST<Fixed> vRes;GUEST<int16_t> version;GUEST<int16_t> reserved1;GUEST<int32_t> reserved2;};static_assert(sizeof(OpenCPicParams) == 24);void describeStruct(const OpenCPicParams&, std::ostream&);

struct CommonSpec{GUEST_STRUCT;GUEST<int16_t> count;GUEST<int16_t> ID;};void describeStruct(const CommonSpec&, std::ostream&);

typedef CommonSpec CommentSpec;

typedef GUEST<CommentSpec> *CommentSpecPtr;

typedef GUEST<CommentSpecPtr> *CommentSpecHandle;

struct FontSpec{GUEST_STRUCT;GUEST<int16_t> pictFontID;GUEST<int16_t> sysFontID;GUEST<int32_t> size[4];GUEST<int16_t> style;GUEST<int32_t> nameOffset;};static_assert(sizeof(FontSpec) == 26);void describeStruct(const FontSpec&, std::ostream&);

typedef FontSpec *FontSpecPtr;

typedef GUEST<FontSpecPtr> *FontSpecHandle;

struct PictInfo{GUEST_STRUCT;GUEST<int16_t> version; // 0
GUEST<int32_t> uniqueColors; // 2
GUEST<PaletteHandle> thePalette; // 6
GUEST<CTabHandle> theColorTable; // 10
GUEST<Fixed> hRes; // 14
GUEST<Fixed> vRes; // 18
GUEST<INTEGER> depth; // 22
Rect sourceRect; // top24, left26, bottom28, right30
GUEST<int32_t> textCount; // 32
GUEST<int32_t> lineCount;GUEST<int32_t> rectCount;GUEST<int32_t> rRectCount;GUEST<int32_t> ovalCount;GUEST<int32_t> arcCount;GUEST<int32_t> polyCount;GUEST<int32_t> regionCount;GUEST<int32_t> bitMapCount;GUEST<int32_t> pixMapCount;GUEST<int32_t> commentCount;GUEST<int32_t> uniqueComments;GUEST<CommentSpecHandle> commentHandle;GUEST<int32_t> uniqueFonts;GUEST<FontSpecHandle> fontHandle;GUEST<Handle> fontNamesHandle;GUEST<int32_t> reserved1;GUEST<int32_t> reserved2;};static_assert(sizeof(PictInfo) == 104);void describeStruct(const PictInfo&, std::ostream&);

typedef PictInfo *PictInfoPtr;

typedef GUEST<PictInfoPtr> *PictInfoHandle;

typedef int32_t PictInfoID;

enum {RGBDirect = 16,
Indirect = 0,
};

// a pixmap pixelType of `native_rgb_pixel_type' means that the format
//    of the pixmap is the same as that of the screen
enum {vdriver_rgb_pixel_type = 185,
};

enum {pixPurge = 1 << 0,
noNewDevice = 1 << 1,
useTempMem = 1 << 2,
keepLocal = 1 << 3,
pixelsPurgeable = 1 << 6,
pixelsLocked = 1 << 7,
mapPix = 1 << 16,
newDepth = 1 << 17,
alignPix = 1 << 18,
newRowBytes = 1 << 19,
reallocPix = 1 << 20,
clipPix = 1 << 28,
stretchPix = 1 << 29,
ditherPix = 1 << 30,
gwFlagErr = 1 << 31,
};

typedef int16_t QDErr;

// error codes returned by QDError
enum {noMemForPictPlaybackErr = -145,
regionTooBigErr = -147,
pixmapTooDeepErr = -148,
nsStackErr = -149,
cMatchErr = -150,
cTempMemErr = -151,
cNoMemErr = -152,
cRangeErr = -153,
cProtectErr = -154,
cDevErr = -155,
cResErr = -156,
cDepthErr = -157,
rgnTooBigErr = -500,
};

// TODO:  FIXME -- #warning find out correct value for colReqErr
// -158 is just a guess
enum {colReqErr = -158,
};

// MacPhoenix: black (0,0,0) on a real 7.5.5 boot; Key Caps passes its
// address to RGBForeColor.
const LowMemGlobal<RGBColor > RGBBlack{ 0xC10 };
inline void LMGetRGBBlack(RGBColor *val) { *val = LM(RGBBlack); }
inline void LMSetRGBBlack(const RGBColor *val) { LM(RGBBlack) = *val; }
NOTRAP_FUNCTION2(LMGetRGBBlack);
NOTRAP_FUNCTION2(LMSetRGBBlack);


// MacPhoenix: white on a real 7.5.5 boot; Key Caps passes its address
// to RGBBackColor.
const LowMemGlobal<RGBColor > RGBWhite{ 0xC16 };
inline void LMGetRGBWhite(RGBColor *val) { *val = LM(RGBWhite); }
inline void LMSetRGBWhite(const RGBColor *val) { LM(RGBWhite) = *val; }
NOTRAP_FUNCTION2(LMGetRGBWhite);
NOTRAP_FUNCTION2(LMSetRGBWhite);


// QuickDraw IMV (true);
const LowMemGlobal<GDHandle > TheGDevice{ 0xCC8 };
inline GDHandle  LMGetTheGDevice() { return LM(TheGDevice); }
inline void LMSetTheGDevice(GDHandle val) { LM(TheGDevice) = val; }
NOTRAP_FUNCTION2(LMGetTheGDevice);
NOTRAP_FUNCTION2(LMSetTheGDevice);


// QuickDraw IMV (true);
const LowMemGlobal<GDHandle > MainDevice{ 0x8A4 };
inline GDHandle  LMGetMainDevice() { return LM(MainDevice); }
inline void LMSetMainDevice(GDHandle val) { LM(MainDevice) = val; }
NOTRAP_FUNCTION2(LMGetMainDevice);
NOTRAP_FUNCTION2(LMSetMainDevice);


// QuickDraw IMV (true);
const LowMemGlobal<GDHandle > DeviceList{ 0x8A8 };
inline GDHandle  LMGetDeviceList() { return LM(DeviceList); }
inline void LMSetDeviceList(GDHandle val) { LM(DeviceList) = val; }
NOTRAP_FUNCTION2(LMGetDeviceList);
NOTRAP_FUNCTION2(LMSetDeviceList);


// QuickDraw IMV-62 (true);
const LowMemGlobal<RGBColor > HiliteRGB{ 0xDA0 };
inline void LMGetHiliteRGB(RGBColor *val) { *val = LM(HiliteRGB); }
inline void LMSetHiliteRGB(const RGBColor *val) { LM(HiliteRGB) = *val; }
NOTRAP_FUNCTION2(LMGetHiliteRGB);
NOTRAP_FUNCTION2(LMSetHiliteRGB);


// D0W? D0<0xFF> ###
DISPATCHER_TRAP(PaletteDispatch, 0xAAA2, D0<0xFF>);


DISPATCHER_TRAP(Pack12, 0xA82E, StackW);


DISPATCHER_TRAP(QDExtensions, 0xAB1D, D0L);


// D0<0xFF>? ###
DISPATCHER_TRAP(Pack15, 0xA831, D0W);


void C_SetStdCProcs(CQDProcs *cProcs);PASCAL_TRAP(SetStdCProcs, 0xAA4E);


void C_OpenCPort(CGrafPtr );PASCAL_TRAP(OpenCPort, 0xAA00);


void C_InitCPort(CGrafPtr );PASCAL_TRAP(InitCPort, 0xAA01);


void C_CloseCPort(CGrafPtr );PASCAL_TRAP(CloseCPort, 0xAA02);


void C_RGBForeColor(RGBColor *);PASCAL_TRAP(RGBForeColor, 0xAA14);


void C_RGBBackColor(RGBColor *);PASCAL_TRAP(RGBBackColor, 0xAA15);


void C_GetForeColor(RGBColor *);PASCAL_TRAP(GetForeColor, 0xAA19);


void C_GetBackColor(RGBColor *);PASCAL_TRAP(GetBackColor, 0xAA1A);


void C_PenPixPat(PixPatHandle );PASCAL_TRAP(PenPixPat, 0xAA0A);


void C_BackPixPat(PixPatHandle );PASCAL_TRAP(BackPixPat, 0xAA0B);


void C_OpColor(RGBColor *);PASCAL_TRAP(OpColor, 0xAA21);


void C_HiliteColor(RGBColor *);PASCAL_TRAP(HiliteColor, 0xAA22);


PixMapHandle C_NewPixMap();PASCAL_TRAP(NewPixMap, 0xAA03);


void C_DisposePixMap(PixMapHandle );PASCAL_TRAP(DisposePixMap, 0xAA04);


void C_CopyPixMap(PixMapHandle , PixMapHandle );PASCAL_TRAP(CopyPixMap, 0xAA05);


PixPatHandle C_NewPixPat();PASCAL_TRAP(NewPixPat, 0xAA07);


void C_DisposePixPat(PixPatHandle );PASCAL_TRAP(DisposePixPat, 0xAA08);


void C_CopyPixPat(PixPatHandle , PixPatHandle );PASCAL_TRAP(CopyPixPat, 0xAA09);


void C_SetPortPix(PixMapHandle );PASCAL_TRAP(SetPortPix, 0xAA06);


GDHandle C_NewGDevice(INTEGER , LONGINT );PASCAL_TRAP(NewGDevice, 0xAA2F);


void C_InitGDevice(INTEGER , LONGINT , GDHandle );PASCAL_TRAP(InitGDevice, 0xAA2E);


void C_SetDeviceAttribute(GDHandle , INTEGER , Boolean );PASCAL_TRAP(SetDeviceAttribute, 0xAA2D);


void C_SetGDevice(GDHandle );PASCAL_TRAP(SetGDevice, 0xAA31);


void C_DisposeGDevice(GDHandle );PASCAL_TRAP(DisposeGDevice, 0xAA30);


GDHandle C_GetGDevice();PASCAL_TRAP(GetGDevice, 0xAA32);


GDHandle C_GetDeviceList();PASCAL_TRAP(GetDeviceList, 0xAA29);


GDHandle C_GetMainDevice();PASCAL_TRAP(GetMainDevice, 0xAA2A);


GDHandle C_GetMaxDevice(const Rect *);PASCAL_TRAP(GetMaxDevice, 0xAA27);


GDHandle C_GetNextDevice(GDHandle );PASCAL_TRAP(GetNextDevice, 0xAA2B);


void C_DeviceLoop(RgnHandle , DeviceLoopDrawingUPP , LONGINT , DeviceLoopFlags );PASCAL_TRAP(DeviceLoop, 0xABCA);


Boolean C_TestDeviceAttribute(GDHandle , INTEGER );PASCAL_TRAP(TestDeviceAttribute, 0xAA2C);


void C_ScreenRes(GUEST<INTEGER> *, GUEST<INTEGER> *);NOTRAP_FUNCTION(ScreenRes);


INTEGER C_HasDepth(GDHandle , INTEGER , INTEGER , INTEGER );PASCAL_SUBTRAP(HasDepth, 0xAAA2, 0x0A14, PaletteDispatch);


OSErr C_SetDepth(GDHandle , INTEGER , INTEGER , INTEGER );PASCAL_SUBTRAP(SetDepth, 0xAAA2, 0x0A13, PaletteDispatch);


void C_MakeITable(CTabHandle , ITabHandle , INTEGER );PASCAL_TRAP(MakeITable, 0xAA39);


LONGINT C_Color2Index(RGBColor *);PASCAL_TRAP(Color2Index, 0xAA33);


void C_Index2Color(LONGINT , RGBColor *);PASCAL_TRAP(Index2Color, 0xAA34);


LONGINT C_GetCTSeed();PASCAL_TRAP(GetCTSeed, 0xAA28);


void C_GetSubTable(CTabHandle , INTEGER , CTabHandle );PASCAL_TRAP(GetSubTable, 0xAA37);


void C_FillCRoundRect(const Rect *, int16_t , int16_t , PixPatHandle );PASCAL_TRAP(FillCRoundRect, 0xAA10);


void C_FillCRect(const Rect *, PixPatHandle );PASCAL_TRAP(FillCRect, 0xAA0E);


void C_FillCOval(const Rect *, PixPatHandle );PASCAL_TRAP(FillCOval, 0xAA0F);


void C_FillCArc(const Rect *, int16_t , int16_t , PixPatHandle );PASCAL_TRAP(FillCArc, 0xAA11);


void C_FillCPoly(PolyHandle , PixPatHandle );PASCAL_TRAP(FillCPoly, 0xAA13);


void C_FillCRgn(RgnHandle , PixPatHandle );PASCAL_TRAP(FillCRgn, 0xAA12);


void C_InvertColor(RGBColor *);PASCAL_TRAP(InvertColor, 0xAA35);


Boolean C_RealColor(RGBColor *);PASCAL_TRAP(RealColor, 0xAA36);


void C_ProtectEntry(INTEGER , Boolean );PASCAL_TRAP(ProtectEntry, 0xAA3D);


void C_ReserveEntry(INTEGER , Boolean );PASCAL_TRAP(ReserveEntry, 0xAA3E);


void C_SetEntries(INTEGER , INTEGER , ColorSpec *cSpecArray);PASCAL_TRAP(SetEntries, 0xAA3F);


void C_AddSearch(ProcPtr );PASCAL_TRAP(AddSearch, 0xAA3A);


void C_AddComp(ProcPtr );PASCAL_TRAP(AddComp, 0xAA3B);


void C_DelSearch(ProcPtr );PASCAL_TRAP(DelSearch, 0xAA4C);


void C_DelComp(ProcPtr );PASCAL_TRAP(DelComp, 0xAA4D);


void C_SetClientID(INTEGER );PASCAL_TRAP(SetClientID, 0xAA3C);


Boolean C_GetGray(GDHandle , RGBColor *, RGBColor *);PASCAL_SUBTRAP(GetGray, 0xAAA2, 0x0C19, PaletteDispatch);


PixPatHandle C_GetPixPat(INTEGER );PASCAL_TRAP(GetPixPat, 0xAA0C);


INTEGER C_QDError();PASCAL_TRAP(QDError, 0xAA40);


void C_CMY2RGB(CMYColor *, RGBColor *);PASCAL_SUBTRAP(CMY2RGB, 0xA82E, 0x0003, Pack12);


void C_RGB2CMY(RGBColor *, CMYColor *);PASCAL_SUBTRAP(RGB2CMY, 0xA82E, 0x0004, Pack12);


void C_HSL2RGB(HSLColor *, RGBColor *);PASCAL_SUBTRAP(HSL2RGB, 0xA82E, 0x0005, Pack12);


void C_RGB2HSL(RGBColor *, HSLColor *);PASCAL_SUBTRAP(RGB2HSL, 0xA82E, 0x0006, Pack12);


void C_HSV2RGB(HSVColor *, RGBColor *);PASCAL_SUBTRAP(HSV2RGB, 0xA82E, 0x0007, Pack12);


void C_RGB2HSV(RGBColor *, HSVColor *);PASCAL_SUBTRAP(RGB2HSV, 0xA82E, 0x0008, Pack12);


SmallFract C_Fix2SmallFract(Fixed );PASCAL_SUBTRAP(Fix2SmallFract, 0xA82E, 0x0001, Pack12);


Fixed C_SmallFract2Fix(SmallFract );PASCAL_SUBTRAP(SmallFract2Fix, 0xA82E, 0x0002, Pack12);


Boolean C_GetColor(Point , Str255 , RGBColor *, RGBColor *);PASCAL_SUBTRAP(GetColor, 0xA82E, 0x0009, Pack12);


CTabHandle C_GetCTable(INTEGER );PASCAL_TRAP(GetCTable, 0xAA18);


void C_DisposeCTable(CTabHandle );PASCAL_TRAP(DisposeCTable, 0xAA24);


void C_InitPalettes();PASCAL_TRAP(InitPalettes, 0xAA90);


PaletteHandle C_NewPalette(INTEGER , CTabHandle , INTEGER , INTEGER );PASCAL_TRAP(NewPalette, 0xAA91);


PaletteHandle C_GetNewPalette(INTEGER );PASCAL_TRAP(GetNewPalette, 0xAA92);


void C_DisposePalette(PaletteHandle );PASCAL_TRAP(DisposePalette, 0xAA93);


void C_ActivatePalette(WindowPtr );PASCAL_TRAP(ActivatePalette, 0xAA94);


void C_SetPalette(WindowPtr , PaletteHandle , Boolean );NOTRAP_FUNCTION(SetPalette);


void C_NSetPalette(WindowPtr , PaletteHandle , INTEGER updates);PASCAL_TRAP(NSetPalette, 0xAA95);


PaletteHandle C_GetPalette(WindowPtr );PASCAL_TRAP(GetPalette, 0xAA96);


void C_PmForeColor(INTEGER );PASCAL_TRAP(PmForeColor, 0xAA97);


void C_PmBackColor(INTEGER );PASCAL_TRAP(PmBackColor, 0xAA98);


void C_AnimateEntry(WindowPtr , INTEGER , RGBColor *);PASCAL_TRAP(AnimateEntry, 0xAA99);


void C_AnimatePalette(WindowPtr , CTabHandle , INTEGER , INTEGER , INTEGER );PASCAL_TRAP(AnimatePalette, 0xAA9A);


void C_GetEntryColor(PaletteHandle , INTEGER , RGBColor *);PASCAL_TRAP(GetEntryColor, 0xAA9B);


void C_SetEntryColor(PaletteHandle , INTEGER , RGBColor *);PASCAL_TRAP(SetEntryColor, 0xAA9C);


void C_GetEntryUsage(PaletteHandle , INTEGER , GUEST<INTEGER> *, GUEST<INTEGER> *);PASCAL_TRAP(GetEntryUsage, 0xAA9D);


void C_SetEntryUsage(PaletteHandle , INTEGER , INTEGER , INTEGER );PASCAL_TRAP(SetEntryUsage, 0xAA9E);


void C_CTab2Palette(CTabHandle , PaletteHandle , INTEGER , INTEGER );PASCAL_TRAP(CTab2Palette, 0xAA9F);


void C_Palette2CTab(PaletteHandle , CTabHandle );PASCAL_TRAP(Palette2CTab, 0xAAA0);


CCrsrHandle C_GetCCursor(INTEGER );PASCAL_TRAP(GetCCursor, 0xAA1B);


void C_SetCCursor(CCrsrHandle );PASCAL_TRAP(SetCCursor, 0xAA1C);


void C_DisposeCCursor(CCrsrHandle );PASCAL_TRAP(DisposeCCursor, 0xAA26);


void C_AllocCursor();PASCAL_TRAP(AllocCursor, 0xAA1D);


void C_RestoreDeviceClut(GDHandle );PASCAL_SUBTRAP(RestoreDeviceClut, 0xAAA2, 0x0002, PaletteDispatch);


void C_ResizePalette(PaletteHandle , INTEGER );PASCAL_SUBTRAP(ResizePalette, 0xAAA2, 0x0003, PaletteDispatch);


INTEGER C_PMgrVersion();PASCAL_SUBTRAP(PMgrVersion, 0xAAA2, 0x0015, PaletteDispatch);


void C_SaveFore(ColorSpec *);PASCAL_SUBTRAP(SaveFore, 0xAAA2, 0x040D, PaletteDispatch);


void C_RestoreFore(ColorSpec *);PASCAL_SUBTRAP(RestoreFore, 0xAAA2, 0x040F, PaletteDispatch);


void C_SaveBack(ColorSpec *);PASCAL_SUBTRAP(SaveBack, 0xAAA2, 0x040E, PaletteDispatch);


void C_RestoreBack(ColorSpec *);PASCAL_SUBTRAP(RestoreBack, 0xAAA2, 0x0410, PaletteDispatch);


void C_SetPaletteUpdates(PaletteHandle , INTEGER );PASCAL_SUBTRAP(SetPaletteUpdates, 0xAAA2, 0x0616, PaletteDispatch);


INTEGER C_GetPaletteUpdates(PaletteHandle );PASCAL_SUBTRAP(GetPaletteUpdates, 0xAAA2, 0x0417, PaletteDispatch);


void C_CopyPalette(PaletteHandle src_palette, PaletteHandle dst_palette, int16_t src_start, int16_t dst_start, int16_t n_entries);PASCAL_TRAP(CopyPalette, 0xAAA1);


void C_GetCWMgrPort(GUEST<CGrafPtr> *);PASCAL_TRAP(GetCWMgrPort, 0xAA48);


// QDExtensions trap
QDErr C_NewGWorld(GUEST<GWorldPtr> *, INTEGER , const Rect *, CTabHandle , GDHandle , GWorldFlags );PASCAL_SUBTRAP(NewGWorld, 0xAB1D, 0x160000, QDExtensions);


Boolean C_LockPixels(PixMapHandle );PASCAL_SUBTRAP(LockPixels, 0xAB1D, 0x40001, QDExtensions);


void C_UnlockPixels(PixMapHandle );PASCAL_SUBTRAP(UnlockPixels, 0xAB1D, 0x40002, QDExtensions);


GWorldFlags C_UpdateGWorld(GUEST<GWorldPtr> *, INTEGER , const Rect *, CTabHandle , GDHandle , GWorldFlags );PASCAL_SUBTRAP(UpdateGWorld, 0xAB1D, 0x160003, QDExtensions);


void C_DisposeGWorld(GWorldPtr );PASCAL_SUBTRAP(DisposeGWorld, 0xAB1D, 0x40004, QDExtensions);


void C_GetGWorld(GUEST<CGrafPtr> *, GUEST<GDHandle> *);PASCAL_SUBTRAP(GetGWorld, 0xAB1D, 0x80005, QDExtensions);


void C_SetGWorld(CGrafPtr , GDHandle );PASCAL_SUBTRAP(SetGWorld, 0xAB1D, 0x80006, QDExtensions);


void C_AllowPurgePixels(PixMapHandle );PASCAL_SUBTRAP(AllowPurgePixels, 0xAB1D, 0x4000B, QDExtensions);


void C_NoPurgePixels(PixMapHandle );PASCAL_SUBTRAP(NoPurgePixels, 0xAB1D, 0x4000C, QDExtensions);


GWorldFlags C_GetPixelsState(PixMapHandle );PASCAL_SUBTRAP(GetPixelsState, 0xAB1D, 0x4000D, QDExtensions);


void C_SetPixelsState(PixMapHandle , GWorldFlags );PASCAL_SUBTRAP(SetPixelsState, 0xAB1D, 0x8000E, QDExtensions);


Ptr C_GetPixBaseAddr(PixMapHandle );PASCAL_SUBTRAP(GetPixBaseAddr, 0xAB1D, 0x4000F, QDExtensions);


QDErr C_NewScreenBuffer(const Rect *, Boolean , GUEST<GDHandle> *, GUEST<PixMapHandle> *);PASCAL_SUBTRAP(NewScreenBuffer, 0xAB1D, 0xE0010, QDExtensions);


void C_DisposeScreenBuffer(PixMapHandle );PASCAL_SUBTRAP(DisposeScreenBuffer, 0xAB1D, 0x40011, QDExtensions);


GDHandle C_GetGWorldDevice(GWorldPtr );PASCAL_SUBTRAP(GetGWorldDevice, 0xAB1D, 0x40012, QDExtensions);


Boolean C_PixMap32Bit(PixMapHandle );PASCAL_SUBTRAP(PixMap32Bit, 0xAB1D, 0x40016, QDExtensions);


PixMapHandle C_GetGWorldPixMap(GWorldPtr );PASCAL_SUBTRAP(GetGWorldPixMap, 0xAB1D, 0x40017, QDExtensions);


QDErr C_NewTempScreenBuffer(const Rect *, Boolean , GUEST<GDHandle> *, GUEST<PixMapHandle> *);PASCAL_SUBTRAP(NewTempScreenBuffer, 0xAB1D, 0xE0015, QDExtensions);


void C_GDeviceChanged(GDHandle );PASCAL_SUBTRAP(GDeviceChanged, 0xAB1D, 0x4000A, QDExtensions);


void C_PortChanged(GrafPtr );PASCAL_SUBTRAP(PortChanged, 0xAB1D, 0x40009, QDExtensions);


void C_PixPatChanged(PixPatHandle );PASCAL_SUBTRAP(PixPatChanged, 0xAB1D, 0x40008, QDExtensions);


void C_CTabChanged(CTabHandle );PASCAL_SUBTRAP(CTabChanged, 0xAB1D, 0x40007, QDExtensions);


Boolean C_QDDone(GrafPtr );PASCAL_SUBTRAP(QDDone, 0xAB1D, 0x40013, QDExtensions);


LONGINT C_OffscreenVersion();PASCAL_SUBTRAP(OffscreenVersion, 0xAB1D, 0x0014, QDExtensions);


OSErr C_BitMapToRegion(RgnHandle , const BitMap *);PASCAL_TRAP(BitMapToRegion, 0xA8D7);


LONGINT C_Entry2Index(INTEGER );PASCAL_SUBTRAP(Entry2Index, 0xAAA2, 0x0000, PaletteDispatch);


void C_SaveEntries(CTabHandle , CTabHandle , ReqListRec *);PASCAL_TRAP(SaveEntries, 0xAA49);


void C_RestoreEntries(CTabHandle , CTabHandle , ReqListRec *);PASCAL_TRAP(RestoreEntries, 0xAA4A);


OSErr C_DisposePictInfo(PictInfoID );PASCAL_SUBTRAP(DisposePictInfo, 0xA831, 0x0206, Pack15);


OSErr C_RecordPictInfo(PictInfoID , PicHandle );PASCAL_SUBTRAP(RecordPictInfo, 0xA831, 0x0403, Pack15);


OSErr C_RecordPixMapInfo(PictInfoID , PixMapHandle );PASCAL_SUBTRAP(RecordPixMapInfo, 0xA831, 0x0404, Pack15);


OSErr C_RetrievePictInfo(PictInfoID , PictInfo *, int16_t );PASCAL_SUBTRAP(RetrievePictInfo, 0xA831, 0x0505, Pack15);


OSErr C_NewPictInfo(GUEST<PictInfoID> *, int16_t , int16_t , int16_t , int16_t );PASCAL_SUBTRAP(NewPictInfo, 0xA831, 0x0602, Pack15);


OSErr C_GetPictInfo(PicHandle , PictInfo *, int16_t , int16_t , int16_t , int16_t );PASCAL_SUBTRAP(GetPictInfo, 0xA831, 0x0800, Pack15);


OSErr C_GetPixMapInfo(PixMapHandle , PictInfo *, int16_t , int16_t , int16_t , int16_t );PASCAL_SUBTRAP(GetPixMapInfo, 0xA831, 0x0801, Pack15);


PicHandle C_OpenCPicture(OpenCPicParams *newheaderp);PASCAL_TRAP(OpenCPicture, 0xAA20);


// MacPhoenix: 7.5.5's private screen-bits save (a table of nine
// selectors at the trap's address). The System's menu bar definition
// saves the bits under a menu with it; Apple's Help Manager those
// under a balloon. The three the Help Manager uses are here, named as
// Apple's later headers do (quickdraw/qSaveBits.cpp).
DISPATCHER_TRAP(SaveRestoreBits, 0xA81E, D0W);


// Selector 1: the screen's pixels in bounds (global) into a new handle,
// a 12-byte record of the rectangle and a PixMap handle, as Apple's.
OSErr C_SaveBits(const Rect *bounds, INTEGER purgeable, GUEST<Handle> *bits);PASCAL_SUBTRAP(SaveBits, 0xA81E, 0x0501, SaveRestoreBits);


// Selector 2: the pixels back on the screen (through the Window Manager
// port), then DiscardBits.
OSErr C_RestoreBits(Handle bits);PASCAL_SUBTRAP(RestoreBits, 0xA81E, 0x0202, SaveRestoreBits);


OSErr C_DiscardBits(Handle bits);PASCAL_SUBTRAP(DiscardBits, 0xA81E, 0x0203, SaveRestoreBits);


} /* namespace Executor */