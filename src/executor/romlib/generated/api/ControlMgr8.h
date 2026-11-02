/*****************************************************************************
 *                                                                           *
 *                               ControlMgr8.h                               *
 *                               =============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          Rect, SInt16, Boolean, UInt8, SInt32, Str255,     *
 *                         UInt32, Handle, UInt16, ResType, Size, Ptr,       *
 *                         ConstStr255Param, OSErr, OSType                   *
 *     Iconutil.h          CIconHandle                                       *
 *     IconServices.h      IconRef                                           *
 *     QuickDraw.h         PicHandle, RGBColor, RgnHandle                    *
 *     WindowMgr.h         DragConstraint, WindowRef                         *
 *     EventMgr.h          EventModifiers                                    *
 *     ControlMgr.h        ControlActionUPP, ControlPartCode, ControlRef     *
 *     DriverServices.h    OSStatus                                          *
 *     Collections.h       Collection                                        *
 *     FileMgr.h           FSSpec                                            *
 *     DragMgr.h           DragTrackingMessage, DragReference                *
 *     WindowMgr8.h        PropertyTag                                       *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "Iconutil.h"
#include "IconServices.h"
#include "QuickDraw.h"
#include "WindowMgr.h"
#include "EventMgr.h"
#include "ControlMgr.h"
#include "DriverServices.h"
#include "Collections.h"
#include "FileMgr.h"
#include "DragMgr.h"
#include "WindowMgr8.h"

#define MODULE_NAME ControlMgr8
#include <base/api-module.h>


namespace Executor {

DISPATCHER_TRAP(ControlDispatch, 0xAA73, D0W);


enum {controlPropertyInvalid = -5603,
controlPropertyNotFoundErr = -5604,
errMessageNotSupported = -30580,
errDataNotSupported = -30581,
errControlDoesntSupportFocus = -30582,
errUnknownControl = -30584,
errCouldntSetFocus = -30585,
errNoRootControl = -30586,
errRootAlreadyExists = -30587,
errInvalidPartCode = -30588,
errControlsAlreadyExist = -30589,
errControlIsNotEmbedder = -30590,
errDataSizeMismatch = -30591,
errControlHiddenOrDisabled = -30592,
errCantEmbedIntoSelf = -30594,
errCantEmbedRoot = -30595,
errItemNotControl = -30596,
controlHandleInvalidErr = -30599,
};

enum {kControlDefProcType = "CDEF"_4,
kControlTemplateResourceType = "CNTL"_4,
kControlColorTableResourceType = "cctb"_4,
kControlDefProcResourceType = "CDEF"_4,
};

struct ControlTemplate{GUEST_STRUCT;Rect controlRect;GUEST<SInt16> controlValue;Boolean controlVisible;UInt8 fill;GUEST<SInt16> controlMaximum;GUEST<SInt16> controlMinimum;GUEST<SInt16> controlDefProcID;GUEST<SInt32> controlReference;GUEST<Str255> controlTitle;};void describeStruct(const ControlTemplate&, std::ostream&);

typedef ControlTemplate *ControlTemplatePtr;

typedef GUEST<ControlTemplatePtr> *ControlTemplateHandle;

typedef UInt32 ControlNotification;

enum {controlNotifyNothing = "nada"_4,
controlNotifyClick = "clik"_4,
controlNotifyFocus = "focu"_4,
controlNotifyKey = "key "_4,
};

typedef UInt32 ControlCapabilities;

enum {kControlCanAutoInvalidate = 1 << 0,
};

enum {staticTextProc = 256,
editTextProc = 272,
iconProc = 288,
userItemProc = 304,
pictItemProc = 320,
};

enum {kNumberCtlCTabEntries = 4,
};

typedef SInt16 ControlVariant;

enum {kControlNoVariant = 0,
kControlUsesOwningWindowsFontVariant = 1 << 3,
};

enum {kControlNoPart = 0,
kControlIndicatorPart = 129,
kControlDisabledPart = 254,
kControlInactivePart = 255,
};

enum {kControlEntireControl = 0,
};

enum {kControlStructureMetaPart = -1,
kControlContentMetaPart = -2,
};

enum {kControlFocusNoPart = 0,
kControlFocusNextPart = -1,
kControlFocusPrevPart = -2,
};

typedef SInt16 ControlFocusPart;

enum {kControlCollectionTagBounds = "boun"_4,
kControlCollectionTagValue = "valu"_4,
kControlCollectionTagMinimum = "min "_4,
kControlCollectionTagMaximum = "max "_4,
kControlCollectionTagViewSize = "view"_4,
kControlCollectionTagVisibility = "visi"_4,
kControlCollectionTagRefCon = "refc"_4,
kControlCollectionTagTitle = "titl"_4,
kControlCollectionTagUnicodeTitle = "uttl"_4,
kControlCollectionTagIDSignature = "idsi"_4,
kControlCollectionTagIDID = "idid"_4,
kControlCollectionTagCommand = "cmd "_4,
kControlCollectionTagVarCode = "varc"_4,
};

enum {kControlCollectionTagSubControls = "subc"_4,
};

enum {kControlContentTextOnly = 0,
kControlNoContent = 0,
kControlContentIconSuiteRes = 1,
kControlContentCIconRes = 2,
kControlContentPictRes = 3,
kControlContentICONRes = 4,
kControlContentIconSuiteHandle = 129,
kControlContentCIconHandle = 130,
kControlContentPictHandle = 131,
kControlContentIconRef = 132,
kControlContentICON = 133,
};

typedef SInt16 ControlContentType;

union ControlButtonContentInfoUnion{GUEST_STRUCT;GUEST<SInt16> resID;GUEST<CIconHandle> cIconHandle;GUEST<Handle> iconSuite;GUEST<IconRef> iconRef;GUEST<PicHandle> picture;GUEST<Handle> ICONHandle;};void describeStruct(const ControlButtonContentInfoUnion&, std::ostream&);

struct ControlButtonContentInfo{GUEST_STRUCT;GUEST<ControlContentType> contentType;ControlButtonContentInfoUnion u;};void describeStruct(const ControlButtonContentInfo&, std::ostream&);

typedef ControlButtonContentInfo *ControlButtonContentInfoPtr;

typedef ControlButtonContentInfo ControlImageContentInfo;

typedef ControlButtonContentInfo *ControlImageContentInfoPtr;

enum {kControlKeyScriptBehaviorAllowAnyScript = "any "_4,
kControlKeyScriptBehaviorPrefersRoman = "prmn"_4,
kControlKeyScriptBehaviorRequiresRoman = "rrmn"_4,
};

typedef UInt32 ControlKeyScriptBehavior;

enum {kControlFontBigSystemFont = -1,
kControlFontSmallSystemFont = -2,
kControlFontSmallBoldSystemFont = -3,
kControlFontViewSystemFont = -4,
};

enum {kControlUseFontMask = 1,
kControlUseFaceMask = 2,
kControlUseSizeMask = 4,
kControlUseForeColorMask = 8,
kControlUseBackColorMask = 16,
kControlUseModeMask = 32,
kControlUseJustMask = 64,
kControlUseAllMask = 255,
kControlAddFontSizeMask = 256,
};

enum {kControlAddToMetaFontMask = 512,
};

enum {kControlUseThemeFontIDMask = 128,
};

struct ControlFontStyleRec{GUEST_STRUCT;GUEST<SInt16> flags;GUEST<SInt16> font;GUEST<SInt16> size;GUEST<SInt16> style;GUEST<SInt16> mode;GUEST<SInt16> just;RGBColor foreColor;RGBColor backColor;};void describeStruct(const ControlFontStyleRec&, std::ostream&);

typedef ControlFontStyleRec *ControlFontStylePtr;

enum {kDoNotActivateAndIgnoreClick = 0,
kDoNotActivateAndHandleClick = 1,
kActivateAndIgnoreClick = 2,
kActivateAndHandleClick = 3,
};

typedef UInt32 ClickActivationResult;

enum {kControlFontStyleTag = "font"_4,
kControlKeyFilterTag = "fltr"_4,
kControlKindTag = "kind"_4,
kControlSizeTag = "size"_4,
};

enum {kControlSupportsGhosting = 1 << 0,
kControlSupportsEmbedding = 1 << 1,
kControlSupportsFocus = 1 << 2,
kControlWantsIdle = 1 << 3,
kControlWantsActivate = 1 << 4,
kControlHandlesTracking = 1 << 5,
kControlSupportsDataAccess = 1 << 6,
kControlHasSpecialBackground = 1 << 7,
kControlGetsFocusOnClick = 1 << 8,
kControlSupportsCalcBestRect = 1 << 9,
kControlSupportsLiveFeedback = 1 << 10,
kControlHasRadioBehavior = 1 << 11,
kControlSupportsDragAndDrop = 1 << 12,
kControlAutoToggles = 1 << 14,
kControlSupportsGetRegion = 1 << 17,
kControlSupportsFlattening = 1 << 19,
kControlSupportsSetCursor = 1 << 20,
kControlSupportsContextualMenus = 1 << 21,
kControlSupportsClickActivation = 1 << 22,
kControlIdlesWithTimer = 1 << 23,
};

enum {drawThumbOutline = 12,
kControlMsgDrawGhost = 13,
kControlMsgCalcBestRect = 14,
kControlMsgHandleTracking = 15,
kControlMsgFocus = 16,
kControlMsgKeyDown = 17,
kControlMsgIdle = 18,
kControlMsgGetFeatures = 19,
kControlMsgSetData = 20,
kControlMsgGetData = 21,
kControlMsgActivate = 22,
kControlMsgSetUpBackground = 23,
kControlMsgCalcValueFromPos = 26,
kControlMsgTestNewMsgSupport = 27,
kControlMsgSubValueChanged = 25,
kControlMsgSubControlAdded = 28,
kControlMsgSubControlRemoved = 29,
kControlMsgApplyTextColor = 30,
kControlMsgGetRegion = 31,
kControlMsgFlatten = 32,
kControlMsgSetCursor = 33,
kControlMsgDragEnter = 38,
kControlMsgDragLeave = 39,
kControlMsgDragWithin = 40,
kControlMsgDragReceive = 41,
kControlMsgDisplayDebugInfo = 46,
kControlMsgContextualMenuClick = 47,
kControlMsgGetClickActivation = 48,
};

typedef SInt16 ControlDefProcMessage;

enum {kControlSizeNormal = 0,
kControlSizeSmall = 1,
kControlSizeLarge = 2,
kControlSizeAuto = 65535,
};

typedef UInt16 ControlSize;

enum {kDrawControlEntireControl = 0,
kDrawControlIndicatorOnly = 129,
};

enum {kDragControlEntireControl = 0,
kDragControlIndicator = 1,
};

struct IndicatorDragConstraint{GUEST_STRUCT;Rect limitRect;Rect slopRect;GUEST<DragConstraint> axis;};void describeStruct(const IndicatorDragConstraint&, std::ostream&);

typedef IndicatorDragConstraint *IndicatorDragConstraintPtr;

enum {kControlSupportsNewMessages = " ok "_4,
};

struct ControlTrackingRec{GUEST_STRUCT;GUEST<Point> startPt;GUEST<EventModifiers> modifiers;GUEST<ControlActionUPP> action;};void describeStruct(const ControlTrackingRec&, std::ostream&);

typedef ControlTrackingRec *ControlTrackingPtr;

struct ControlKeyDownRec{GUEST_STRUCT;GUEST<EventModifiers> modifiers;GUEST<SInt16> keyCode;GUEST<SInt16> charCode;};void describeStruct(const ControlKeyDownRec&, std::ostream&);

typedef ControlKeyDownRec *ControlKeyDownPtr;

struct ControlDataAccessRec{GUEST_STRUCT;GUEST<ResType> tag;GUEST<ResType> part;GUEST<Size> size;GUEST<Ptr> dataPtr;};void describeStruct(const ControlDataAccessRec&, std::ostream&);

typedef ControlDataAccessRec *ControlDataAccessPtr;

struct ControlCalcSizeRec{GUEST_STRUCT;GUEST<SInt16> height;GUEST<SInt16> width;GUEST<SInt16> baseLine;};void describeStruct(const ControlCalcSizeRec&, std::ostream&);

typedef ControlCalcSizeRec *ControlCalcSizePtr;

struct ControlBackgroundRec{GUEST_STRUCT;GUEST<SInt16> depth;Boolean colorDevice;};void describeStruct(const ControlBackgroundRec&, std::ostream&);

typedef ControlBackgroundRec *ControlBackgroundPtr;

struct ControlApplyTextColorRec{GUEST_STRUCT;GUEST<SInt16> depth;Boolean colorDevice;Boolean active;};void describeStruct(const ControlApplyTextColorRec&, std::ostream&);

typedef ControlApplyTextColorRec *ControlApplyTextColorPtr;

struct ControlGetRegionRec{GUEST_STRUCT;GUEST<RgnHandle> region;GUEST<ControlPartCode> part;};void describeStruct(const ControlGetRegionRec&, std::ostream&);

typedef ControlGetRegionRec *ControlGetRegionPtr;

struct ControlSetCursorRec{GUEST_STRUCT;GUEST<Point> localPoint;GUEST<EventModifiers> modifiers;Boolean cursorWasSet;};void describeStruct(const ControlSetCursorRec&, std::ostream&);

typedef ControlSetCursorRec *ControlSetCursorPtr;

struct ControlContextualMenuClickRec{GUEST_STRUCT;GUEST<Point> localPoint;Boolean menuDisplayed;};void describeStruct(const ControlContextualMenuClickRec&, std::ostream&);

typedef ControlContextualMenuClickRec *ControlContextualMenuClickPtr;

struct ControlClickActivationRec{GUEST_STRUCT;GUEST<Point> localPoint;GUEST<EventModifiers> modifiers;GUEST<ClickActivationResult> result;};void describeStruct(const ControlClickActivationRec&, std::ostream&);

typedef ControlClickActivationRec *ControlClickActivationPtr;

enum {kControlKeyFilterBlockKey = 0,
kControlKeyFilterPassKey = 1,
};

typedef SInt16 ControlKeyFilterResult;

using ControlKeyFilterUPP = UPP<ControlKeyFilterResult(ControlRef theControl, GUEST<SInt16> *keyCode, GUEST<SInt16> *charCode, GUEST<EventModifiers> *modifiers)>;


enum {kControlDefProcPtr = 0,
kControlDefObjectClass = 1,
};

typedef UInt32 ControlDefType;

using ControlCNTLToCollectionUPP = UPP<OSStatus(const Rect *bounds, SInt16 value, Boolean visible, SInt16 max, SInt16 min, SInt16 procID, SInt32 refCon, ConstStr255Param title, Collection collection)>;


Boolean C_IsControlActive(ControlRef inControl);PASCAL_SUBTRAP(IsControlActive, 0xAA73, 0x0005, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, IsControlActive, InterfaceLib);


Boolean C_IsControlVisible(ControlRef inControl);PASCAL_SUBTRAP(IsControlVisible, 0xAA73, 0x0006, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, IsControlVisible, InterfaceLib);


OSErr C_ActivateControl(ControlRef inControl);PASCAL_SUBTRAP(ActivateControl, 0xAA73, 0x0007, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, ActivateControl, InterfaceLib);


OSErr C_DeactivateControl(ControlRef inControl);PASCAL_SUBTRAP(DeactivateControl, 0xAA73, 0x0008, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, DeactivateControl, InterfaceLib);


OSErr C_SetControlVisibility(ControlRef inControl, Boolean inIsVisible, Boolean inDoDraw);PASCAL_SUBTRAP(SetControlVisibility, 0xAA73, 0x001E, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, SetControlVisibility, InterfaceLib);


OSErr C_GetBestControlRect(ControlRef inControl, Rect *outRect, GUEST<SInt16> *outBaseLineOffset);PASCAL_SUBTRAP(GetBestControlRect, 0xAA73, 0x001B, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetBestControlRect, InterfaceLib);


OSErr C_SetControlFontStyle(ControlRef inControl, const ControlFontStyleRec *inStyle);PASCAL_SUBTRAP(SetControlFontStyle, 0xAA73, 0x001C, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, SetControlFontStyle, InterfaceLib);


void C_DrawControlInCurrentPort(ControlRef inControl);PASCAL_SUBTRAP(DrawControlInCurrentPort, 0xAA73, 0x0018, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, DrawControlInCurrentPort, InterfaceLib);


OSErr C_SetUpControlBackground(ControlRef inControl, SInt16 inDepth, Boolean inIsColorDevice);PASCAL_SUBTRAP(SetUpControlBackground, 0xAA73, 0x001D, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, SetUpControlBackground, InterfaceLib);


OSErr C_SetUpControlTextColor(ControlRef inControl, SInt16 inDepth, Boolean inIsColorDevice);LIBRARY_FUNCTION(ControlsLib, SetUpControlTextColor, C_SetUpControlTextColor);


using ControlColorUPP = UPP<OSStatus(ControlRef inControl, SInt16 inMessage, SInt16 inDrawDepth, Boolean inDrawInColor)>;




ControlRef C_FindControlUnderMouse(Point inWhere, WindowRef inWindow, GUEST<SInt16> *outPart);PASCAL_SUBTRAP(FindControlUnderMouse, 0xAA73, 0x0009, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, FindControlUnderMouse, InterfaceLib);


ControlPartCode C_HandleControlClick(ControlRef inControl, Point inWhere, EventModifiers inModifiers, ControlActionUPP inAction);PASCAL_SUBTRAP(HandleControlClick, 0xAA73, 0x000A, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, HandleControlClick, InterfaceLib);






ControlPartCode C_HandleControlKey(ControlRef inControl, SInt16 inKeyCode, SInt16 inCharCode, EventModifiers inModifiers);PASCAL_SUBTRAP(HandleControlKey, 0xAA73, 0x000B, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, HandleControlKey, InterfaceLib);


void C_IdleControls(WindowRef inWindow);PASCAL_SUBTRAP(IdleControls, 0xAA73, 0x000C, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, IdleControls, InterfaceLib);




SInt32 C_GetControlViewSize(ControlRef theControl);LIBRARY_FUNCTION(ControlsLib, GetControlViewSize, C_GetControlViewSize);


void C_SetControlViewSize(ControlRef theControl, SInt32 newViewSize);LIBRARY_FUNCTION(ControlsLib, SetControlViewSize, C_SetControlViewSize);


SInt32 C_GetControl32BitValue(ControlRef theControl);LIBRARY_FUNCTION(ControlsLib, GetControl32BitValue, C_GetControl32BitValue);


void C_SetControl32BitValue(ControlRef theControl, SInt32 newValue);LIBRARY_FUNCTION(ControlsLib, SetControl32BitValue, C_SetControl32BitValue);


SInt32 C_GetControl32BitMaximum(ControlRef theControl);LIBRARY_FUNCTION(ControlsLib, GetControl32BitMaximum, C_GetControl32BitMaximum);


void C_SetControl32BitMaximum(ControlRef theControl, SInt32 newMaximum);LIBRARY_FUNCTION(ControlsLib, SetControl32BitMaximum, C_SetControl32BitMaximum);


SInt32 C_GetControl32BitMinimum(ControlRef theControl);LIBRARY_FUNCTION(ControlsLib, GetControl32BitMinimum, C_GetControl32BitMinimum);


void C_SetControl32BitMinimum(ControlRef theControl, SInt32 newMinimum);LIBRARY_FUNCTION(ControlsLib, SetControl32BitMinimum, C_SetControl32BitMinimum);


Boolean C_IsValidControlHandle(ControlRef theControl);LIBRARY_FUNCTION(ControlsLib, IsValidControlHandle, C_IsValidControlHandle);


struct ControlID{GUEST_STRUCT;GUEST<OSType> signature;GUEST<SInt32> id;};void describeStruct(const ControlID&, std::ostream&);

struct ControlKind{GUEST_STRUCT;GUEST<OSType> signature;GUEST<OSType> kind;};void describeStruct(const ControlKind&, std::ostream&);

enum {kControlKindSignatureApple = "appl"_4,
};

enum {kControlPropertyPersistent = 1,
};

OSStatus C_GetControlProperty(ControlRef control, OSType propertyCreator, OSType propertyTag, UInt32 bufferSize, GUEST<UInt32> *actualSize, void *propertyBuffer);LIBRARY_FUNCTION(ControlsLib, GetControlProperty, C_GetControlProperty);


OSStatus C_GetControlPropertySize(ControlRef control, OSType propertyCreator, OSType propertyTag, GUEST<UInt32> *size);LIBRARY_FUNCTION(ControlsLib, GetControlPropertySize, C_GetControlPropertySize);


OSStatus C_SetControlProperty(ControlRef control, OSType propertyCreator, OSType propertyTag, UInt32 propertySize, void *propertyData);LIBRARY_FUNCTION(ControlsLib, SetControlProperty, C_SetControlProperty);


OSStatus C_RemoveControlProperty(ControlRef control, OSType propertyCreator, OSType propertyTag);LIBRARY_FUNCTION(ControlsLib, RemoveControlProperty, C_RemoveControlProperty);


OSStatus C_GetControlRegion(ControlRef inControl, ControlPartCode inPart, RgnHandle outRegion);LIBRARY_FUNCTION(ControlsLib, GetControlRegion, C_GetControlRegion);


SInt32 C_SendControlMessage(ControlRef inControl, SInt16 inMessage, void *inParam);PASCAL_SUBTRAP(SendControlMessage, 0xAA73, 0xFFFE, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, SendControlMessage, InterfaceLib);


OSErr DumpControlHierarchy(WindowRef inWindow, const FSSpec *inDumpFile);TRAP_GLUE(AppearanceLib, DumpControlHierarchy, ControlDispatch, 0xFFFF);


OSErr C_CreateRootControl(WindowRef inWindow, GUEST<ControlRef> *outControl);PASCAL_SUBTRAP(CreateRootControl, 0xAA73, 0x0001, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, CreateRootControl, InterfaceLib);


OSErr C_GetRootControl(WindowRef inWindow, GUEST<ControlRef> *outControl);PASCAL_SUBTRAP(GetRootControl, 0xAA73, 0x0002, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetRootControl, InterfaceLib);


OSErr C_EmbedControl(ControlRef inControl, ControlRef inContainer);PASCAL_SUBTRAP(EmbedControl, 0xAA73, 0x0003, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, EmbedControl, InterfaceLib);


OSErr C_AutoEmbedControl(ControlRef inControl, WindowRef inWindow);PASCAL_SUBTRAP(AutoEmbedControl, 0xAA73, 0x0004, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, AutoEmbedControl, InterfaceLib);


OSErr C_GetSuperControl(ControlRef inControl, GUEST<ControlRef> *outParent);PASCAL_SUBTRAP(GetSuperControl, 0xAA73, 0x0015, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetSuperControl, InterfaceLib);


OSErr C_CountSubControls(ControlRef inControl, GUEST<UInt16> *outNumChildren);PASCAL_SUBTRAP(CountSubControls, 0xAA73, 0x0016, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, CountSubControls, InterfaceLib);


OSErr C_GetIndexedSubControl(ControlRef inControl, UInt16 inIndex, GUEST<ControlRef> *outSubControl);PASCAL_SUBTRAP(GetIndexedSubControl, 0xAA73, 0x0017, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetIndexedSubControl, InterfaceLib);


OSErr C_SetControlSupervisor(ControlRef inControl, ControlRef inBoss);PASCAL_SUBTRAP(SetControlSupervisor, 0xAA73, 0x001A, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, SetControlSupervisor, InterfaceLib);


OSErr C_GetKeyboardFocus(WindowRef inWindow, GUEST<ControlRef> *outControl);PASCAL_SUBTRAP(GetKeyboardFocus, 0xAA73, 0x000D, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetKeyboardFocus, InterfaceLib);


OSErr C_SetKeyboardFocus(WindowRef inWindow, ControlRef inControl, ControlFocusPart inPart);PASCAL_SUBTRAP(SetKeyboardFocus, 0xAA73, 0x000E, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, SetKeyboardFocus, InterfaceLib);


OSErr C_AdvanceKeyboardFocus(WindowRef inWindow);PASCAL_SUBTRAP(AdvanceKeyboardFocus, 0xAA73, 0x000F, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, AdvanceKeyboardFocus, InterfaceLib);


OSErr C_ReverseKeyboardFocus(WindowRef inWindow);PASCAL_SUBTRAP(ReverseKeyboardFocus, 0xAA73, 0x0010, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, ReverseKeyboardFocus, InterfaceLib);


OSErr C_ClearKeyboardFocus(WindowRef inWindow);PASCAL_SUBTRAP(ClearKeyboardFocus, 0xAA73, 0x0019, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, ClearKeyboardFocus, InterfaceLib);


OSErr C_GetControlFeatures(ControlRef inControl, GUEST<UInt32> *outFeatures);PASCAL_SUBTRAP(GetControlFeatures, 0xAA73, 0x0011, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetControlFeatures, InterfaceLib);


OSErr C_SetControlData(ControlRef inControl, ControlPartCode inPart, ResType inTagName, Size inSize, const void *inData);PASCAL_SUBTRAP(SetControlData, 0xAA73, 0x0012, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, SetControlData, InterfaceLib);


OSErr C_GetControlData(ControlRef inControl, ControlPartCode inPart, ResType inTagName, Size inBufferSize, void *inBuffer, GUEST<Size> *outActualSize);PASCAL_SUBTRAP(GetControlData, 0xAA73, 0x0013, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetControlData, InterfaceLib);


OSErr C_GetControlDataSize(ControlRef inControl, ControlPartCode inPart, ResType inTagName, GUEST<Size> *outMaxSize);PASCAL_SUBTRAP(GetControlDataSize, 0xAA73, 0x0014, ControlDispatch);
LIBRARY_ALSO(AppearanceLib, GetControlDataSize, InterfaceLib);


enum {kDragTrackingEnterControl = 2,
kDragTrackingInControl = 3,
kDragTrackingLeaveControl = 4,
};













enum {kNoHiliteControlPart = kControlNoPart,
kInIndicatorControlPart = kControlIndicatorPart,
kReservedControlPart = kControlDisabledPart,
kControlInactiveControlPart = kControlInactivePart,
};

enum {kControlBevelButtonContentTag = "cont"_4,
};

OSErr C_SetBevelButtonContentInfo(ControlRef inButton, ControlButtonContentInfoPtr inContent);LIBRARY_FUNCTION(AppearanceLib, SetBevelButtonContentInfo, C_SetBevelButtonContentInfo);


OSStatus C_GetControlSystemProperty(ControlRef control, PropertyTag tag, UInt32 bufferSize, GUEST<UInt32> *actualSize, void *buffer);LIBRARY_FUNCTION(ControlsLib, GetControlSystemProperty, C_GetControlSystemProperty);


OSStatus C_SetControlSystemProperty(ControlRef control, PropertyTag tag, UInt32 size, const void *buffer);LIBRARY_FUNCTION(ControlsLib, SetControlSystemProperty, C_SetControlSystemProperty);


} /* namespace Executor */