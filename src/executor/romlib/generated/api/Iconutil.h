/*****************************************************************************
 *                                                                           *
 *                                 Iconutil.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     OSErr, ResType, Handle, Rect, Str255, Boolean          *
 *     QuickDraw.h    PixMap, BitMap, RGBColor, RgnHandle                    *
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

#define MODULE_NAME Iconutil
#include <base/api-module.h>


namespace Executor {

enum {large1BitMask = "ICN#"_4,
large4BitData = "icl4"_4,
large8BitData = "icl8"_4,
small1BitMask = "ics#"_4,
small4BitData = "ics4"_4,
small8BitData = "ics8"_4,
mini1BitMask = "icm#"_4,
mini4BitData = "icm4"_4,
mini8BitData = "icm8"_4,
};

// IconAlignmentType values
enum {atNone = 0,
atVerticalCenter = 1,
atTop = 2,
atBottom = 3,
atHorizontalCenter = 4,
atAbsoluteCenter = atVerticalCenter | atHorizontalCenter, // 5
atCenterTop = atTop | atHorizontalCenter, // 6
atCenterBottom = atBottom | atHorizontalCenter, // 7
atLeft = 8,
atCenterLeft = atVerticalCenter | atLeft, // 9
atTopLeft = atTop | atLeft, // 10
atBottomLeft = atBottom | atLeft, // 11
atRight = 12,
atCenterRight = atVerticalCenter | atRight, // 13
atTopRight = atTop | atRight, // 14
atBottomRight = atBottom | atRight, // 15
};

enum {kAlignNode = 0,
kAlignVerticalCenter = 1,
kAlignTop = 2,
kAlignBottom = 3,
kAlignHorizontalCenter = 4,
kAlignAbsoluteCenter = kAlignVerticalCenter | kAlignHorizontalCenter, // 5
};

// IconTranformType values
enum {ttNone = 0,
ttDisabled = 1,
ttOffline = 2,
ttOpen = 3,
ttLabel1 = 256,
ttLabel2 = 512,
ttLabel3 = 768,
ttLabel4 = 1024,
ttLabel5 = 1280,
ttLabel6 = 1536,
ttLabel7 = 1792,
ttSelected = 16384,
ttSelectedDisabled = ttSelected | ttDisabled,
ttSelectedOffline = ttSelected | ttOffline,
ttSelectedOpen = ttSelected | ttOpen,
};

enum {kTransformNone = 0,
};

// IconSelectorValue values
// #### what kind of eediot at apple named all the other icon flag
//    types *Type, except this one?
enum {svLarge1Bit = 1,
svLarge4Bit = 2,
svLarge8Bit = 4,
svSmall1Bit = 256,
svSmall4Bit = 512,
svSmall8Bit = 1024,
svMini1Bit = 65536,
svMini4Bit = 131072,
svMini8Bit = 262144,
svAllLargeData = 255,
svAllSmallData = 65280,
svAllMiniData = 16711680,
svAll1BitData = 65793,
svAll4BitData = 131586,
svAll8BitData = 263172,
svAllAvailableData = 16777215,
};

enum {noMaskFoundErr = -1000,
};

using IconActionUPP = UPP<OSErr(ResType theType, GUEST<GUEST<Handle>> *theIcon, void *yourDataPtr)>;


using IconGetterUPP = UPP<Handle(ResType theType, void *yourDataPtr)>;


typedef uint32_t IconSelectorValue;

typedef int16_t IconAlignmentType;

typedef int16_t IconTransformType;

struct CIcon{GUEST_STRUCT;PixMap iconPMap;BitMap iconMask;BitMap iconBMap;GUEST<Handle> iconData;GUEST<int16_t> iconMaskData[1];};static_assert(sizeof(CIcon) == 84);void describeStruct(const CIcon&, std::ostream&);

typedef CIcon *CIconPtr;

typedef GUEST<CIconPtr> *CIconHandle;

// icon utility function prototypes
DISPATCHER_TRAP(IconDispatch, 0xABC9, D0W);


OSErr C_PlotIconID(const Rect *rect, IconAlignmentType align, IconTransformType tranform, int16_t res_id);PASCAL_SUBTRAP(PlotIconID, 0xABC9, 0x0500, IconDispatch);


OSErr C_PlotIconMethod(const Rect *rect, IconAlignmentType align, IconTransformType transform, IconGetterUPP method, void *data);PASCAL_SUBTRAP(PlotIconMethod, 0xABC9, 0x0805, IconDispatch);


void C_PlotCIcon(const Rect *rect, CIconHandle icon);PASCAL_TRAP(PlotCIcon, 0xAA1F);


void C_PlotIcon(const Rect *rect, Handle icon);PASCAL_TRAP(PlotIcon, 0xA94B);


OSErr C_PlotIconHandle(const Rect *rect, IconAlignmentType align, IconTransformType transform, Handle icon);PASCAL_SUBTRAP(PlotIconHandle, 0xABC9, 0x061D, IconDispatch);


OSErr C_PlotCIconHandle(const Rect *rect, IconAlignmentType align, IconTransformType transform, CIconHandle icon);PASCAL_SUBTRAP(PlotCIconHandle, 0xABC9, 0x061F, IconDispatch);


OSErr C_PlotSICNHandle(const Rect *rect, IconAlignmentType align, IconTransformType transform, Handle icon);PASCAL_SUBTRAP(PlotSICNHandle, 0xABC9, 0x061E, IconDispatch);


Handle C_GetIcon(int16_t icon_id);PASCAL_TRAP(GetIcon, 0xA9BB);


CIconHandle C_GetCIcon(int16_t icon_id);PASCAL_TRAP(GetCIcon, 0xAA1E);


void C_DisposeCIcon(CIconHandle icon);PASCAL_TRAP(DisposeCIcon, 0xAA25);


OSErr C_GetIconSuite(GUEST<Handle> *suite, int16_t res_id, IconSelectorValue selector);PASCAL_SUBTRAP(GetIconSuite, 0xABC9, 0x0501, IconDispatch);


OSErr C_NewIconSuite(GUEST<Handle> *suite);PASCAL_SUBTRAP(NewIconSuite, 0xABC9, 0x0207, IconDispatch);


OSErr C_AddIconToSuite(Handle icon_data, Handle suite, ResType type);PASCAL_SUBTRAP(AddIconToSuite, 0xABC9, 0x0608, IconDispatch);


OSErr C_GetIconFromSuite(GUEST<Handle> *icon_data, Handle suite, ResType type);PASCAL_SUBTRAP(GetIconFromSuite, 0xABC9, 0x0609, IconDispatch);


OSErr C_PlotIconSuite(const Rect *rect, IconAlignmentType align, IconTransformType transform, Handle suite);PASCAL_SUBTRAP(PlotIconSuite, 0xABC9, 0x0603, IconDispatch);


OSErr C_ForEachIconDo(Handle suite, IconSelectorValue selector, IconActionUPP action, void *data);PASCAL_SUBTRAP(ForEachIconDo, 0xABC9, 0x080A, IconDispatch);


int16_t C_GetSuiteLabel(Handle suite);PASCAL_SUBTRAP(GetSuiteLabel, 0xABC9, 0x0217, IconDispatch);


void C_SetIconDrawContext(int32_t context);PASCAL_SUBTRAP(SetIconDrawContext, 0xABC9, 0x0218, IconDispatch);


void C_GetIconDrawContext(GUEST<GUEST<int32_t>> *context);PASCAL_SUBTRAP(GetIconDrawContext, 0xABC9, 0x0220, IconDispatch);


OSErr C_SetSuiteLabel(Handle suite, int16_t label);PASCAL_SUBTRAP(SetSuiteLabel, 0xABC9, 0x0316, IconDispatch);


OSErr C_GetLabel(int16_t label, RGBColor *label_color, Str255 label_string);PASCAL_SUBTRAP(GetLabel, 0xABC9, 0x050B, IconDispatch);


OSErr C_DisposeIconSuite(Handle suite, Boolean dispose_data_p);PASCAL_SUBTRAP(DisposeIconSuite, 0xABC9, 0x0302, IconDispatch);


OSErr C_IconSuiteToRgn(RgnHandle rgn, const Rect *rect, IconAlignmentType align, Handle suite);PASCAL_SUBTRAP(IconSuiteToRgn, 0xABC9, 0x0714, IconDispatch);


OSErr C_IconIDToRgn(RgnHandle rgn, const Rect *rect, IconAlignmentType align, int16_t icon_id);PASCAL_SUBTRAP(IconIDToRgn, 0xABC9, 0x0613, IconDispatch);


OSErr C_IconMethodToRgn(RgnHandle rgn, const Rect *rect, IconAlignmentType align, IconGetterUPP method, void *data);PASCAL_SUBTRAP(IconMethodToRgn, 0xABC9, 0x0915, IconDispatch);


Boolean C_PtInIconSuite(Point test_pt, const Rect *rect, IconAlignmentType align, Handle suite);PASCAL_SUBTRAP(PtInIconSuite, 0xABC9, 0x070E, IconDispatch);


Boolean C_PtInIconID(Point test_pt, const Rect *rect, IconAlignmentType align, int16_t icon_id);PASCAL_SUBTRAP(PtInIconID, 0xABC9, 0x060D, IconDispatch);


Boolean C_PtInIconMethod(Point test_pt, const Rect *rect, IconAlignmentType align, IconGetterUPP method, void *data);PASCAL_SUBTRAP(PtInIconMethod, 0xABC9, 0x090F, IconDispatch);


Boolean C_RectInIconSuite(const Rect *test_rect, const Rect *rect, IconAlignmentType align, Handle suite);PASCAL_SUBTRAP(RectInIconSuite, 0xABC9, 0x0711, IconDispatch);


Boolean C_RectInIconID(const Rect *test_rect, const Rect *rect, IconAlignmentType align, int16_t icon_id);PASCAL_SUBTRAP(RectInIconID, 0xABC9, 0x0610, IconDispatch);


Boolean C_RectInIconMethod(const Rect *test_rect, const Rect *rect, IconAlignmentType align, IconGetterUPP method, void *data);PASCAL_SUBTRAP(RectInIconMethod, 0xABC9, 0x0912, IconDispatch);


OSErr C_MakeIconCache(GUEST<Handle> *cache, IconGetterUPP make_icon, void *data);PASCAL_SUBTRAP(MakeIconCache, 0xABC9, 0x0604, IconDispatch);


OSErr C_LoadIconCache(const Rect *rect, IconAlignmentType align, IconTransformType transform, Handle cache);PASCAL_SUBTRAP(LoadIconCache, 0xABC9, 0x0606, IconDispatch);


OSErr C_GetIconCacheData(Handle cache, GUEST<void*> *data);PASCAL_SUBTRAP(GetIconCacheData, 0xABC9, 0x0419, IconDispatch);


OSErr C_SetIconCacheData(Handle cache, void *data);PASCAL_SUBTRAP(SetIconCacheData, 0xABC9, 0x041A, IconDispatch);


OSErr C_GetIconCacheProc(Handle cache, GUEST<IconGetterUPP> *proc);PASCAL_SUBTRAP(GetIconCacheProc, 0xABC9, 0x041B, IconDispatch);


OSErr C_SetIconCacheProc(Handle cache, IconGetterUPP proc);PASCAL_SUBTRAP(SetIconCacheProc, 0xABC9, 0x041C, IconDispatch);


} /* namespace Executor */