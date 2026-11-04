/*****************************************************************************
 *                                                                           *
 *                                WindowMgr8.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          OSType, UInt32, SInt16, Rect, Boolean, SInt32,    *
 *                         Handle, StringPtr, Ptr, ConstStringPtr            *
 *     QuickDraw.h         RgnHandle, GrafPtr, RGBColor, PixPatHandle        *
 *     WindowMgr.h         WindowRegionCode, WStateData, WindowRef,          *
 *                         WindowDefUPP                                      *
 *     CQuickDraw.h        GWorldPtr, GDHandle                               *
 *     DriverServices.h    OSStatus                                          *
 *     Collections.h       Collection                                        *
 *     FileMgr.h           OptionBits, FSSpec                                *
 *     AliasMgr.h          AliasHandle                                       *
 *     IconServices.h      IconRef                                           *
 *     DragMgr.h           DragReference                                     *
 *     EventMgr.h          EventRecord                                       *
 *     MenuMgr.h           MenuRef                                           *
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
#include "CQuickDraw.h"
#include "DriverServices.h"
#include "Collections.h"
#include "FileMgr.h"
#include "AliasMgr.h"
#include "IconServices.h"
#include "DragMgr.h"
#include "EventMgr.h"
#include "MenuMgr.h"

#define MODULE_NAME WindowMgr8
#include <base/api-module.h>


namespace Executor {

enum {errInvalidWindowPtr = -5600,
errUnsupportedWindowAttributesForClass = -5601,
errWindowDoesNotHaveProxy = -5602,
errInvalidWindowProperty = -5603,
errWindowPropertyNotFound = -5604,
errUnrecognizedWindowClass = -5605,
errCorruptWindowDescription = -5606,
errUserWantsToDragWindow = -5607,
errWindowsAlreadyInitialized = -5608,
errFloatingWindowsNotInitialized = -5609,
errWindowRegionCodeInvalid = -30593,
};

typedef OSType PropertyCreator;

typedef OSType PropertyTag;

typedef UInt32 WindowClass;

enum {kAlertWindowClass = 1,
kMovableAlertWindowClass = 2,
kModalWindowClass = 3,
kMovableModalWindowClass = 4,
kFloatingWindowClass = 5,
kDocumentWindowClass = 6,
kUtilityWindowClass = 8,
kHelpWindowClass = 10,
kSheetWindowClass = 11,
kToolbarWindowClass = 12,
kPlainWindowClass = 13,
kOverlayWindowClass = 14,
kSheetAlertWindowClass = 15,
kAltPlainWindowClass = 16,
kAllWindowClasses = (unsigned long)0xFFFFFFFF,
};

typedef UInt32 WindowAttributes;

enum {kWindowNoAttributes = 0,
kWindowCloseBoxAttribute = (1 << 0),
kWindowHorizontalZoomAttribute = (1 << 1),
kWindowVerticalZoomAttribute = (1 << 2),
kWindowFullZoomAttribute = (kWindowVerticalZoomAttribute | kWindowHorizontalZoomAttribute),
kWindowCollapseBoxAttribute = (1 << 3),
kWindowResizableAttribute = (1 << 4),
kWindowSideTitlebarAttribute = (1 << 5),
kWindowNoUpdatesAttribute = (1 << 16),
kWindowNoActivatesAttribute = (1 << 17),
kWindowOpaqueForEventsAttribute = (1 << 18),
kWindowNoShadowAttribute = (1 << 21),
kWindowHideOnSuspendAttribute = (1 << 24),
kWindowStandardHandlerAttribute = (1 << 25),
kWindowHideOnFullScreenAttribute = (1 << 26),
kWindowInWindowMenuAttribute = (1 << 27),
kWindowLiveResizeAttribute = (1 << 28),
kWindowStandardDocumentAttributes = (kWindowCloseBoxAttribute | kWindowFullZoomAttribute | kWindowCollapseBoxAttribute | kWindowResizableAttribute),
kWindowStandardFloatingAttributes = (kWindowCloseBoxAttribute | kWindowCollapseBoxAttribute),
};

enum {kWindowDefProcType = "WDEF"_4,
};

enum {kStandardWindowDefinition = 0,
kRoundWindowDefinition = 1,
kFloatingWindowDefinition = 124,
};

enum {kDocumentWindowVariantCode = 0,
kModalDialogVariantCode = 1,
kPlainDialogVariantCode = 2,
kShadowDialogVariantCode = 3,
kMovableModalDialogVariantCode = 5,
kAlertVariantCode = 7,
kMovableAlertVariantCode = 9,
kSideFloaterVariantCode = 8,
};

enum {zoomDocProc = 8,
zoomNoGrow = 12,
floatProc = 1985,
floatGrowProc = 1987,
floatZoomProc = 1989,
floatZoomGrowProc = 1991,
floatSideProc = 1993,
floatSideGrowProc = 1995,
floatSideZoomProc = 1997,
floatSideZoomGrowProc = 1999,
};

enum {kWindowDocumentDefProcResID = 64,
kWindowDialogDefProcResID = 65,
kWindowUtilityDefProcResID = 66,
kWindowUtilitySideTitleDefProcResID = 67,
kWindowSheetDefProcResID = 68,
kWindowSimpleDefProcResID = 69,
kWindowSheetAlertDefProcResID = 70,
};

enum {kWindowDocumentProc = 1024,
kWindowGrowDocumentProc = 1025,
kWindowVertZoomDocumentProc = 1026,
kWindowVertZoomGrowDocumentProc = 1027,
kWindowHorizZoomDocumentProc = 1028,
kWindowHorizZoomGrowDocumentProc = 1029,
kWindowFullZoomDocumentProc = 1030,
kWindowFullZoomGrowDocumentProc = 1031,
};

enum {kWindowPlainDialogProc = 1040,
kWindowShadowDialogProc = 1041,
kWindowModalDialogProc = 1042,
kWindowMovableModalDialogProc = 1043,
kWindowAlertProc = 1044,
kWindowMovableAlertProc = 1045,
};

enum {kWindowMovableModalGrowProc = 1046,
};

enum {kWindowFloatProc = 1057,
kWindowFloatGrowProc = 1059,
kWindowFloatVertZoomProc = 1061,
kWindowFloatVertZoomGrowProc = 1063,
kWindowFloatHorizZoomProc = 1065,
kWindowFloatHorizZoomGrowProc = 1067,
kWindowFloatFullZoomProc = 1069,
kWindowFloatFullZoomGrowProc = 1071,
};

enum {kWindowFloatSideProc = 1073,
kWindowFloatSideGrowProc = 1075,
kWindowFloatSideVertZoomProc = 1077,
kWindowFloatSideVertZoomGrowProc = 1079,
kWindowFloatSideHorizZoomProc = 1081,
kWindowFloatSideHorizZoomGrowProc = 1083,
kWindowFloatSideFullZoomProc = 1085,
kWindowFloatSideFullZoomGrowProc = 1087,
};

enum {kWindowSheetProc = 1088,
kWindowSheetAlertProc = 1120,
};

enum {kWindowSimpleProc = 1104,
kWindowSimpleFrameProc = 1105,
};

enum {kWindowNoPosition = 0,
kWindowDefaultPosition = 0,
kWindowCenterMainScreen = 10250,
kWindowAlertPositionMainScreen = 12298,
kWindowStaggerMainScreen = 14346,
kWindowCenterParentWindow = 43018,
kWindowAlertPositionParentWindow = 45066,
kWindowStaggerParentWindow = 47114,
kWindowCenterParentWindowScreen = 26634,
kWindowAlertPositionParentWindowScreen = 28682,
kWindowStaggerParentWindowScreen = 30730,
};

typedef UInt32 WindowPositionMethod;

enum {kWindowCenterOnMainScreen = 1,
kWindowCenterOnParentWindow = 2,
kWindowCenterOnParentWindowScreen = 3,
kWindowCascadeOnMainScreen = 4,
kWindowCascadeOnParentWindow = 5,
kWindowCascadeOnParentWindowScreen = 6,
kWindowAlertPositionOnMainScreen = 7,
kWindowAlertPositionOnParentWindow = 8,
kWindowAlertPositionOnParentWindowScreen = 9,
};

enum {kWindowOpaqueRgn = 35,
};

struct GetWindowRegionRec{GUEST_STRUCT;GUEST<RgnHandle> winRgn;GUEST<WindowRegionCode> regionCode;};void describeStruct(const GetWindowRegionRec&, std::ostream&);

typedef GetWindowRegionRec *GetWindowRegionPtr;

typedef GetWindowRegionRec *GetWindowRegionRecPtr;

struct SetupWindowProxyDragImageRec{GUEST_STRUCT;GUEST<GWorldPtr> imageGWorld;GUEST<RgnHandle> imageRgn;GUEST<RgnHandle> outlineRgn;};void describeStruct(const SetupWindowProxyDragImageRec&, std::ostream&);

struct MeasureWindowTitleRec{GUEST_STRUCT;GUEST<SInt16> fullTitleWidth;GUEST<SInt16> titleTextWidth;};void describeStruct(const MeasureWindowTitleRec&, std::ostream&);

typedef MeasureWindowTitleRec *MeasureWindowTitleRecPtr;

struct GetGrowImageRegionRec{GUEST_STRUCT;Rect growRect;GUEST<RgnHandle> growImageRegion;};void describeStruct(const GetGrowImageRegionRec&, std::ostream&);

enum {kDialogWindowKind = 2,
kApplicationWindowKind = 8,
};

typedef SInt16 WindowPartCode;

enum {inNoWindow = 0,
inCollapseBox = 11,
inProxyIcon = 12,
};

typedef SInt16 WindowDefPartCode;

enum {wInCollapseBox = 9,
wInProxyIcon = 10,
};

enum {kWindowMsgGetFeatures = 7,
kWindowMsgGetRegion = 8,
};

enum {kWindowMsgDragHilite = 9,
kWindowMsgModified = 10,
kWindowMsgDrawInCurrentPort = 11,
kWindowMsgSetupProxyDragImage = 12,
kWindowMsgStateChanged = 13,
kWindowMsgMeasureTitle = 14,
};

enum {kWindowMsgGetGrowImageRegion = 19,
};

enum {kWindowStateTitleChanged = (1 << 0),
};

enum {kWindowCanGrow = (1 << 0),
kWindowCanZoom = (1 << 1),
kWindowCanCollapse = (1 << 2),
kWindowIsModal = (1 << 3),
kWindowCanGetWindowRegion = (1 << 4),
kWindowIsAlert = (1 << 5),
kWindowHasTitleBar = (1 << 6),
};

enum {kWindowSupportsDragHilite = (1 << 7),
kWindowSupportsModifiedBit = (1 << 8),
kWindowCanDrawInCurrentPort = (1 << 9),
kWindowCanSetupProxyDragImage = (1 << 10),
kWindowCanMeasureTitle = (1 << 11),
kWindowWantsDisposeAtProcessDeath = (1 << 12),
kWindowSupportsGetGrowImageRegion = (1 << 13),
kWindowDefSupportsColorGrafPort = 1073741826,
};

enum {kWindowIsOpaque = (1 << 14),
};

enum {kWindowSupportsSetGrowImageRegion = (1 << 13),
};

enum {kMouseUpOutOfSlop = (long)0x80008000,
};

enum {kWindowDefinitionVersionOne = 1,
kWindowDefinitionVersionTwo = 2,
};

enum {kWindowIsCollapsedState = (1 << 0),
};

enum {kStoredWindowSystemTag = "appl"_4,
kStoredBasicWindowDescriptionID = "sbas"_4,
kStoredWindowPascalTitleID = "s255"_4,
};

typedef WStateData *WStateDataPtr;

typedef GUEST<WStateDataPtr> *WStateDataHandle;

using WindowPaintUPP = UPP<OSStatus(GDHandle device, GrafPtr qdContext, WindowRef window, RgnHandle inClientPaintRgn, RgnHandle outSystemPaintRgn, void *refCon)>;


enum {kWindowDefProcPtr = 0,
kWindowDefObjectClass = 1,
kWindowDefProcID = 2,
};

typedef UInt32 WindowDefType;

union WindowDefSpecUnion{GUEST_STRUCT;GUEST<WindowDefUPP> defProc;GUEST<void*> classRef;GUEST<short> procID;};void describeStruct(const WindowDefSpecUnion&, std::ostream&);

struct WindowDefSpec{GUEST_STRUCT;GUEST<WindowDefType> defType;WindowDefSpecUnion u;};void describeStruct(const WindowDefSpec&, std::ostream&);

typedef WindowDefSpec *WindowDefSpecPtr;

OSStatus C_CreateNewWindow(WindowClass windowClass, WindowAttributes attributes, const Rect *contentBounds, GUEST<WindowRef> *outWindow);LIBRARY_FUNCTION(WindowsLib, CreateNewWindow, C_CreateNewWindow);








OSStatus C_GetWindowOwnerCount(WindowRef window, GUEST<UInt32> *outCount);LIBRARY_FUNCTION(WindowsLib, GetWindowOwnerCount, C_GetWindowOwnerCount);


OSStatus C_CloneWindow(WindowRef window);LIBRARY_FUNCTION(WindowsLib, CloneWindow, C_CloneWindow);


OSStatus C_GetWindowWidgetHilite(WindowRef inWindow, GUEST<WindowDefPartCode> *outHilite);LIBRARY_FUNCTION(WindowsLib, GetWindowWidgetHilite, C_GetWindowWidgetHilite);


OSStatus C_GetWindowClass(WindowRef window, GUEST<WindowClass> *outClass);LIBRARY_FUNCTION(WindowsLib, GetWindowClass, C_GetWindowClass);


OSStatus C_GetWindowAttributes(WindowRef window, GUEST<WindowAttributes> *outAttributes);LIBRARY_FUNCTION(WindowsLib, GetWindowAttributes, C_GetWindowAttributes);


typedef UInt32 WindowModality;

enum {kWindowModalityNone = 0,
kWindowModalitySystemModal = 1,
kWindowModalityAppModal = 2,
kWindowModalityWindowModal = 3,
};

OSStatus C_ShowFloatingWindows();LIBRARY_FUNCTION(WindowsLib, ShowFloatingWindows, C_ShowFloatingWindows);


OSStatus C_HideFloatingWindows();LIBRARY_FUNCTION(WindowsLib, HideFloatingWindows, C_HideFloatingWindows);


Boolean C_AreFloatingWindowsVisible();LIBRARY_FUNCTION(WindowsLib, AreFloatingWindowsVisible, C_AreFloatingWindowsVisible);


struct OpaqueWindowGroupRef;

typedef OpaqueWindowGroupRef *WindowGroupRef;

enum {windowGroupInvalidErr = -5616,
};

typedef UInt32 WindowGroupAttributes;

enum {kWindowGroupAttrSelectAsLayer = 1 << 0,
kWindowGroupAttrMoveTogether = 1 << 1,
kWindowGroupAttrLayerTogether = 1 << 2,
kWindowGroupAttrSharedActivation = 1 << 3,
kWindowGroupAttrHideOnCollapse = 1 << 4,
};

typedef UInt32 WindowActivationScope;

enum {kWindowActivationScopeNone = 0,
kWindowActivationScopeIndependent = 1,
kWindowActivationScopeAll = 2,
};

enum {kNextWindowGroup = true,
kPreviousWindowGroup = false,
};

typedef UInt32 WindowGroupContentOptions;

enum {kWindowGroupContentsReturnWindows = 1 << 0,
kWindowGroupContentsRecurse = 1 << 1,
kWindowGroupContentsVisible = 1 << 2,
};

OSStatus C_SetWindowContentColor(WindowRef window, const RGBColor *color);LIBRARY_FUNCTION(WindowsLib, SetWindowContentColor, C_SetWindowContentColor);


OSStatus C_GetWindowContentColor(WindowRef window, RGBColor *color);LIBRARY_FUNCTION(WindowsLib, GetWindowContentColor, C_GetWindowContentColor);


OSStatus C_GetWindowContentPattern(WindowRef window, PixPatHandle outPixPat);LIBRARY_FUNCTION(WindowsLib, GetWindowContentPattern, C_GetWindowContentPattern);


OSStatus C_SetWindowContentPattern(WindowRef window, PixPatHandle pixPat);LIBRARY_FUNCTION(WindowsLib, SetWindowContentPattern, C_SetWindowContentPattern);


typedef OptionBits WindowPaintProcOptions;

enum {kWindowPaintProcOptionsNone = 0,
};



typedef UInt32 ScrollWindowOptions;

enum {kScrollWindowNoOptions = 0,
kScrollWindowInvalidate = (1 << 0),
kScrollWindowEraseToPortBackground = (1 << 1),
};

WindowRef C_FrontNonFloatingWindow();LIBRARY_FUNCTION(WindowsLib, FrontNonFloatingWindow, C_FrontNonFloatingWindow);


Boolean C_IsValidWindowPtr(WindowRef possibleWindow);LIBRARY_FUNCTION(WindowsLib, IsValidWindowPtr, C_IsValidWindowPtr);


OSStatus C_InitFloatingWindows();LIBRARY_FUNCTION(WindowsLib, InitFloatingWindows, C_InitFloatingWindows);


EXTERN_DISPATCHER_TRAP(AppearanceDispatch, 0xAA74, D0W);
OSStatus C_GetWindowFeatures(WindowRef window, GUEST<UInt32> *outFeatures);PASCAL_SUBTRAP(GetWindowFeatures, 0xAA74, 0x0013, AppearanceDispatch);
LIBRARY_ALSO(AppearanceLib, GetWindowFeatures, InterfaceLib);


OSStatus C_GetWindowStructureWidths(WindowRef inWindow, Rect *outRect);LIBRARY_FUNCTION(WindowsLib, GetWindowStructureWidths, C_GetWindowStructureWidths);


OSStatus C_SetWindowProxyFSSpec(WindowRef window, const FSSpec *inFile);LIBRARY_FUNCTION(WindowsLib, SetWindowProxyFSSpec, C_SetWindowProxyFSSpec);


OSStatus C_GetWindowProxyFSSpec(WindowRef window, FSSpec *outFile);LIBRARY_FUNCTION(WindowsLib, GetWindowProxyFSSpec, C_GetWindowProxyFSSpec);


OSStatus C_SetWindowProxyAlias(WindowRef window, AliasHandle alias);LIBRARY_FUNCTION(WindowsLib, SetWindowProxyAlias, C_SetWindowProxyAlias);


OSStatus C_GetWindowProxyAlias(WindowRef window, GUEST<AliasHandle> *alias);LIBRARY_FUNCTION(WindowsLib, GetWindowProxyAlias, C_GetWindowProxyAlias);


OSStatus C_SetWindowProxyCreatorAndType(WindowRef window, OSType fileCreator, OSType fileType, SInt16 vRefNum);LIBRARY_FUNCTION(WindowsLib, SetWindowProxyCreatorAndType, C_SetWindowProxyCreatorAndType);


OSStatus C_GetWindowProxyIcon(WindowRef window, GUEST<IconRef> *outIcon);LIBRARY_FUNCTION(WindowsLib, GetWindowProxyIcon, C_GetWindowProxyIcon);


OSStatus C_SetWindowProxyIcon(WindowRef window, IconRef icon);LIBRARY_FUNCTION(WindowsLib, SetWindowProxyIcon, C_SetWindowProxyIcon);


OSStatus C_RemoveWindowProxy(WindowRef window);LIBRARY_FUNCTION(WindowsLib, RemoveWindowProxy, C_RemoveWindowProxy);


OSStatus C_BeginWindowProxyDrag(WindowRef window, GUEST<DragReference> *outNewDrag, RgnHandle outDragOutlineRgn);LIBRARY_FUNCTION(WindowsLib, BeginWindowProxyDrag, C_BeginWindowProxyDrag);


OSStatus C_EndWindowProxyDrag(WindowRef window, DragReference theDrag);LIBRARY_FUNCTION(WindowsLib, EndWindowProxyDrag, C_EndWindowProxyDrag);


OSStatus C_TrackWindowProxyFromExistingDrag(WindowRef window, Point startPt, DragReference drag, RgnHandle inDragOutlineRgn);LIBRARY_FUNCTION(WindowsLib, TrackWindowProxyFromExistingDrag, C_TrackWindowProxyFromExistingDrag);


OSStatus C_TrackWindowProxyDrag(WindowRef window, Point startPt);LIBRARY_FUNCTION(WindowsLib, TrackWindowProxyDrag, C_TrackWindowProxyDrag);


Boolean C_IsWindowModified(WindowRef window);LIBRARY_FUNCTION(WindowsLib, IsWindowModified, C_IsWindowModified);


OSStatus C_SetWindowModified(WindowRef window, Boolean modified);LIBRARY_FUNCTION(WindowsLib, SetWindowModified, C_SetWindowModified);


Boolean C_IsWindowPathSelectClick(WindowRef window, const EventRecord *event);LIBRARY_FUNCTION(WindowsLib, IsWindowPathSelectClick, C_IsWindowPathSelectClick);


OSStatus C_WindowPathSelect(WindowRef window, MenuRef menu, GUEST<SInt32> *outMenuResult);LIBRARY_FUNCTION(WindowsLib, WindowPathSelect, C_WindowPathSelect);


OSStatus C_HiliteWindowFrameForDrag(WindowRef window, Boolean hilited);LIBRARY_FUNCTION(WindowsLib, HiliteWindowFrameForDrag, C_HiliteWindowFrameForDrag);


typedef UInt32 WindowTransitionEffect;

enum {kWindowZoomTransitionEffect = 1,
kWindowSheetTransitionEffect = 2,
kWindowSlideTransitionEffect = 3,
};

typedef UInt32 WindowTransitionAction;

enum {kWindowShowTransitionAction = 1,
kWindowHideTransitionAction = 2,
kWindowMoveTransitionAction = 3,
kWindowResizeTransitionAction = 4,
};

OSStatus C_TransitionWindow(WindowRef window, WindowTransitionEffect effect, WindowTransitionAction action, const Rect *rect);LIBRARY_FUNCTION(WindowsLib, TransitionWindow, C_TransitionWindow);


OSStatus C_GetWindowBounds(WindowRef window, WindowRegionCode regionCode, Rect *globalBounds);LIBRARY_FUNCTION(WindowsLib, GetWindowBounds, C_GetWindowBounds);


Boolean C_ResizeWindow(WindowRef window, Point startPoint, const Rect *sizeConstraints, Rect *newContentRect);LIBRARY_FUNCTION(WindowsLib, ResizeWindow, C_ResizeWindow);


OSStatus C_SetWindowBounds(WindowRef window, WindowRegionCode regionCode, const Rect *globalBounds);LIBRARY_FUNCTION(WindowsLib, SetWindowBounds, C_SetWindowBounds);


OSStatus C_RepositionWindow(WindowRef window, WindowRef parentWindow, WindowPositionMethod method);LIBRARY_FUNCTION(WindowsLib, RepositionWindow, C_RepositionWindow);


OSStatus C_MoveWindowStructure(WindowRef window, short hGlobal, short vGlobal);LIBRARY_FUNCTION(WindowsLib, MoveWindowStructure, C_MoveWindowStructure);


Boolean C_IsWindowInStandardState(WindowRef window, GUEST<Point> *idealSize, Rect *idealStandardState);LIBRARY_FUNCTION(WindowsLib, IsWindowInStandardState, C_IsWindowInStandardState);


OSStatus C_ZoomWindowIdeal(WindowRef window, WindowPartCode partCode, GUEST<Point> *ioIdealSize);LIBRARY_FUNCTION(WindowsLib, ZoomWindowIdeal, C_ZoomWindowIdeal);


OSStatus C_GetWindowIdealUserState(WindowRef window, Rect *userState);LIBRARY_FUNCTION(WindowsLib, GetWindowIdealUserState, C_GetWindowIdealUserState);


OSStatus C_SetWindowIdealUserState(WindowRef window, const Rect *userState);LIBRARY_FUNCTION(WindowsLib, SetWindowIdealUserState, C_SetWindowIdealUserState);


typedef UInt32 WindowConstrainOptions;

enum {kWindowConstrainMayResize = (1 << 0),
kWindowConstrainMoveRegardlessOfFit = (1 << 1),
kWindowConstrainAllowPartial = (1 << 2),
kWindowConstrainCalcOnly = (1 << 3),
kWindowConstrainStandardOptions = kWindowConstrainMoveRegardlessOfFit,
};

OSStatus C_GetWindowProperty(WindowRef window, PropertyCreator propertyCreator, PropertyTag propertyTag, UInt32 bufferSize, GUEST<UInt32> *actualSize, void *propertyBuffer);LIBRARY_FUNCTION(WindowsLib, GetWindowProperty, C_GetWindowProperty);


OSStatus C_GetWindowPropertySize(WindowRef window, PropertyCreator creator, PropertyTag tag, GUEST<UInt32> *size);LIBRARY_FUNCTION(WindowsLib, GetWindowPropertySize, C_GetWindowPropertySize);


OSStatus C_SetWindowProperty(WindowRef window, PropertyCreator propertyCreator, PropertyTag propertyTag, UInt32 propertySize, void *propertyBuffer);LIBRARY_FUNCTION(WindowsLib, SetWindowProperty, C_SetWindowProperty);


OSStatus C_RemoveWindowProperty(WindowRef window, PropertyCreator propertyCreator, PropertyTag propertyTag);LIBRARY_FUNCTION(WindowsLib, RemoveWindowProperty, C_RemoveWindowProperty);


enum {kWindowPropertyPersistent = 1,
};

Handle C_GetWindowDataHandle(WindowRef window);LIBRARY_FUNCTION(available, GetWindowDataHandle, C_GetWindowDataHandle);


void C_SetWindowDataHandle(WindowRef window, Handle data);LIBRARY_FUNCTION(available, SetWindowDataHandle, C_SetWindowDataHandle);


Boolean C_GetWindowZoomFlag(WindowRef window);LIBRARY_FUNCTION(available, GetWindowZoomFlag, C_GetWindowZoomFlag);


void C_GetWindowStructureRgn(WindowRef window, RgnHandle r);LIBRARY_FUNCTION(available, GetWindowStructureRgn, C_GetWindowStructureRgn);


void C_GetWindowContentRgn(WindowRef window, RgnHandle r);LIBRARY_FUNCTION(available, GetWindowContentRgn, C_GetWindowContentRgn);


void C_GetWindowUpdateRgn(WindowRef window, RgnHandle r);LIBRARY_FUNCTION(available, GetWindowUpdateRgn, C_GetWindowUpdateRgn);


SInt16 C_GetWindowTitleWidth(WindowRef window);LIBRARY_FUNCTION(available, GetWindowTitleWidth, C_GetWindowTitleWidth);


enum {kWindowGroupAttrSelectable = kWindowGroupAttrSelectAsLayer,
kWindowGroupAttrPositionFixed = kWindowGroupAttrMoveTogether,
kWindowGroupAttrZOrderFixed = kWindowGroupAttrLayerTogether,
};

OSStatus C_GetWindowSystemProperty(WindowRef window, PropertyTag tag, UInt32 bufferSize, GUEST<UInt32> *actualSize, void *buffer);LIBRARY_FUNCTION(WindowsLib, GetWindowSystemProperty, C_GetWindowSystemProperty);


OSStatus C_SetWindowSystemProperty(WindowRef window, PropertyTag tag, UInt32 size, const void *buffer);LIBRARY_FUNCTION(WindowsLib, SetWindowSystemProperty, C_SetWindowSystemProperty);


OSStatus C_RemoveWindowSystemProperty(WindowRef window, PropertyTag tag);LIBRARY_FUNCTION(WindowsLib, RemoveWindowSystemProperty, C_RemoveWindowSystemProperty);


Boolean C_WindowHasUnicodeTitle(WindowRef window);LIBRARY_FUNCTION(WindowsLib, WindowHasUnicodeTitle, C_WindowHasUnicodeTitle);


OSStatus C_GetWindowTitleLength(WindowRef window, GUEST<UInt32> *outLength);LIBRARY_FUNCTION(WindowsLib, GetWindowTitleLength, C_GetWindowTitleLength);


OSStatus C_GetWindowGadgetDropThreshold(WindowRef window, Boolean *outPassed);LIBRARY_FUNCTION(WindowsLib, GetWindowGadgetDropThreshold, C_GetWindowGadgetDropThreshold);


OSStatus C_EraseWindowRegion(WindowRef window, RgnHandle region);LIBRARY_FUNCTION(WindowsLib, EraseWindowRegion, C_EraseWindowRegion);


OSStatus C_PostDelayedWindowReshape(WindowRef window);LIBRARY_FUNCTION(WindowsLib, PostDelayedWindowReshape, C_PostDelayedWindowReshape);


OSStatus C_GetDesktopPixPat(GUEST<PixPatHandle> *outPixPat, StringPtr outName);LIBRARY_FUNCTION(WindowsLib, GetDesktopPixPat, C_GetDesktopPixPat);


OSStatus C_GetDesktopPattern(GUEST<Handle> *outPattern, StringPtr outName);LIBRARY_FUNCTION(WindowsLib, GetDesktopPattern, C_GetDesktopPattern);


OSStatus C_GetDesktopPictureForDevice(GDHandle device, FSSpec *outFile, GUEST<UInt32> *outPlacement);LIBRARY_FUNCTION(WindowsLib, GetDesktopPictureForDevice, C_GetDesktopPictureForDevice);


OSStatus C_GetPictureFromFile(FSSpec *file, Ptr outPicture);LIBRARY_FUNCTION(WindowsLib, GetPictureFromFile, C_GetPictureFromFile);


UInt32 C_GetWindowDoubleClickAction();LIBRARY_FUNCTION(WindowsLib, GetWindowDoubleClickAction, C_GetWindowDoubleClickAction);


OSStatus C_SetWindowDoubleClickAction(UInt32 action);LIBRARY_FUNCTION(WindowsLib, SetWindowDoubleClickAction, C_SetWindowDoubleClickAction);


OSStatus C_GetCurrentMonitorDesktopList(GUEST<Ptr> *outList);LIBRARY_FUNCTION(WindowsLib, GetCurrentMonitorDesktopList, C_GetCurrentMonitorDesktopList);


OSStatus C_RefreshMonitorDesktopList(Ptr list, GUEST<Ptr> *outList);LIBRARY_FUNCTION(WindowsLib, RefreshMonitorDesktopList, C_RefreshMonitorDesktopList);


OSStatus C_DisposeMonitorDesktopList(Ptr list);LIBRARY_FUNCTION(WindowsLib, DisposeMonitorDesktopList, C_DisposeMonitorDesktopList);


OSStatus C_SetCurrentMonitorDesktopList(Ptr list);LIBRARY_FUNCTION(WindowsLib, SetCurrentMonitorDesktopList, C_SetCurrentMonitorDesktopList);


OSStatus C_CountMonitorDesktopRecordsInList(Ptr list, GUEST<UInt32> *outCount);LIBRARY_FUNCTION(WindowsLib, CountMonitorDesktopRecordsInList, C_CountMonitorDesktopRecordsInList);


OSStatus C_GetMonitorDesktopRecordFromList(Ptr list, UInt32 index, Ptr outRecord);LIBRARY_FUNCTION(WindowsLib, GetMonitorDesktopRecordFromList, C_GetMonitorDesktopRecordFromList);


OSStatus C_GetMonitorDesktopAlignment(Ptr list, UInt32 index, GUEST<UInt32> *outAlignment);LIBRARY_FUNCTION(WindowsLib, GetMonitorDesktopAlignment, C_GetMonitorDesktopAlignment);


OSStatus C_SetMonitorDesktopAlignment(Ptr list, UInt32 index, UInt32 alignment);LIBRARY_FUNCTION(WindowsLib, SetMonitorDesktopAlignment, C_SetMonitorDesktopAlignment);


OSStatus C_MonitorDesktopHasPicture(Ptr list, UInt32 index, Boolean *outHasPicture);LIBRARY_FUNCTION(WindowsLib, MonitorDesktopHasPicture, C_MonitorDesktopHasPicture);


OSStatus C_SetMonitorDesktopToFile(Ptr list, UInt32 index, FSSpec *file, UInt32 alignment);LIBRARY_FUNCTION(WindowsLib, SetMonitorDesktopToFile, C_SetMonitorDesktopToFile);


OSStatus C_CopyMonitorDesktopFile(Ptr fromList, UInt32 fromIndex, Ptr toList, UInt32 toIndex);LIBRARY_FUNCTION(WindowsLib, CopyMonitorDesktopFile, C_CopyMonitorDesktopFile);


OSStatus C_DrawMonitorDesktopFromListWillTakeTime(Ptr list, UInt32 index, GrafPtr port, const Rect *bounds, Boolean *outWillTakeTime);LIBRARY_FUNCTION(WindowsLib, DrawMonitorDesktopFromListWillTakeTime, C_DrawMonitorDesktopFromListWillTakeTime);


OSStatus C_DrawMonitorDesktopFromList(Ptr list, UInt32 index, GrafPtr port, const Rect *bounds, RgnHandle region, PixPatHandle pattern, Point origin, Boolean flag);LIBRARY_FUNCTION(WindowsLib, DrawMonitorDesktopFromList, C_DrawMonitorDesktopFromList);


OSStatus C_FileContainsPossibleDesktopPicture(FSSpec *file, Boolean *outIsPicture);LIBRARY_FUNCTION(WindowsLib, FileContainsPossibleDesktopPicture, C_FileContainsPossibleDesktopPicture);


OSStatus C_SetDesktopPattern(Handle ppat, ConstStringPtr name);LIBRARY_FUNCTION(WindowsLib, SetDesktopPattern, C_SetDesktopPattern);


OSStatus C_SetDesktopPictureForDevice(GDHandle device, FSSpec *file, UInt32 placement);LIBRARY_FUNCTION(WindowsLib, SetDesktopPictureForDevice, C_SetDesktopPictureForDevice);


void C_DrawDesktop();LIBRARY_FUNCTION(WindowsLib, DrawDesktop, C_DrawDesktop);


void C_RecalculateAllWindowTitleWidths();LIBRARY_FUNCTION(WindowsLib, RecalculateAllWindowTitleWidths, C_RecalculateAllWindowTitleWidths);


} /* namespace Executor */