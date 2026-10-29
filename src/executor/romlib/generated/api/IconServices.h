/*****************************************************************************
 *                                                                           *
 *                               IconServices.h                              *
 *                               ==============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h         OSType, LONGINT, OSErr, Handle, INTEGER,           *
 *                        SignedByte, Boolean, Rect                          *
 *     CodeFragments.h    InitBlock                                          *
 *     Iconutil.h         IconSelectorValue, IconAlignmentType,              *
 *                        IconTransformType                                  *
 *     FileMgr.h          FSSpec                                             *
 *     QuickDraw.h        RgnHandle                                          *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "CodeFragments.h"
#include "Iconutil.h"
#include "FileMgr.h"
#include "QuickDraw.h"

#define MODULE_NAME IconServices
#include <base/api-module.h>


namespace Executor {

// The selector in D0 (TWOWORDINLINE(0x70nn, 0xAA75): moveq #nn, D0),
// Pascal arguments on the stack.
DISPATCHER_TRAP(IconServicesDispatch, 0xAA75, D0<0xFF>);


struct OpaqueIconRef;

typedef OpaqueIconRef *IconRef;

typedef uint32_t IconServicesUsageFlags;

struct IconFamilyElement{GUEST_STRUCT;GUEST<OSType> elementType;GUEST<LONGINT> elementSize;uint8_t elementData[1];};void describeStruct(const IconFamilyElement&, std::ostream&);

struct IconFamilyResource{GUEST_STRUCT;GUEST<OSType> resourceType;GUEST<LONGINT> resourceSize;IconFamilyElement elements[1];};void describeStruct(const IconFamilyResource&, std::ostream&);

typedef IconFamilyResource *IconFamilyPtr;

typedef GUEST<IconFamilyPtr> *IconFamilyHandle;

enum {kSystemIconsCreator = "macs"_4,
kClipboardIcon = "CLIP"_4,
kClippingUnknownTypeIcon = "clpu"_4,
kClippingPictureTypeIcon = "clpp"_4,
kClippingTextTypeIcon = "clpt"_4,
kClippingSoundTypeIcon = "clps"_4,
kDesktopIcon = "desk"_4,
kFinderIcon = "FNDR"_4,
kFontSuitcaseIcon = "FFIL"_4,
kFullTrashIcon = "ftrh"_4,
kGenericApplicationIcon = "APPL"_4,
kGenericCDROMIcon = "cddr"_4,
kGenericControlPanelIcon = "APPC"_4,
kGenericControlStripModuleIcon = "sdev"_4,
kGenericComponentIcon = "thng"_4,
kGenericDeskAccessoryIcon = "APPD"_4,
kGenericDocumentIcon = "docu"_4,
kGenericEditionFileIcon = "edtf"_4,
kGenericExtensionIcon = "INIT"_4,
kGenericFileServerIcon = "srvr"_4,
kGenericFontIcon = "ffil"_4,
kGenericFontScalerIcon = "sclr"_4,
kGenericFloppyIcon = "flpy"_4,
kGenericHardDiskIcon = "hdsk"_4,
kGenericIDiskIcon = "idsk"_4,
kGenericRemovableMediaIcon = "rmov"_4,
kGenericMoverObjectIcon = "movr"_4,
kGenericPCCardIcon = "pcmc"_4,
kGenericPreferencesIcon = "pref"_4,
kGenericQueryDocumentIcon = "qery"_4,
kGenericRAMDiskIcon = "ramd"_4,
kGenericSharedLibaryIcon = "shlb"_4,
kGenericStationeryIcon = "sdoc"_4,
kGenericSuitcaseIcon = "suit"_4,
kGenericWORMIcon = "worm"_4,
kInternationResourcesIcon = "ifil"_4,
kKeyboardLayoutIcon = "kfil"_4,
kSoundFileIcon = "sfil"_4,
kSystemSuitcaseIcon = "zsys"_4,
kTrashIcon = "trsh"_4,
kTrueTypeFontIcon = "tfil"_4,
kTrueTypeFlatFontIcon = "sfnt"_4,
kTrueTypeMultiFlatFontIcon = "ttcf"_4,
kUserIDiskIcon = "udsk"_4,
kInternetLocationHTTPIcon = "ilht"_4,
kInternetLocationFTPIcon = "ilft"_4,
kInternetLocationAppleShareIcon = "ilaf"_4,
kInternetLocationAppleTalkZoneIcon = "ilat"_4,
kInternetLocationFileIcon = "ilfi"_4,
kInternetLocationMailIcon = "ilma"_4,
kInternetLocationNewsIcon = "ilnw"_4,
kInternetLocationNSLNeighborhoodIcon = "ilns"_4,
kInternetLocationGenericIcon = "ilge"_4,
kGenericFolderIcon = "fldr"_4,
kDropFolderIcon = "dbox"_4,
kMountedFolderIcon = "mntd"_4,
kOpenFolderIcon = "ofld"_4,
kOwnedFolderIcon = "ownd"_4,
kPrivateFolderIcon = "prvf"_4,
kSharedFolderIcon = "shfl"_4,
kSharingPrivsNotApplicableIcon = "shna"_4,
kSharingPrivsReadOnlyIcon = "shro"_4,
kSharingPrivsReadWriteIcon = "shrw"_4,
kSharingPrivsUnknownIcon = "shuk"_4,
kSharingPrivsWritableIcon = "writ"_4,
kUserFolderIcon = "ufld"_4,
kWorkgroupFolderIcon = "wfld"_4,
kGuestUserIcon = "gusr"_4,
kUserIcon = "user"_4,
kOwnerIcon = "susr"_4,
kGroupIcon = "grup"_4,
kAppleExtrasFolderIcon = "aexƒ"_4,
kAppleMenuFolderIcon = "amnu"_4,
kApplicationsFolderIcon = "apps"_4,
kApplicationSupportFolderIcon = "asup"_4,
kAssistantsFolderIcon = "astƒ"_4,
kContextualMenuItemsFolderIcon = "cmnu"_4,
kControlPanelDisabledFolderIcon = "ctrD"_4,
kControlPanelFolderIcon = "ctrl"_4,
kControlStripModulesFolderIcon = "sdvƒ"_4,
kDocumentsFolderIcon = "docs"_4,
kExtensionsDisabledFolderIcon = "extD"_4,
kExtensionsFolderIcon = "extn"_4,
kFavoritesFolderIcon = "favs"_4,
kFontsFolderIcon = "font"_4,
kHelpFolderIcon = "ƒhlp"_4,
kInternetFolderIcon = "intƒ"_4,
kInternetPlugInFolderIcon = "ƒnet"_4,
kLocalesFolderIcon = "ƒloc"_4,
kMacOSReadMeFolderIcon = "morƒ"_4,
kPreferencesFolderIcon = "prfƒ"_4,
kPrinterDescriptionFolderIcon = "ppdf"_4,
kPrinterDriverFolderIcon = "ƒprd"_4,
kPrintMonitorFolderIcon = "prnt"_4,
kRecentApplicationsFolderIcon = "rapp"_4,
kRecentDocumentsFolderIcon = "rdoc"_4,
kRecentServersFolderIcon = "rsrv"_4,
kScriptingAdditionsFolderIcon = "ƒscr"_4,
kSharedLibrariesFolderIcon = "ƒlib"_4,
kScriptsFolderIcon = "scrƒ"_4,
kShutdownItemsDisabledFolderIcon = "shdD"_4,
kShutdownItemsFolderIcon = "shdf"_4,
kSpeakableItemsFolder = "spki"_4,
kStartupItemsDisabledFolderIcon = "strD"_4,
kStartupItemsFolderIcon = "strt"_4,
kSystemExtensionDisabledFolderIcon = "macD"_4,
kSystemFolderIcon = "macs"_4,
kTextEncodingsFolderIcon = "ƒtex"_4,
kAppearanceFolderIcon = "appr"_4,
kUtilitiesFolderIcon = "utiƒ"_4,
kVoicesFolderIcon = "fvoc"_4,
kColorSyncFolderIcon = "prof"_4,
kInternetSearchSitesFolderIcon = "issf"_4,
kUsersFolderIcon = "usrƒ"_4,
kAppleScriptBadgeIcon = "scrp"_4,
kLockedBadgeIcon = "lbdg"_4,
kMountedBadgeIcon = "mbdg"_4,
kSharedBadgeIcon = "sbdg"_4,
kAliasBadgeIcon = "abdg"_4,
kAlertNoteIcon = "note"_4,
kAlertCautionIcon = "caut"_4,
kAlertStopIcon = "stop"_4,
kAppleTalkIcon = "atlk"_4,
kAppleTalkZoneIcon = "atzn"_4,
kAFPServerIcon = "afps"_4,
kFTPServerIcon = "ftps"_4,
kHTTPServerIcon = "htps"_4,
kGenericNetworkIcon = "gnet"_4,
kIPFileServerIcon = "isrv"_4,
kAppleLogoIcon = "capl"_4,
kAppleMenuIcon = "sapl"_4,
kBackwardArrowIcon = "baro"_4,
kFavoriteItemsIcon = "favr"_4,
kForwardArrowIcon = "faro"_4,
kGridIcon = "grid"_4,
kHelpIcon = "help"_4,
kKeepArrangedIcon = "arng"_4,
kLockedIcon = "lock"_4,
kNoFilesIcon = "nfil"_4,
kNoFolderIcon = "nfld"_4,
kNoWriteIcon = "nwrt"_4,
kProtectedApplicationFolderIcon = "papp"_4,
kProtectedSystemFolderIcon = "psys"_4,
kRecentItemsIcon = "rcnt"_4,
kShortcutIcon = "shrt"_4,
kSortAscendingIcon = "asnd"_4,
kSortDescendingIcon = "dsnd"_4,
kUnlockedIcon = "ulck"_4,
kConnectToIcon = "cnct"_4,
};

enum {kIconServicesNormalUsageFlag = 0,
};

enum {kIconFamilyType = "icns"_4,
};

enum {invalidIconRefErr = -2580,
noSuchIconErr = -2581,
noIconDataAvailableErr = -2582,
};

OSErr C_IconServicesInit(InitBlock *initBlockPtr);PASCAL_SUBTRAP(IconServicesInit, 0xAA75, 0x0015, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IconServicesInit, InterfaceLib);


void C_IconServicesTerminate();PASCAL_SUBTRAP(IconServicesTerminate, 0xAA75, 0x0016, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IconServicesTerminate, InterfaceLib);


OSErr C_IconRefToIconFamily(IconRef theIconRef, IconSelectorValue whichIcons, GUEST<IconFamilyHandle> *iconFamily);PASCAL_SUBTRAP(IconRefToIconFamily, 0xAA75, 0x0024, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IconRefToIconFamily, InterfaceLib);


OSErr C_IconFamilyToIconSuite(IconFamilyHandle iconFamily, IconSelectorValue whichIcons, GUEST<Handle> *iconSuite);PASCAL_SUBTRAP(IconFamilyToIconSuite, 0xAA75, 0x0025, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IconFamilyToIconSuite, InterfaceLib);


OSErr C_IconSuiteToIconFamily(Handle iconSuite, IconSelectorValue whichIcons, GUEST<IconFamilyHandle> *iconFamily);PASCAL_SUBTRAP(IconSuiteToIconFamily, 0xAA75, 0x0026, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IconSuiteToIconFamily, InterfaceLib);


OSErr C_SetIconFamilyData(IconFamilyHandle iconFamily, OSType iconType, Handle h);PASCAL_SUBTRAP(SetIconFamilyData, 0xAA75, 0x0030, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, SetIconFamilyData, InterfaceLib);


OSErr C_GetIconFamilyData(IconFamilyHandle iconFamily, OSType iconType, Handle h);PASCAL_SUBTRAP(GetIconFamilyData, 0xAA75, 0x0031, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconFamilyData, InterfaceLib);


OSErr C_GetIconRefOwners(IconRef theIconRef, GUEST<uint16_t> *owners);PASCAL_SUBTRAP(GetIconRefOwners, 0xAA75, 0x000B, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconRefOwners, InterfaceLib);


OSErr C_AcquireIconRef(IconRef theIconRef);PASCAL_SUBTRAP(AcquireIconRef, 0xAA75, 0x0027, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, AcquireIconRef, InterfaceLib);


OSErr C_ReleaseIconRef(IconRef theIconRef);PASCAL_SUBTRAP(ReleaseIconRef, 0xAA75, 0x0028, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, ReleaseIconRef, InterfaceLib);


OSErr C_GetIconRefFromFile(const FSSpec *theFile, GUEST<IconRef> *theIconRef, GUEST<INTEGER> *theLabel);PASCAL_SUBTRAP(GetIconRefFromFile, 0xAA75, 0x0002, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconRefFromFile, InterfaceLib);


OSErr C_GetIconRef(INTEGER vRefNum, OSType creator, OSType iconType, GUEST<IconRef> *theIconRef);PASCAL_SUBTRAP(GetIconRef, 0xAA75, 0x0021, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconRef, InterfaceLib);


OSErr C_GetIconRefFromFolder(INTEGER vRefNum, LONGINT parentFolderID, LONGINT folderID, SignedByte attributes, SignedByte accessPrivileges, GUEST<IconRef> *theIconRef);PASCAL_SUBTRAP(GetIconRefFromFolder, 0xAA75, 0x0022, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconRefFromFolder, InterfaceLib);


OSErr C_GetIconRefByName(OSType creator, OSType iconType, GUEST<IconRef> *theIconRef);PASCAL_SUBTRAP(GetIconRefByName, 0xAA75, 0x0006, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconRefByName, InterfaceLib);


OSErr C_RegisterIconRefFromIconFamily(OSType creator, OSType iconType, IconFamilyHandle iconFamily, GUEST<IconRef> *theIconRef);PASCAL_SUBTRAP(RegisterIconRefFromIconFamily, 0xAA75, 0x001C, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, RegisterIconRefFromIconFamily, InterfaceLib);


OSErr C_RegisterIconRefFromResource(OSType creator, OSType iconType, const FSSpec *resourceFile, INTEGER resourceID, GUEST<IconRef> *theIconRef);PASCAL_SUBTRAP(RegisterIconRefFromResource, 0xAA75, 0x0023, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, RegisterIconRefFromResource, InterfaceLib);


OSErr C_UnregisterIconRef(OSType creator, OSType iconType);PASCAL_SUBTRAP(UnregisterIconRef, 0xAA75, 0x0008, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, UnregisterIconRef, InterfaceLib);


OSErr C_UpdateIconRef(IconRef theIconRef);PASCAL_SUBTRAP(UpdateIconRef, 0xAA75, 0x0009, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, UpdateIconRef, InterfaceLib);


OSErr C_OverrideIconRefFromResource(IconRef theIconRef, const FSSpec *resourceFile, INTEGER resourceID);PASCAL_SUBTRAP(OverrideIconRefFromResource, 0xAA75, 0x002A, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, OverrideIconRefFromResource, InterfaceLib);


OSErr C_OverrideIconRef(IconRef oldIconRef, IconRef newIconRef);PASCAL_SUBTRAP(OverrideIconRef, 0xAA75, 0x002B, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, OverrideIconRef, InterfaceLib);


OSErr C_RemoveIconRefOverride(IconRef theIconRef);PASCAL_SUBTRAP(RemoveIconRefOverride, 0xAA75, 0x001E, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, RemoveIconRefOverride, InterfaceLib);


OSErr C_CompositeIconRef(IconRef backgroundIconRef, IconRef foregroundIconRef, GUEST<IconRef> *compositeIconRef);PASCAL_SUBTRAP(CompositeIconRef, 0xAA75, 0x0014, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, CompositeIconRef, InterfaceLib);


OSErr C_IsIconRefComposite(IconRef compositeIconRef, GUEST<IconRef> *backgroundIconRef, GUEST<IconRef> *foregroundIconRef);PASCAL_SUBTRAP(IsIconRefComposite, 0xAA75, 0x001A, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IsIconRefComposite, InterfaceLib);


Boolean C_IsValidIconRef(IconRef theIconRef);PASCAL_SUBTRAP(IsValidIconRef, 0xAA75, 0x0032, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IsValidIconRef, InterfaceLib);


OSErr C_PlotIconRef(const Rect *theRect, IconAlignmentType align, IconTransformType transform, IconServicesUsageFlags theIconServicesUsageFlags, IconRef theIconRef);PASCAL_SUBTRAP(PlotIconRef, 0xAA75, 0x000E, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, PlotIconRef, InterfaceLib);


Boolean C_PtInIconRef(const GUEST<Point> *testPt, const Rect *iconRect, IconAlignmentType align, IconServicesUsageFlags theIconServicesUsageFlags, IconRef theIconRef);PASCAL_SUBTRAP(PtInIconRef, 0xAA75, 0x000F, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, PtInIconRef, InterfaceLib);


Boolean C_RectInIconRef(const Rect *testRect, const Rect *iconRect, IconAlignmentType align, IconServicesUsageFlags iconServicesUsageFlags, IconRef theIconRef);PASCAL_SUBTRAP(RectInIconRef, 0xAA75, 0x0010, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, RectInIconRef, InterfaceLib);


OSErr C_IconRefToRgn(RgnHandle theRgn, const Rect *iconRect, IconAlignmentType align, IconServicesUsageFlags iconServicesUsageFlags, IconRef theIconRef);PASCAL_SUBTRAP(IconRefToRgn, 0xAA75, 0x0011, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IconRefToRgn, InterfaceLib);


OSErr C_GetIconSizesFromIconRef(IconSelectorValue iconSelectorInput, GUEST<IconSelectorValue> *iconSelectorOutputPtr, IconServicesUsageFlags iconServicesUsageFlags, IconRef theIconRef);PASCAL_SUBTRAP(GetIconSizesFromIconRef, 0xAA75, 0x0012, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconSizesFromIconRef, InterfaceLib);


OSErr C_FlushIconRefs(OSType creator, OSType iconType);PASCAL_SUBTRAP(FlushIconRefs, 0xAA75, 0x0029, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, FlushIconRefs, InterfaceLib);


OSErr C_FlushIconRefsByVolume(INTEGER vRefNum);PASCAL_SUBTRAP(FlushIconRefsByVolume, 0xAA75, 0x0018, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, FlushIconRefsByVolume, InterfaceLib);


OSErr C_SetCustomIconsEnabled(INTEGER vRefNum, Boolean enableCustomIcons);PASCAL_SUBTRAP(SetCustomIconsEnabled, 0xAA75, 0x001F, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, SetCustomIconsEnabled, InterfaceLib);


OSErr C_GetCustomIconsEnabled(INTEGER vRefNum, Boolean *customIconsEnabled);PASCAL_SUBTRAP(GetCustomIconsEnabled, 0xAA75, 0x0020, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetCustomIconsEnabled, InterfaceLib);


Boolean C_IsIconRefMaskEmpty(IconRef iconRef);PASCAL_SUBTRAP(IsIconRefMaskEmpty, 0xAA75, 0x0033, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, IsIconRefMaskEmpty, InterfaceLib);


IconRef C_GetIconRefVariant(IconRef inIconRef, OSType inVariant, GUEST<IconTransformType> *outTransform);PASCAL_SUBTRAP(GetIconRefVariant, 0xAA75, 0x0034, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, GetIconRefVariant, InterfaceLib);


OSErr C_RegisterIconRefFromIconFile(OSType creator, OSType iconType, const FSSpec *iconFile, GUEST<IconRef> *theIconRef);PASCAL_SUBTRAP(RegisterIconRefFromIconFile, 0xAA75, 0x0035, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, RegisterIconRefFromIconFile, InterfaceLib);


OSErr C_ReadIconFile(const FSSpec *iconFile, GUEST<IconFamilyHandle> *iconFamily);PASCAL_SUBTRAP(ReadIconFile, 0xAA75, 0x0036, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, ReadIconFile, InterfaceLib);


OSErr C_WriteIconFile(IconFamilyHandle iconFamily, const FSSpec *iconFile);PASCAL_SUBTRAP(WriteIconFile, 0xAA75, 0x0037, IconServicesDispatch);
LIBRARY_ALSO(IconServicesLib, WriteIconFile, InterfaceLib);


} /* namespace Executor */