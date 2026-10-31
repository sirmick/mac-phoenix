/*****************************************************************************
 *                                                                           *
 *                                Appearance.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          SInt16, UInt32, UInt16, UInt8, SInt32, Rect,      *
 *                         OSType, Boolean, ConstStr255Param, ScriptCode,    *
 *                         Str255, StringPtr, ConstStringPtr, Ptr, Handle    *
 *     Collections.h       Collection                                        *
 *     DriverServices.h    OSStatus                                          *
 *     ProcessMgr.h        ProcessSerialNumber                               *
 *     WindowMgr.h         WindowRef, WindowRegionCode                       *
 *     QuickDraw.h         RgnHandle, CTabHandle, Style, RGBColor            *
 *     ControlMgr.h        ControlPartCode                                   *
 *     FileMgr.h           ItemCount                                         *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "Collections.h"
#include "DriverServices.h"
#include "ProcessMgr.h"
#include "WindowMgr.h"
#include "QuickDraw.h"
#include "ControlMgr.h"
#include "FileMgr.h"

#define MODULE_NAME Appearance
#include <base/api-module.h>


namespace Executor {

// The selector in D0 (THREEWORDINLINE(0x303C, sel, 0xAA74): move.w
// #sel, D0), Pascal arguments on the stack.
DISPATCHER_TRAP(AppearanceDispatch, 0xAA74, D0W);


enum {themeInvalidBrushErr = -30560,
themeProcessRegisteredErr = -30561,
themeProcessNotRegisteredErr = -30562,
themeBadTextColorErr = -30563,
themeHasNoAccentsErr = -30564,
themeBadCursorIndexErr = -30565,
themeScriptFontNotFoundErr = -30566,
themeMonitorDepthNotSupportedErr = -30567,
};

enum {kAppearanceEventClass = "appr"_4,
kAEAppearanceChanged = "thme"_4,
kAESystemFontChanged = "sysf"_4,
kAESmallSystemFontChanged = "ssfn"_4,
kAEViewsFontChanged = "vfnt"_4,
};

enum {kThemeDataFileType = "thme"_4,
kThemePlatinumFileType = "pltn"_4,
kThemeCustomThemesFileType = "scen"_4,
kThemeSoundTrackFileType = "tsnd"_4,
};

enum {kThemeBrushDialogBackgroundActive = 1,
kThemeBrushDialogBackgroundInactive = 2,
kThemeBrushAlertBackgroundActive = 3,
kThemeBrushAlertBackgroundInactive = 4,
kThemeBrushModelessDialogBackgroundActive = 5,
kThemeBrushModelessDialogBackgroundInactive = 6,
kThemeBrushUtilityWindowBackgroundActive = 7,
kThemeBrushUtilityWindowBackgroundInactive = 8,
kThemeBrushListViewSortColumnBackground = 9,
kThemeBrushListViewBackground = 10,
kThemeBrushIconLabelBackground = 11,
kThemeBrushListViewSeparator = 12,
kThemeBrushChasingArrows = 13,
kThemeBrushDragHilite = 14,
kThemeBrushDocumentWindowBackground = 15,
kThemeBrushFinderWindowBackground = 16,
};

enum {kThemeBrushScrollBarDelimiterActive = 17,
kThemeBrushScrollBarDelimiterInactive = 18,
kThemeBrushFocusHighlight = 19,
kThemeBrushPopupArrowActive = 20,
kThemeBrushPopupArrowPressed = 21,
kThemeBrushPopupArrowInactive = 22,
kThemeBrushAppleGuideCoachmark = 23,
kThemeBrushIconLabelBackgroundSelected = 24,
kThemeBrushStaticAreaFill = 25,
kThemeBrushActiveAreaFill = 26,
kThemeBrushButtonFrameActive = 27,
kThemeBrushButtonFrameInactive = 28,
kThemeBrushButtonFaceActive = 29,
kThemeBrushButtonFaceInactive = 30,
kThemeBrushButtonFacePressed = 31,
kThemeBrushButtonActiveDarkShadow = 32,
kThemeBrushButtonActiveDarkHighlight = 33,
kThemeBrushButtonActiveLightShadow = 34,
kThemeBrushButtonActiveLightHighlight = 35,
kThemeBrushButtonInactiveDarkShadow = 36,
kThemeBrushButtonInactiveDarkHighlight = 37,
kThemeBrushButtonInactiveLightShadow = 38,
kThemeBrushButtonInactiveLightHighlight = 39,
kThemeBrushButtonPressedDarkShadow = 40,
kThemeBrushButtonPressedDarkHighlight = 41,
kThemeBrushButtonPressedLightShadow = 42,
kThemeBrushButtonPressedLightHighlight = 43,
kThemeBrushBevelActiveLight = 44,
kThemeBrushBevelActiveDark = 45,
kThemeBrushBevelInactiveLight = 46,
kThemeBrushBevelInactiveDark = 47,
};

enum {kThemeBrushNotificationWindowBackground = 48,
};

enum {kThemeBrushMovableModalBackground = 49,
kThemeBrushSheetBackground = 50,
kThemeBrushDrawerBackground = 51,
};

enum {kThemeBrushBlack = -1,
kThemeBrushWhite = -2,
};

typedef SInt16 ThemeBrush;

enum {kThemeTextColorDialogActive = 1,
kThemeTextColorDialogInactive = 2,
kThemeTextColorAlertActive = 3,
kThemeTextColorAlertInactive = 4,
kThemeTextColorModelessDialogActive = 5,
kThemeTextColorModelessDialogInactive = 6,
kThemeTextColorWindowHeaderActive = 7,
kThemeTextColorWindowHeaderInactive = 8,
kThemeTextColorPlacardActive = 9,
kThemeTextColorPlacardInactive = 10,
kThemeTextColorPlacardPressed = 11,
kThemeTextColorPushButtonActive = 12,
kThemeTextColorPushButtonInactive = 13,
kThemeTextColorPushButtonPressed = 14,
kThemeTextColorBevelButtonActive = 15,
kThemeTextColorBevelButtonInactive = 16,
kThemeTextColorBevelButtonPressed = 17,
kThemeTextColorPopupButtonActive = 18,
kThemeTextColorPopupButtonInactive = 19,
kThemeTextColorPopupButtonPressed = 20,
kThemeTextColorIconLabel = 21,
kThemeTextColorListView = 22,
};

enum {kThemeTextColorDocumentWindowTitleActive = 23,
kThemeTextColorDocumentWindowTitleInactive = 24,
kThemeTextColorMovableModalWindowTitleActive = 25,
kThemeTextColorMovableModalWindowTitleInactive = 26,
kThemeTextColorUtilityWindowTitleActive = 27,
kThemeTextColorUtilityWindowTitleInactive = 28,
kThemeTextColorPopupWindowTitleActive = 29,
kThemeTextColorPopupWindowTitleInactive = 30,
kThemeTextColorRootMenuActive = 31,
kThemeTextColorRootMenuSelected = 32,
kThemeTextColorRootMenuDisabled = 33,
kThemeTextColorMenuItemActive = 34,
kThemeTextColorMenuItemSelected = 35,
kThemeTextColorMenuItemDisabled = 36,
kThemeTextColorPopupLabelActive = 37,
kThemeTextColorPopupLabelInactive = 38,
};

enum {kThemeTextColorTabFrontActive = 39,
kThemeTextColorTabNonFrontActive = 40,
kThemeTextColorTabNonFrontPressed = 41,
kThemeTextColorTabFrontInactive = 42,
kThemeTextColorTabNonFrontInactive = 43,
kThemeTextColorIconLabelSelected = 44,
kThemeTextColorBevelButtonStickyActive = 45,
kThemeTextColorBevelButtonStickyInactive = 46,
};

enum {kThemeTextColorNotification = 47,
};

enum {kThemeTextColorBlack = -1,
kThemeTextColorWhite = -2,
};

typedef SInt16 ThemeTextColor;

enum {kThemeStateInactive = 0,
kThemeStateActive = 1,
kThemeStatePressed = 2,
kThemeStateRollover = 6,
kThemeStateUnavailable = 7,
kThemeStateUnavailableInactive = 8,
};

enum {kThemeStateDisabled = 0,
};

enum {kThemeStatePressedUp = 2,
kThemeStatePressedDown = 3,
};

typedef UInt32 ThemeDrawState;

enum {kThemeArrowCursor = 0,
kThemeCopyArrowCursor = 1,
kThemeAliasArrowCursor = 2,
kThemeContextualMenuArrowCursor = 3,
kThemeIBeamCursor = 4,
kThemeCrossCursor = 5,
kThemePlusCursor = 6,
kThemeWatchCursor = 7,
kThemeClosedHandCursor = 8,
kThemeOpenHandCursor = 9,
kThemePointingHandCursor = 10,
kThemeCountingUpHandCursor = 11,
kThemeCountingDownHandCursor = 12,
kThemeCountingUpAndDownHandCursor = 13,
kThemeSpinningCursor = 14,
kThemeResizeLeftCursor = 15,
kThemeResizeRightCursor = 16,
kThemeResizeLeftRightCursor = 17,
};

typedef UInt32 ThemeCursor;

enum {kThemeMenuBarNormal = 0,
kThemeMenuBarSelected = 1,
};

typedef UInt16 ThemeMenuBarState;

enum {kThemeMenuSquareMenuBar = (1 << 0),
};

enum {kThemeMenuActive = 0,
kThemeMenuSelected = 1,
kThemeMenuDisabled = 3,
};

typedef UInt16 ThemeMenuState;

enum {kThemeMenuTypePullDown = 0,
kThemeMenuTypePopUp = 1,
kThemeMenuTypeHierarchical = 2,
kThemeMenuTypeInactive = 256,
};

typedef UInt16 ThemeMenuType;

enum {kThemeMenuItemPlain = 0,
kThemeMenuItemHierarchical = 1,
kThemeMenuItemScrollUpArrow = 2,
kThemeMenuItemScrollDownArrow = 3,
kThemeMenuItemAtTop = 256,
kThemeMenuItemAtBottom = 512,
kThemeMenuItemHierBackground = 1024,
kThemeMenuItemPopUpBackground = 2048,
kThemeMenuItemHasIcon = 32768,
};

typedef UInt16 ThemeMenuItemType;

enum {kThemeBackgroundTabPane = 1,
kThemeBackgroundPlacard = 2,
kThemeBackgroundWindowHeader = 3,
kThemeBackgroundListViewWindowHeader = 4,
kThemeBackgroundSecondaryGroupBox = 5,
};

typedef UInt32 ThemeBackgroundKind;

enum {kThemeNameTag = "name"_4,
kThemeVariantNameTag = "varn"_4,
kThemeHighlightColorTag = "hcol"_4,
kThemeScrollBarArrowStyleTag = "sbar"_4,
kThemeScrollBarThumbStyleTag = "sbth"_4,
kThemeSoundsEnabledTag = "snds"_4,
kThemeDblClickCollapseTag = "coll"_4,
};

enum {kThemeAppearanceFileNameTag = "thme"_4,
kThemeSystemFontTag = "lgsf"_4,
kThemeSmallSystemFontTag = "smsf"_4,
kThemeViewsFontTag = "vfnt"_4,
kThemeViewsFontSizeTag = "vfsz"_4,
kThemeDesktopPatternNameTag = "patn"_4,
kThemeDesktopPatternTag = "patt"_4,
kThemeDesktopPictureNameTag = "dpnm"_4,
kThemeDesktopPictureAliasTag = "dpal"_4,
kThemeDesktopPictureAlignmentTag = "dpan"_4,
kThemeHighlightColorNameTag = "hcnm"_4,
kThemeExamplePictureIDTag = "epic"_4,
kThemeSoundTrackNameTag = "sndt"_4,
kThemeSoundMaskTag = "smsk"_4,
kThemeUserDefinedTag = "user"_4,
kThemeSmoothFontEnabledTag = "smoo"_4,
kThemeSmoothFontMinSizeTag = "smos"_4,
};

enum {kThemeCheckBoxClassicX = 0,
kThemeCheckBoxCheckMark = 1,
};

typedef UInt16 ThemeCheckBoxStyle;

enum {kThemeScrollBarArrowsSingle = 0,
kThemeScrollBarArrowsLowerRight = 1,
};

typedef UInt16 ThemeScrollBarArrowStyle;

enum {kThemeScrollBarThumbNormal = 0,
kThemeScrollBarThumbProportional = 1,
};

typedef UInt16 ThemeScrollBarThumbStyle;

enum {kThemeSystemFont = 0,
kThemeSmallSystemFont = 1,
kThemeSmallEmphasizedSystemFont = 2,
kThemeViewsFont = 3,
kThemeEmphasizedSystemFont = 4,
kThemeApplicationFont = 5,
kThemeLabelFont = 6,
kThemeMenuTitleFont = 100,
kThemeMenuItemFont = 101,
kThemeMenuItemMarkFont = 102,
kThemeMenuItemCmdKeyFont = 103,
kThemeWindowTitleFont = 104,
kThemePushButtonFont = 105,
kThemeUtilityWindowTitleFont = 106,
kThemeAlertHeaderFont = 107,
kThemeCurrentPortFont = 200,
};

typedef UInt16 ThemeFontID;

enum {kThemeTabNonFront = 0,
kThemeTabNonFrontPressed = 1,
kThemeTabNonFrontInactive = 2,
kThemeTabFront = 3,
kThemeTabFrontInactive = 4,
kThemeTabNonFrontUnavailable = 5,
kThemeTabFrontUnavailable = 6,
};

typedef UInt16 ThemeTabStyle;

enum {kThemeTabNorth = 0,
kThemeTabSouth = 1,
kThemeTabEast = 2,
kThemeTabWest = 3,
};

typedef UInt16 ThemeTabDirection;

enum {kThemeSmallTabHeight = 16,
kThemeLargeTabHeight = 21,
kThemeTabPaneOverlap = 3,
kThemeSmallTabHeightMax = 19,
kThemeLargeTabHeightMax = 24,
};

enum {kThemeMediumScrollBar = 0,
kThemeSmallScrollBar = 1,
kThemeMediumSlider = 2,
kThemeMediumProgressBar = 3,
kThemeMediumIndeterminateBar = 4,
kThemeRelevanceBar = 5,
kThemeSmallSlider = 6,
kThemeLargeProgressBar = 7,
kThemeLargeIndeterminateBar = 8,
};

typedef UInt16 ThemeTrackKind;

enum {kThemeTrackActive = 0,
kThemeTrackDisabled = 1,
kThemeTrackNothingToScroll = 2,
kThemeTrackInactive = 3,
};

typedef UInt8 ThemeTrackEnableState;

enum {kThemeLeftOutsideArrowPressed = 1,
kThemeLeftInsideArrowPressed = 2,
kThemeLeftTrackPressed = 4,
kThemeThumbPressed = 8,
kThemeRightTrackPressed = 16,
kThemeRightInsideArrowPressed = 32,
kThemeRightOutsideArrowPressed = 64,
kThemeTopOutsideArrowPressed = kThemeLeftOutsideArrowPressed,
kThemeTopInsideArrowPressed = kThemeLeftInsideArrowPressed,
kThemeTopTrackPressed = kThemeLeftTrackPressed,
kThemeBottomTrackPressed = kThemeRightTrackPressed,
kThemeBottomInsideArrowPressed = kThemeRightInsideArrowPressed,
kThemeBottomOutsideArrowPressed = kThemeRightOutsideArrowPressed,
};

typedef UInt8 ThemeTrackPressState;

enum {kThemeThumbPlain = 0,
kThemeThumbUpward = 1,
kThemeThumbDownward = 2,
};

typedef UInt8 ThemeThumbDirection;

enum {kThemeTrackHorizontal = (1 << 0),
kThemeTrackRightToLeft = (1 << 1),
kThemeTrackShowThumb = (1 << 2),
kThemeTrackThumbRgnIsNotGhost = (1 << 3),
kThemeTrackNoScrollBarArrows = (1 << 4),
};

typedef UInt16 ThemeTrackAttributes;

struct ScrollBarTrackInfo{GUEST_STRUCT;GUEST<SInt32> viewsize;ThemeTrackPressState pressState;};void describeStruct(const ScrollBarTrackInfo&, std::ostream&);

struct SliderTrackInfo{GUEST_STRUCT;ThemeThumbDirection thumbDir;ThemeTrackPressState pressState;};void describeStruct(const SliderTrackInfo&, std::ostream&);

struct ProgressTrackInfo{GUEST_STRUCT;UInt8 phase;};void describeStruct(const ProgressTrackInfo&, std::ostream&);

union ThemeTrackInfo{GUEST_STRUCT;ScrollBarTrackInfo scrollbar;SliderTrackInfo slider;ProgressTrackInfo progress;};void describeStruct(const ThemeTrackInfo&, std::ostream&);

struct ThemeTrackDrawInfo{GUEST_STRUCT;GUEST<ThemeTrackKind> kind;Rect bounds;GUEST<SInt32> min;GUEST<SInt32> max;GUEST<SInt32> value;GUEST<UInt32> reserved;GUEST<ThemeTrackAttributes> attributes;ThemeTrackEnableState enableState;UInt8 filler1;ThemeTrackInfo trackInfo;};static_assert(sizeof(ThemeTrackDrawInfo) == 36);void describeStruct(const ThemeTrackDrawInfo&, std::ostream&);

enum {kThemeWindowHasGrow = (1 << 0),
kThemeWindowHasHorizontalZoom = (1 << 3),
kThemeWindowHasVerticalZoom = (1 << 4),
kThemeWindowHasFullZoom = kThemeWindowHasHorizontalZoom + kThemeWindowHasVerticalZoom,
kThemeWindowHasCloseBox = (1 << 5),
kThemeWindowHasCollapseBox = (1 << 6),
kThemeWindowHasTitleText = (1 << 7),
kThemeWindowIsCollapsed = (1 << 8),
kThemeWindowHasDirty = (1 << 9),
};

typedef UInt32 ThemeWindowAttributes;

enum {kThemeDocumentWindow = 0,
kThemeDialogWindow = 1,
kThemeMovableDialogWindow = 2,
kThemeAlertWindow = 3,
kThemeMovableAlertWindow = 4,
kThemePlainDialogWindow = 5,
kThemeShadowDialogWindow = 6,
kThemePopupWindow = 7,
kThemeUtilityWindow = 8,
kThemeUtilitySideWindow = 9,
kThemeSheetWindow = 10,
};

typedef UInt16 ThemeWindowType;

enum {kThemeWidgetCloseBox = 0,
kThemeWidgetZoomBox = 1,
kThemeWidgetCollapseBox = 2,
kThemeWidgetDirtyCloseBox = 6,
};

typedef UInt16 ThemeTitleBarWidget;

enum {kThemeArrowLeft = 0,
kThemeArrowDown = 1,
kThemeArrowRight = 2,
kThemeArrowUp = 3,
};

typedef UInt16 ThemeArrowOrientation;

enum {kThemeArrow3pt = 0,
kThemeArrow5pt = 1,
kThemeArrow7pt = 2,
kThemeArrow9pt = 3,
};

typedef UInt16 ThemePopupArrowSize;

enum {kThemeGrowLeft = (1 << 0),
kThemeGrowRight = (1 << 1),
kThemeGrowUp = (1 << 2),
kThemeGrowDown = (1 << 3),
};

typedef UInt16 ThemeGrowDirection;

enum {kThemePushButton = 0,
kThemeCheckBox = 1,
kThemeRadioButton = 2,
kThemeBevelButton = 3,
kThemeArrowButton = 4,
kThemePopupButton = 5,
kThemeDisclosureButton = 6,
kThemeIncDecButton = 7,
kThemeSmallBevelButton = 8,
kThemeMediumBevelButton = 3,
kThemeLargeBevelButton = 9,
kThemeListHeaderButton = 10,
kThemeRoundButton = 11,
kThemeLargeRoundButton = 12,
kThemeSmallCheckBox = 13,
kThemeSmallRadioButton = 14,
kThemeRoundedBevelButton = 15,
kThemeNormalCheckBox = kThemeCheckBox,
kThemeNormalRadioButton = kThemeRadioButton,
};

typedef UInt16 ThemeButtonKind;

enum {kThemeButtonOff = 0,
kThemeButtonOn = 1,
kThemeButtonMixed = 2,
kThemeDisclosureRight = 0,
kThemeDisclosureDown = 1,
kThemeDisclosureLeft = 2,
};

typedef UInt16 ThemeButtonValue;

enum {kThemeAdornmentNone = 0,
kThemeAdornmentDefault = (1 << 0),
kThemeAdornmentFocus = (1 << 2),
kThemeAdornmentRightToLeft = (1 << 4),
kThemeAdornmentDrawIndicatorOnly = (1 << 5),
kThemeAdornmentHeaderButtonLeftNeighborSelected = (1 << 6),
kThemeAdornmentHeaderButtonRightNeighborSelected = (1 << 7),
kThemeAdornmentHeaderButtonSortUp = (1 << 8),
kThemeAdornmentHeaderMenuButton = (1 << 9),
kThemeAdornmentHeaderButtonNoShadow = (1 << 10),
kThemeAdornmentHeaderButtonShadowOnly = (1 << 11),
kThemeAdornmentNoShadow = kThemeAdornmentHeaderButtonNoShadow,
kThemeAdornmentShadowOnly = kThemeAdornmentHeaderButtonShadowOnly,
kThemeAdornmentArrowLeftArrow = (1 << 6),
kThemeAdornmentArrowDownArrow = (1 << 7),
kThemeAdornmentArrowDoubleArrow = (1 << 8),
kThemeAdornmentArrowUpArrow = (1 << 9),
};

typedef UInt16 ThemeButtonAdornment;

struct ThemeButtonDrawInfo{GUEST_STRUCT;GUEST<ThemeDrawState> state;GUEST<ThemeButtonValue> value;GUEST<ThemeButtonAdornment> adornment;};void describeStruct(const ThemeButtonDrawInfo&, std::ostream&);

typedef ThemeButtonDrawInfo *ThemeButtonDrawInfoPtr;

enum {kThemeNoSounds = 0,
kThemeWindowSoundsMask = (1 << 0),
kThemeMenuSoundsMask = (1 << 1),
kThemeControlSoundsMask = (1 << 2),
kThemeFinderSoundsMask = (1 << 3),
};

enum {kThemeDragSoundNone = 0,
kThemeDragSoundMoveWindow = "wmov"_4,
kThemeDragSoundGrowWindow = "wgro"_4,
kThemeDragSoundMoveUtilWindow = "umov"_4,
kThemeDragSoundGrowUtilWindow = "ugro"_4,
kThemeDragSoundMoveDialog = "dmov"_4,
kThemeDragSoundMoveAlert = "amov"_4,
kThemeDragSoundMoveIcon = "imov"_4,
kThemeDragSoundSliderThumb = "slth"_4,
kThemeDragSoundSliderGhost = "slgh"_4,
kThemeDragSoundScrollBarThumb = "sbth"_4,
kThemeDragSoundScrollBarGhost = "sbgh"_4,
kThemeDragSoundScrollBarArrowDecreasing = "sbad"_4,
kThemeDragSoundScrollBarArrowIncreasing = "sbai"_4,
kThemeDragSoundDragging = "drag"_4,
};

typedef OSType ThemeDragSoundKind;

enum {kThemeSoundNone = 0,
kThemeSoundMenuOpen = "mnuo"_4,
kThemeSoundMenuClose = "mnuc"_4,
kThemeSoundMenuItemHilite = "mnui"_4,
kThemeSoundMenuItemRelease = "mnus"_4,
kThemeSoundWindowClosePress = "wclp"_4,
kThemeSoundWindowCloseEnter = "wcle"_4,
kThemeSoundWindowCloseExit = "wclx"_4,
kThemeSoundWindowCloseRelease = "wclr"_4,
kThemeSoundWindowZoomPress = "wzmp"_4,
kThemeSoundWindowZoomEnter = "wzme"_4,
kThemeSoundWindowZoomExit = "wzmx"_4,
kThemeSoundWindowZoomRelease = "wzmr"_4,
kThemeSoundWindowCollapsePress = "wcop"_4,
kThemeSoundWindowCollapseEnter = "wcoe"_4,
kThemeSoundWindowCollapseExit = "wcox"_4,
kThemeSoundWindowCollapseRelease = "wcor"_4,
kThemeSoundWindowDragBoundary = "wdbd"_4,
kThemeSoundUtilWinClosePress = "uclp"_4,
kThemeSoundUtilWinCloseEnter = "ucle"_4,
kThemeSoundUtilWinCloseExit = "uclx"_4,
kThemeSoundUtilWinCloseRelease = "uclr"_4,
kThemeSoundUtilWinZoomPress = "uzmp"_4,
kThemeSoundUtilWinZoomEnter = "uzme"_4,
kThemeSoundUtilWinZoomExit = "uzmx"_4,
kThemeSoundUtilWinZoomRelease = "uzmr"_4,
kThemeSoundUtilWinCollapsePress = "ucop"_4,
kThemeSoundUtilWinCollapseEnter = "ucoe"_4,
kThemeSoundUtilWinCollapseExit = "ucox"_4,
kThemeSoundUtilWinCollapseRelease = "ucor"_4,
kThemeSoundUtilWinDragBoundary = "udbd"_4,
kThemeSoundWindowOpen = "wopn"_4,
kThemeSoundWindowClose = "wcls"_4,
kThemeSoundWindowZoomIn = "wzmi"_4,
kThemeSoundWindowZoomOut = "wzmo"_4,
kThemeSoundWindowCollapseUp = "wcol"_4,
kThemeSoundWindowCollapseDown = "wexp"_4,
kThemeSoundWindowActivate = "wact"_4,
kThemeSoundUtilWindowOpen = "uopn"_4,
kThemeSoundUtilWindowClose = "ucls"_4,
kThemeSoundUtilWindowZoomIn = "uzmi"_4,
kThemeSoundUtilWindowZoomOut = "uzmo"_4,
kThemeSoundUtilWindowCollapseUp = "ucol"_4,
kThemeSoundUtilWindowCollapseDown = "uexp"_4,
kThemeSoundUtilWindowActivate = "uact"_4,
kThemeSoundDialogOpen = "dopn"_4,
kThemeSoundDialogClose = "dlgc"_4,
kThemeSoundAlertOpen = "aopn"_4,
kThemeSoundAlertClose = "altc"_4,
kThemeSoundPopupWindowOpen = "pwop"_4,
kThemeSoundPopupWindowClose = "pwcl"_4,
kThemeSoundButtonPress = "btnp"_4,
kThemeSoundButtonEnter = "btne"_4,
kThemeSoundButtonExit = "btnx"_4,
kThemeSoundButtonRelease = "btnr"_4,
kThemeSoundDefaultButtonPress = "dbtp"_4,
kThemeSoundDefaultButtonEnter = "dbte"_4,
kThemeSoundDefaultButtonExit = "dbtx"_4,
kThemeSoundDefaultButtonRelease = "dbtr"_4,
kThemeSoundCancelButtonPress = "cbtp"_4,
kThemeSoundCancelButtonEnter = "cbte"_4,
kThemeSoundCancelButtonExit = "cbtx"_4,
kThemeSoundCancelButtonRelease = "cbtr"_4,
kThemeSoundCheckboxPress = "chkp"_4,
kThemeSoundCheckboxEnter = "chke"_4,
kThemeSoundCheckboxExit = "chkx"_4,
kThemeSoundCheckboxRelease = "chkr"_4,
kThemeSoundRadioPress = "radp"_4,
kThemeSoundRadioEnter = "rade"_4,
kThemeSoundRadioExit = "radx"_4,
kThemeSoundRadioRelease = "radr"_4,
kThemeSoundScrollArrowPress = "sbap"_4,
kThemeSoundScrollArrowEnter = "sbae"_4,
kThemeSoundScrollArrowExit = "sbax"_4,
kThemeSoundScrollArrowRelease = "sbar"_4,
kThemeSoundScrollEndOfTrack = "sbte"_4,
kThemeSoundScrollTrackPress = "sbtp"_4,
kThemeSoundSliderEndOfTrack = "slte"_4,
kThemeSoundSliderTrackPress = "sltp"_4,
kThemeSoundBalloonOpen = "blno"_4,
kThemeSoundBalloonClose = "blnc"_4,
kThemeSoundBevelPress = "bevp"_4,
kThemeSoundBevelEnter = "beve"_4,
kThemeSoundBevelExit = "bevx"_4,
kThemeSoundBevelRelease = "bevr"_4,
kThemeSoundLittleArrowUpPress = "laup"_4,
kThemeSoundLittleArrowDnPress = "ladp"_4,
kThemeSoundLittleArrowEnter = "lare"_4,
kThemeSoundLittleArrowExit = "larx"_4,
kThemeSoundLittleArrowUpRelease = "laur"_4,
kThemeSoundLittleArrowDnRelease = "ladr"_4,
kThemeSoundPopupPress = "popp"_4,
kThemeSoundPopupEnter = "pope"_4,
kThemeSoundPopupExit = "popx"_4,
kThemeSoundPopupRelease = "popr"_4,
kThemeSoundDisclosurePress = "dscp"_4,
kThemeSoundDisclosureEnter = "dsce"_4,
kThemeSoundDisclosureExit = "dscx"_4,
kThemeSoundDisclosureRelease = "dscr"_4,
kThemeSoundTabPressed = "tabp"_4,
kThemeSoundTabEnter = "tabe"_4,
kThemeSoundTabExit = "tabx"_4,
kThemeSoundTabRelease = "tabr"_4,
kThemeSoundDragTargetHilite = "dthi"_4,
kThemeSoundDragTargetUnhilite = "dtuh"_4,
kThemeSoundDragTargetDrop = "dtdr"_4,
kThemeSoundEmptyTrash = "ftrs"_4,
kThemeSoundSelectItem = "fsel"_4,
kThemeSoundNewItem = "fnew"_4,
kThemeSoundReceiveDrop = "fdrp"_4,
kThemeSoundCopyDone = "fcpd"_4,
kThemeSoundResolveAlias = "fral"_4,
kThemeSoundLaunchApp = "flap"_4,
kThemeSoundDiskInsert = "dski"_4,
kThemeSoundDiskEject = "dske"_4,
kThemeSoundFinderDragOnIcon = "fdon"_4,
kThemeSoundFinderDragOffIcon = "fdof"_4,
};

typedef OSType ThemeSoundKind;

enum {kThemePopupTabNormalPosition = 0,
kThemePopupTabCenterOnWindow = 1,
kThemePopupTabCenterOnOffset = 2,
};

struct ThemeWindowMetrics{GUEST_STRUCT;GUEST<UInt16> metricSize;GUEST<SInt16> titleHeight;GUEST<SInt16> titleWidth;GUEST<SInt16> popupTabOffset;GUEST<SInt16> popupTabWidth;GUEST<UInt16> popupTabPosition;};void describeStruct(const ThemeWindowMetrics&, std::ostream&);

typedef ThemeWindowMetrics *ThemeWindowMetricsPtr;

enum {kThemeMetricScrollBarWidth = 0,
kThemeMetricSmallScrollBarWidth = 1,
kThemeMetricCheckBoxHeight = 2,
kThemeMetricRadioButtonHeight = 3,
kThemeMetricEditTextWhitespace = 4,
kThemeMetricEditTextFrameOutset = 5,
kThemeMetricListBoxFrameOutset = 6,
kThemeMetricFocusRectOutset = 7,
kThemeMetricImageWellThickness = 8,
kThemeMetricScrollBarOverlap = 9,
kThemeMetricLargeTabHeight = 10,
kThemeMetricLargeTabCapsWidth = 11,
kThemeMetricTabFrameOverlap = 12,
kThemeMetricTabIndentOrStyle = 13,
kThemeMetricTabOverlap = 14,
kThemeMetricSmallTabHeight = 15,
kThemeMetricSmallTabCapsWidth = 16,
kThemeMetricDisclosureButtonHeight = 17,
kThemeMetricRoundButtonSize = 18,
kThemeMetricPushButtonHeight = 19,
kThemeMetricListHeaderHeight = 20,
kThemeMetricSmallCheckBoxHeight = 21,
kThemeMetricDisclosureButtonWidth = 22,
kThemeMetricSmallDisclosureButtonHeight = 23,
kThemeMetricSmallDisclosureButtonWidth = 24,
kThemeMetricDisclosureTriangleHeight = 25,
kThemeMetricDisclosureTriangleWidth = 26,
kThemeMetricLittleArrowsHeight = 27,
kThemeMetricLittleArrowsWidth = 28,
kThemeMetricPaneSplitterHeight = 29,
kThemeMetricPopupButtonHeight = 30,
kThemeMetricSmallPopupButtonHeight = 31,
kThemeMetricLargeProgressBarThickness = 32,
kThemeMetricPullDownHeight = 33,
kThemeMetricSmallPullDownHeight = 34,
kThemeMetricSmallPushButtonHeight = 35,
kThemeMetricSmallRadioButtonHeight = 36,
kThemeMetricRelevanceIndicatorHeight = 37,
kThemeMetricResizeControlHeight = 38,
kThemeMetricSmallResizeControlHeight = 39,
kThemeMetricLargeRoundButtonSize = 40,
kThemeMetricHSliderHeight = 41,
kThemeMetricHSliderTickHeight = 42,
kThemeMetricSmallHSliderHeight = 43,
kThemeMetricSmallHSliderTickHeight = 44,
kThemeMetricVSliderWidth = 45,
kThemeMetricVSliderTickWidth = 46,
kThemeMetricSmallVSliderWidth = 47,
kThemeMetricSmallVSliderTickWidth = 48,
kThemeMetricTitleBarControlsHeight = 49,
kThemeMetricCheckBoxWidth = 50,
kThemeMetricSmallCheckBoxWidth = 51,
kThemeMetricRadioButtonWidth = 52,
kThemeMetricSmallRadioButtonWidth = 53,
kThemeMetricSmallHSliderMinThumbWidth = 54,
kThemeMetricSmallVSliderMinThumbHeight = 55,
kThemeMetricSmallHSliderTickOffset = 56,
kThemeMetricSmallVSliderTickOffset = 57,
kThemeMetricNormalProgressBarThickness = 58,
kThemeMetricProgressBarShadowOutset = 59,
kThemeMetricSmallProgressBarShadowOutset = 60,
kThemeMetricPrimaryGroupBoxContentInset = 61,
kThemeMetricSecondaryGroupBoxContentInset = 62,
};

typedef UInt32 ThemeMetric;

struct OpaqueThemeDrawingState;

typedef OpaqueThemeDrawingState *ThemeDrawingState;

using ThemeTabTitleDrawUPP = UPP<void(const Rect *bounds, ThemeTabStyle style, ThemeTabDirection direction, SInt16 depth, Boolean isColorDev, UInt32 userData)>;


using ThemeEraseUPP = UPP<void(const Rect *bounds, UInt32 eraseData, SInt16 depth, Boolean isColorDev)>;


using ThemeButtonDrawUPP = UPP<void(const Rect *bounds, ThemeButtonKind kind, const ThemeButtonDrawInfo *info, UInt32 userData, SInt16 depth, Boolean isColorDev)>;


using WindowTitleDrawingUPP = UPP<void(const Rect *bounds, SInt16 depth, Boolean colorDevice, UInt32 userData)>;


using ThemeIteratorUPP = UPP<Boolean(ConstStr255Param inFileName, SInt16 resID, Collection inThemeSettings, void *inUserData)>;


using MenuTitleDrawingUPP = UPP<void(const Rect *inBounds, SInt16 inDepth, Boolean inIsColorDevice, SInt32 inUserData)>;


using MenuItemDrawingUPP = UPP<void(const Rect *inBounds, SInt16 inDepth, Boolean inIsColorDevice, SInt32 inUserData)>;


OSStatus C_RegisterAppearanceClient();PASCAL_SUBTRAP(RegisterAppearanceClient, 0xAA74, 0x0015, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, RegisterAppearanceClient, InterfaceLib);


OSStatus C_UnregisterAppearanceClient();PASCAL_SUBTRAP(UnregisterAppearanceClient, 0xAA74, 0x0016, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, UnregisterAppearanceClient, InterfaceLib);


Boolean C_IsAppearanceClient(const ProcessSerialNumber *process);PASCAL_SUBTRAP(IsAppearanceClient, 0xAA74, 0xFFFF, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, IsAppearanceClient, InterfaceLib);


OSStatus C_SetThemePen(ThemeBrush inBrush, SInt16 inDepth, Boolean inIsColorDevice);PASCAL_SUBTRAP(SetThemePen, 0xAA74, 0x0001, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, SetThemePen, InterfaceLib);


OSStatus C_SetThemeBackground(ThemeBrush inBrush, SInt16 inDepth, Boolean inIsColorDevice);PASCAL_SUBTRAP(SetThemeBackground, 0xAA74, 0x0002, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, SetThemeBackground, InterfaceLib);


OSStatus C_SetThemeTextColor(ThemeTextColor inColor, SInt16 inDepth, Boolean inIsColorDevice);PASCAL_SUBTRAP(SetThemeTextColor, 0xAA74, 0x0003, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, SetThemeTextColor, InterfaceLib);


OSStatus C_SetThemeWindowBackground(WindowRef inWindow, ThemeBrush inBrush, Boolean inUpdate);PASCAL_SUBTRAP(SetThemeWindowBackground, 0xAA74, 0x0004, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, SetThemeWindowBackground, InterfaceLib);


OSStatus C_DrawThemeWindowHeader(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemeWindowHeader, 0xAA74, 0x0005, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeWindowHeader, InterfaceLib);


OSStatus C_DrawThemeWindowListViewHeader(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemeWindowListViewHeader, 0xAA74, 0x0006, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeWindowListViewHeader, InterfaceLib);


OSStatus C_DrawThemePlacard(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemePlacard, 0xAA74, 0x0007, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemePlacard, InterfaceLib);


OSStatus C_DrawThemeEditTextFrame(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemeEditTextFrame, 0xAA74, 0x0009, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeEditTextFrame, InterfaceLib);


OSStatus C_DrawThemeListBoxFrame(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemeListBoxFrame, 0xAA74, 0x000A, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeListBoxFrame, InterfaceLib);


OSStatus C_DrawThemeFocusRect(const Rect *inRect, Boolean inHasFocus);PASCAL_SUBTRAP(DrawThemeFocusRect, 0xAA74, 0x000B, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeFocusRect, InterfaceLib);


OSStatus C_DrawThemePrimaryGroup(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemePrimaryGroup, 0xAA74, 0x000C, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemePrimaryGroup, InterfaceLib);


OSStatus C_DrawThemeSecondaryGroup(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemeSecondaryGroup, 0xAA74, 0x000D, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeSecondaryGroup, InterfaceLib);


OSStatus C_DrawThemeSeparator(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemeSeparator, 0xAA74, 0x000E, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeSeparator, InterfaceLib);


OSStatus C_DrawThemeModelessDialogFrame(const Rect *inRect, ThemeDrawState inState);PASCAL_SUBTRAP(DrawThemeModelessDialogFrame, 0xAA74, 0x0008, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeModelessDialogFrame, InterfaceLib);


OSStatus C_DrawThemeGenericWell(const Rect *inRect, ThemeDrawState inState, Boolean inFillCenter);PASCAL_SUBTRAP(DrawThemeGenericWell, 0xAA74, 0x0022, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeGenericWell, InterfaceLib);


OSStatus C_DrawThemeFocusRegion(RgnHandle inRegion, Boolean inHasFocus);PASCAL_SUBTRAP(DrawThemeFocusRegion, 0xAA74, 0x0023, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, DrawThemeFocusRegion, InterfaceLib);


Boolean C_IsThemeInColor(SInt16 inDepth, Boolean inIsColorDevice);PASCAL_SUBTRAP(IsThemeInColor, 0xAA74, 0x0024, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, IsThemeInColor, InterfaceLib);


OSStatus C_GetThemeAccentColors(GUEST<CTabHandle> *outColors);PASCAL_SUBTRAP(GetThemeAccentColors, 0xAA74, 0x0025, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, GetThemeAccentColors, InterfaceLib);


OSStatus DrawThemeMenuBarBackground(const Rect *inBounds, ThemeMenuBarState inState, UInt32 inAttributes);TRAP_GLUE(AppearanceLib, DrawThemeMenuBarBackground, AppearanceDispatch, 0x0018);


OSStatus DrawThemeMenuTitle(const Rect *inMenuBarRect, const Rect *inTitleRect, ThemeMenuState inState, UInt32 inAttributes, MenuTitleDrawingUPP inTitleProc, UInt32 inTitleData);TRAP_GLUE(AppearanceLib, DrawThemeMenuTitle, AppearanceDispatch, 0x0019);


OSStatus C_GetThemeMenuBarHeight(GUEST<SInt16> *outHeight);PASCAL_SUBTRAP(GetThemeMenuBarHeight, 0xAA74, 0x001A, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, GetThemeMenuBarHeight, InterfaceLib);


OSStatus DrawThemeMenuBackground(const Rect *inMenuRect, ThemeMenuType inMenuType);TRAP_GLUE(AppearanceLib, DrawThemeMenuBackground, AppearanceDispatch, 0x001B);


OSStatus GetThemeMenuBackgroundRegion(const Rect *inMenuRect, ThemeMenuType menuType, RgnHandle region);TRAP_GLUE(AppearanceLib, GetThemeMenuBackgroundRegion, AppearanceDispatch, 0x001C);


OSStatus DrawThemeMenuItem(const Rect *inMenuRect, const Rect *inItemRect, SInt16 inVirtualMenuTop, SInt16 inVirtualMenuBottom, ThemeMenuState inState, ThemeMenuItemType inItemType, MenuItemDrawingUPP inDrawProc, UInt32 inUserData);TRAP_GLUE(AppearanceLib, DrawThemeMenuItem, AppearanceDispatch, 0x001D);


OSStatus DrawThemeMenuSeparator(const Rect *inItemRect);TRAP_GLUE(AppearanceLib, DrawThemeMenuSeparator, AppearanceDispatch, 0x001E);


OSStatus C_GetThemeMenuSeparatorHeight(GUEST<SInt16> *outHeight);PASCAL_SUBTRAP(GetThemeMenuSeparatorHeight, 0xAA74, 0x001F, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, GetThemeMenuSeparatorHeight, InterfaceLib);


OSStatus C_GetThemeMenuItemExtra(ThemeMenuItemType inItemType, GUEST<SInt16> *outHeight, GUEST<SInt16> *outWidth);PASCAL_SUBTRAP(GetThemeMenuItemExtra, 0xAA74, 0x0020, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, GetThemeMenuItemExtra, InterfaceLib);


OSStatus C_GetThemeMenuTitleExtra(GUEST<SInt16> *outWidth, Boolean inIsSquished);PASCAL_SUBTRAP(GetThemeMenuTitleExtra, 0xAA74, 0x0021, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, GetThemeMenuTitleExtra, InterfaceLib);


OSStatus C_GetTheme(Collection ioCollection);LIBRARY_FUNCTION(AppearanceLib, GetTheme, C_GetTheme);






OSStatus C_DrawThemeTabPane(const Rect *inRect, ThemeDrawState inState);LIBRARY_FUNCTION(AppearanceLib, DrawThemeTabPane, C_DrawThemeTabPane);


OSStatus C_DrawThemeTab(const Rect *inRect, ThemeTabStyle inStyle, ThemeTabDirection inDirection, ThemeTabTitleDrawUPP labelProc, UInt32 userData);LIBRARY_FUNCTION(AppearanceLib, DrawThemeTab, C_DrawThemeTab);


OSStatus C_GetThemeTabRegion(const Rect *inRect, ThemeTabStyle inStyle, ThemeTabDirection inDirection, RgnHandle ioRgn);LIBRARY_FUNCTION(AppearanceLib, GetThemeTabRegion, C_GetThemeTabRegion);


OSStatus C_SetThemeCursor(ThemeCursor inCursor);LIBRARY_FUNCTION(AppearanceLib, SetThemeCursor, C_SetThemeCursor);


OSStatus C_SetAnimatedThemeCursor(ThemeCursor inCursor, UInt32 inAnimationStep);LIBRARY_FUNCTION(AppearanceLib, SetAnimatedThemeCursor, C_SetAnimatedThemeCursor);


OSStatus C_GetThemeScrollBarThumbStyle(GUEST<ThemeScrollBarThumbStyle> *outStyle);LIBRARY_FUNCTION(AppearanceLib, GetThemeScrollBarThumbStyle, C_GetThemeScrollBarThumbStyle);


OSStatus C_GetThemeScrollBarArrowStyle(GUEST<ThemeScrollBarArrowStyle> *outStyle);LIBRARY_FUNCTION(AppearanceLib, GetThemeScrollBarArrowStyle, C_GetThemeScrollBarArrowStyle);


OSStatus C_GetThemeCheckBoxStyle(GUEST<ThemeCheckBoxStyle> *outStyle);LIBRARY_FUNCTION(AppearanceLib, GetThemeCheckBoxStyle, C_GetThemeCheckBoxStyle);


OSStatus C_UseThemeFont(ThemeFontID inFontID, ScriptCode inScript);LIBRARY_FUNCTION(AppearanceLib, UseThemeFont, C_UseThemeFont);


OSStatus C_GetThemeFont(ThemeFontID inFontID, ScriptCode inScript, Str255 outFontName, GUEST<SInt16> *outFontSize, Style *outStyle);LIBRARY_FUNCTION(AppearanceLib, GetThemeFont, C_GetThemeFont);


OSStatus C_DrawThemeTrack(const ThemeTrackDrawInfo *drawInfo, RgnHandle rgnGhost, ThemeEraseUPP eraseProc, UInt32 eraseData);LIBRARY_FUNCTION(AppearanceLib, DrawThemeTrack, C_DrawThemeTrack);


Boolean C_HitTestThemeTrack(const ThemeTrackDrawInfo *drawInfo, Point mousePoint, GUEST<ControlPartCode> *partHit);LIBRARY_FUNCTION(AppearanceLib, HitTestThemeTrack, C_HitTestThemeTrack);


OSStatus C_GetThemeTrackBounds(const ThemeTrackDrawInfo *drawInfo, Rect *bounds);LIBRARY_FUNCTION(AppearanceLib, GetThemeTrackBounds, C_GetThemeTrackBounds);


OSStatus C_GetThemeTrackThumbRgn(const ThemeTrackDrawInfo *drawInfo, RgnHandle thumbRgn);LIBRARY_FUNCTION(AppearanceLib, GetThemeTrackThumbRgn, C_GetThemeTrackThumbRgn);


OSStatus C_GetThemeTrackDragRect(const ThemeTrackDrawInfo *drawInfo, Rect *dragRect);LIBRARY_FUNCTION(AppearanceLib, GetThemeTrackDragRect, C_GetThemeTrackDragRect);


OSStatus C_DrawThemeTrackTickMarks(const ThemeTrackDrawInfo *drawInfo, ItemCount numTicks, ThemeEraseUPP eraseProc, UInt32 eraseData);LIBRARY_FUNCTION(AppearanceLib, DrawThemeTrackTickMarks, C_DrawThemeTrackTickMarks);


OSStatus C_GetThemeTrackThumbPositionFromOffset(const ThemeTrackDrawInfo *drawInfo, Point thumbOffset, GUEST<SInt32> *relativePosition);LIBRARY_FUNCTION(AppearanceLib, GetThemeTrackThumbPositionFromOffset, C_GetThemeTrackThumbPositionFromOffset);


OSStatus C_GetThemeTrackThumbPositionFromRegion(const ThemeTrackDrawInfo *drawInfo, RgnHandle thumbRgn, GUEST<SInt32> *relativePosition);LIBRARY_FUNCTION(AppearanceLib, GetThemeTrackThumbPositionFromRegion, C_GetThemeTrackThumbPositionFromRegion);


OSStatus C_GetThemeTrackLiveValue(const ThemeTrackDrawInfo *drawInfo, SInt32 relativePosition, GUEST<SInt32> *value);LIBRARY_FUNCTION(AppearanceLib, GetThemeTrackLiveValue, C_GetThemeTrackLiveValue);


OSStatus C_DrawThemeScrollBarArrows(const Rect *bounds, ThemeTrackEnableState enableState, ThemeTrackPressState pressState, Boolean isHoriz, Rect *trackBounds);LIBRARY_FUNCTION(AppearanceLib, DrawThemeScrollBarArrows, C_DrawThemeScrollBarArrows);


OSStatus C_GetThemeScrollBarTrackRect(const Rect *bounds, ThemeTrackEnableState enableState, ThemeTrackPressState pressState, Boolean isHoriz, Rect *trackBounds);LIBRARY_FUNCTION(AppearanceLib, GetThemeScrollBarTrackRect, C_GetThemeScrollBarTrackRect);


Boolean C_HitTestThemeScrollBarArrows(const Rect *scrollBarBounds, ThemeTrackEnableState enableState, ThemeTrackPressState pressState, Boolean isHoriz, Point ptHit, Rect *trackBounds, GUEST<ControlPartCode> *partcode);LIBRARY_FUNCTION(AppearanceLib, HitTestThemeScrollBarArrows, C_HitTestThemeScrollBarArrows);


OSStatus C_GetThemeWindowRegion(ThemeWindowType flavor, const Rect *contRect, ThemeDrawState state, const ThemeWindowMetrics *metrics, ThemeWindowAttributes attributes, WindowRegionCode winRegion, RgnHandle rgn);LIBRARY_FUNCTION(AppearanceLib, GetThemeWindowRegion, C_GetThemeWindowRegion);


OSStatus C_DrawThemeWindowFrame(ThemeWindowType flavor, const Rect *contRect, ThemeDrawState state, const ThemeWindowMetrics *metrics, ThemeWindowAttributes attributes, WindowTitleDrawingUPP titleProc, UInt32 titleData);LIBRARY_FUNCTION(AppearanceLib, DrawThemeWindowFrame, C_DrawThemeWindowFrame);


OSStatus C_DrawThemeTitleBarWidget(ThemeWindowType flavor, const Rect *contRect, ThemeDrawState state, const ThemeWindowMetrics *metrics, ThemeWindowAttributes attributes, ThemeTitleBarWidget widget);LIBRARY_FUNCTION(AppearanceLib, DrawThemeTitleBarWidget, C_DrawThemeTitleBarWidget);


Boolean C_GetThemeWindowRegionHit(ThemeWindowType flavor, const Rect *inContRect, ThemeDrawState state, const ThemeWindowMetrics *metrics, ThemeWindowAttributes inAttributes, Point inPoint, GUEST<WindowRegionCode> *outRegionHit);LIBRARY_FUNCTION(AppearanceLib, GetThemeWindowRegionHit, C_GetThemeWindowRegionHit);


OSStatus C_DrawThemeScrollBarDelimiters(ThemeWindowType flavor, const Rect *inContRect, ThemeDrawState state, ThemeWindowAttributes attributes);LIBRARY_FUNCTION(AppearanceLib, DrawThemeScrollBarDelimiters, C_DrawThemeScrollBarDelimiters);


OSStatus C_DrawThemeButton(const Rect *inBounds, ThemeButtonKind inKind, const ThemeButtonDrawInfo *inNewInfo, const ThemeButtonDrawInfo *inPrevInfo, ThemeEraseUPP inEraseProc, ThemeButtonDrawUPP inLabelProc, UInt32 inUserData);LIBRARY_FUNCTION(AppearanceLib, DrawThemeButton, C_DrawThemeButton);


OSStatus C_GetThemeButtonRegion(const Rect *inBounds, ThemeButtonKind inKind, const ThemeButtonDrawInfo *inNewInfo, RgnHandle outRegion);LIBRARY_FUNCTION(AppearanceLib, GetThemeButtonRegion, C_GetThemeButtonRegion);


OSStatus C_GetThemeButtonContentBounds(const Rect *inBounds, ThemeButtonKind inKind, const ThemeButtonDrawInfo *inDrawInfo, Rect *outBounds);LIBRARY_FUNCTION(AppearanceLib, GetThemeButtonContentBounds, C_GetThemeButtonContentBounds);


OSStatus C_GetThemeButtonBackgroundBounds(const Rect *inBounds, ThemeButtonKind inKind, const ThemeButtonDrawInfo *inDrawInfo, Rect *outBounds);LIBRARY_FUNCTION(AppearanceLib, GetThemeButtonBackgroundBounds, C_GetThemeButtonBackgroundBounds);


OSStatus C_PlayThemeSound(ThemeSoundKind kind);PASCAL_SUBTRAP(PlayThemeSound, 0xAA74, 0x0026, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, PlayThemeSound, InterfaceLib);


OSStatus C_BeginThemeDragSound(ThemeDragSoundKind kind);PASCAL_SUBTRAP(BeginThemeDragSound, 0xAA74, 0x0027, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, BeginThemeDragSound, InterfaceLib);


OSStatus C_EndThemeDragSound();PASCAL_SUBTRAP(EndThemeDragSound, 0xAA74, 0x0028, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, EndThemeDragSound, InterfaceLib);


OSStatus C_DrawThemeTickMark(const Rect *bounds, ThemeDrawState state);LIBRARY_FUNCTION(AppearanceLib, DrawThemeTickMark, C_DrawThemeTickMark);


OSStatus C_DrawThemeChasingArrows(const Rect *bounds, UInt32 index, ThemeDrawState state, ThemeEraseUPP eraseProc, UInt32 eraseData);LIBRARY_FUNCTION(AppearanceLib, DrawThemeChasingArrows, C_DrawThemeChasingArrows);


OSStatus C_DrawThemePopupArrow(const Rect *bounds, ThemeArrowOrientation orientation, ThemePopupArrowSize size, ThemeDrawState state, ThemeEraseUPP eraseProc, UInt32 eraseData);LIBRARY_FUNCTION(AppearanceLib, DrawThemePopupArrow, C_DrawThemePopupArrow);








OSStatus C_NormalizeThemeDrawingState();LIBRARY_FUNCTION(AppearanceLib, NormalizeThemeDrawingState, C_NormalizeThemeDrawingState);


OSStatus C_GetThemeDrawingState(GUEST<ThemeDrawingState> *outState);LIBRARY_FUNCTION(AppearanceLib, GetThemeDrawingState, C_GetThemeDrawingState);


OSStatus C_SetThemeDrawingState(ThemeDrawingState inState, Boolean inDisposeNow);LIBRARY_FUNCTION(AppearanceLib, SetThemeDrawingState, C_SetThemeDrawingState);


OSStatus C_DisposeThemeDrawingState(ThemeDrawingState inState);LIBRARY_FUNCTION(AppearanceLib, DisposeThemeDrawingState, C_DisposeThemeDrawingState);


OSStatus C_ApplyThemeBackground(ThemeBackgroundKind inKind, const Rect *bounds, ThemeDrawState inState, SInt16 inDepth, Boolean inColorDev);LIBRARY_FUNCTION(AppearanceLib, ApplyThemeBackground, C_ApplyThemeBackground);


OSStatus C_SetThemeTextColorForWindow(WindowRef window, Boolean isActive, SInt16 depth, Boolean isColorDev);LIBRARY_FUNCTION(AppearanceLib, SetThemeTextColorForWindow, C_SetThemeTextColorForWindow);




OSStatus C_GetThemeBrushAsColor(ThemeBrush inBrush, SInt16 inDepth, Boolean inColorDev, RGBColor *outColor);LIBRARY_FUNCTION(AppearanceLib, GetThemeBrushAsColor, C_GetThemeBrushAsColor);


OSStatus C_GetThemeTextColor(ThemeTextColor inColor, SInt16 inDepth, Boolean inColorDev, RGBColor *outColor);LIBRARY_FUNCTION(AppearanceLib, GetThemeTextColor, C_GetThemeTextColor);


enum {appearanceBadBrushIndexErr = themeInvalidBrushErr,
appearanceProcessRegisteredErr = themeProcessRegisteredErr,
appearanceProcessNotRegisteredErr = themeProcessNotRegisteredErr,
appearanceBadTextColorIndexErr = themeBadTextColorErr,
appearanceThemeHasNoAccents = themeHasNoAccentsErr,
appearanceBadCursorIndexErr = themeBadCursorIndexErr,
};

enum {kThemeActiveDialogBackgroundBrush = kThemeBrushDialogBackgroundActive,
kThemeInactiveDialogBackgroundBrush = kThemeBrushDialogBackgroundInactive,
kThemeActiveAlertBackgroundBrush = kThemeBrushAlertBackgroundActive,
kThemeInactiveAlertBackgroundBrush = kThemeBrushAlertBackgroundInactive,
kThemeActiveModelessDialogBackgroundBrush = kThemeBrushModelessDialogBackgroundActive,
kThemeInactiveModelessDialogBackgroundBrush = kThemeBrushModelessDialogBackgroundInactive,
kThemeActiveUtilityWindowBackgroundBrush = kThemeBrushUtilityWindowBackgroundActive,
kThemeInactiveUtilityWindowBackgroundBrush = kThemeBrushUtilityWindowBackgroundInactive,
kThemeListViewSortColumnBackgroundBrush = kThemeBrushListViewSortColumnBackground,
kThemeListViewBackgroundBrush = kThemeBrushListViewBackground,
kThemeIconLabelBackgroundBrush = kThemeBrushIconLabelBackground,
kThemeListViewSeparatorBrush = kThemeBrushListViewSeparator,
kThemeChasingArrowsBrush = kThemeBrushChasingArrows,
kThemeDragHiliteBrush = kThemeBrushDragHilite,
kThemeDocumentWindowBackgroundBrush = kThemeBrushDocumentWindowBackground,
kThemeFinderWindowBackgroundBrush = kThemeBrushFinderWindowBackground,
};

enum {kThemeActiveScrollBarDelimiterBrush = kThemeBrushScrollBarDelimiterActive,
kThemeInactiveScrollBarDelimiterBrush = kThemeBrushScrollBarDelimiterInactive,
kThemeFocusHighlightBrush = kThemeBrushFocusHighlight,
kThemeActivePopupArrowBrush = kThemeBrushPopupArrowActive,
kThemePressedPopupArrowBrush = kThemeBrushPopupArrowPressed,
kThemeInactivePopupArrowBrush = kThemeBrushPopupArrowInactive,
kThemeAppleGuideCoachmarkBrush = kThemeBrushAppleGuideCoachmark,
};

enum {kThemeActiveDialogTextColor = kThemeTextColorDialogActive,
kThemeInactiveDialogTextColor = kThemeTextColorDialogInactive,
kThemeActiveAlertTextColor = kThemeTextColorAlertActive,
kThemeInactiveAlertTextColor = kThemeTextColorAlertInactive,
kThemeActiveModelessDialogTextColor = kThemeTextColorModelessDialogActive,
kThemeInactiveModelessDialogTextColor = kThemeTextColorModelessDialogInactive,
kThemeActiveWindowHeaderTextColor = kThemeTextColorWindowHeaderActive,
kThemeInactiveWindowHeaderTextColor = kThemeTextColorWindowHeaderInactive,
kThemeActivePlacardTextColor = kThemeTextColorPlacardActive,
kThemeInactivePlacardTextColor = kThemeTextColorPlacardInactive,
kThemePressedPlacardTextColor = kThemeTextColorPlacardPressed,
kThemeActivePushButtonTextColor = kThemeTextColorPushButtonActive,
kThemeInactivePushButtonTextColor = kThemeTextColorPushButtonInactive,
kThemePressedPushButtonTextColor = kThemeTextColorPushButtonPressed,
kThemeActiveBevelButtonTextColor = kThemeTextColorBevelButtonActive,
kThemeInactiveBevelButtonTextColor = kThemeTextColorBevelButtonInactive,
kThemePressedBevelButtonTextColor = kThemeTextColorBevelButtonPressed,
kThemeActivePopupButtonTextColor = kThemeTextColorPopupButtonActive,
kThemeInactivePopupButtonTextColor = kThemeTextColorPopupButtonInactive,
kThemePressedPopupButtonTextColor = kThemeTextColorPopupButtonPressed,
kThemeIconLabelTextColor = kThemeTextColorIconLabel,
kThemeListViewTextColor = kThemeTextColorListView,
};

enum {kThemeActiveDocumentWindowTitleTextColor = kThemeTextColorDocumentWindowTitleActive,
kThemeInactiveDocumentWindowTitleTextColor = kThemeTextColorDocumentWindowTitleInactive,
kThemeActiveMovableModalWindowTitleTextColor = kThemeTextColorMovableModalWindowTitleActive,
kThemeInactiveMovableModalWindowTitleTextColor = kThemeTextColorMovableModalWindowTitleInactive,
kThemeActiveUtilityWindowTitleTextColor = kThemeTextColorUtilityWindowTitleActive,
kThemeInactiveUtilityWindowTitleTextColor = kThemeTextColorUtilityWindowTitleInactive,
kThemeActivePopupWindowTitleColor = kThemeTextColorPopupWindowTitleActive,
kThemeInactivePopupWindowTitleColor = kThemeTextColorPopupWindowTitleInactive,
kThemeActiveRootMenuTextColor = kThemeTextColorRootMenuActive,
kThemeSelectedRootMenuTextColor = kThemeTextColorRootMenuSelected,
kThemeDisabledRootMenuTextColor = kThemeTextColorRootMenuDisabled,
kThemeActiveMenuItemTextColor = kThemeTextColorMenuItemActive,
kThemeSelectedMenuItemTextColor = kThemeTextColorMenuItemSelected,
kThemeDisabledMenuItemTextColor = kThemeTextColorMenuItemDisabled,
kThemeActivePopupLabelTextColor = kThemeTextColorPopupLabelActive,
kThemeInactivePopupLabelTextColor = kThemeTextColorPopupLabelInactive,
};

enum {kAEThemeSwitch = kAEAppearanceChanged,
};

enum {kThemeNoAdornment = kThemeAdornmentNone,
kThemeDefaultAdornment = kThemeAdornmentDefault,
kThemeFocusAdornment = kThemeAdornmentFocus,
kThemeRightToLeftAdornment = kThemeAdornmentRightToLeft,
kThemeDrawIndicatorOnly = kThemeAdornmentDrawIndicatorOnly,
};

enum {kThemeBrushPassiveAreaFill = kThemeBrushStaticAreaFill,
};

enum {kThemeMetricCheckBoxGlyphHeight = kThemeMetricCheckBoxHeight,
kThemeMetricRadioButtonGlyphHeight = kThemeMetricRadioButtonHeight,
kThemeMetricDisclosureButtonSize = kThemeMetricDisclosureButtonHeight,
kThemeMetricBestListHeaderHeight = kThemeMetricListHeaderHeight,
kThemeMetricSmallProgressBarThickness = kThemeMetricNormalProgressBarThickness,
kThemeMetricProgressBarThickness = kThemeMetricLargeProgressBarThickness,
};

enum {kThemeScrollBar = kThemeMediumScrollBar,
kThemeSlider = kThemeMediumSlider,
kThemeProgressBar = kThemeMediumProgressBar,
kThemeIndeterminateBar = kThemeMediumIndeterminateBar,
};

OSStatus C_GetViewsFont(ScriptCode script, StringPtr outFontName, GUEST<SInt16> *outFontSize);LIBRARY_FUNCTION(AppearanceLib, GetViewsFont, C_GetViewsFont);


OSStatus C_GetSmallSystemFont(ScriptCode script, StringPtr outFontName);LIBRARY_FUNCTION(AppearanceLib, GetSmallSystemFont, C_GetSmallSystemFont);


Boolean C_AreThemeSoundsEnabled();LIBRARY_FUNCTION(AppearanceLib, AreThemeSoundsEnabled, C_AreThemeSoundsEnabled);


OSStatus C_GetScenario(Collection ioCollection);LIBRARY_FUNCTION(AppearanceLib, GetScenario, C_GetScenario);


OSStatus C_GetCurrentTheme(ConstStringPtr themeName, Ptr outThemeFile);LIBRARY_FUNCTION(AppearanceLib, GetCurrentTheme, C_GetCurrentTheme);


OSStatus C_GetThemeVariantList(ConstStringPtr themeName, GUEST<Handle> *outList);LIBRARY_FUNCTION(AppearanceLib, GetThemeVariantList, C_GetThemeVariantList);


OSStatus C_CountThemeVariants(Handle list, GUEST<SInt16> *outCount);LIBRARY_FUNCTION(AppearanceLib, CountThemeVariants, C_CountThemeVariants);


OSStatus C_GetIndThemeVariant(Handle list, SInt16 index, StringPtr outName);LIBRARY_FUNCTION(AppearanceLib, GetIndThemeVariant, C_GetIndThemeVariant);


OSStatus C_GetThemeSoundMask(GUEST<UInt32> *outMask);LIBRARY_FUNCTION(AppearanceLib, GetThemeSoundMask, C_GetThemeSoundMask);


Boolean C_IsThemeCreditDialogAvailable();LIBRARY_FUNCTION(AppearanceLib, IsThemeCreditDialogAvailable, C_IsThemeCreditDialogAvailable);


} /* namespace Executor */