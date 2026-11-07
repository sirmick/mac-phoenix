/*****************************************************************************
 *                                                                           *
 *                                 Displays.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h       UInt32, OSErr, Boolean, Ptr, OSType, Handle,         *
 *                      Rect, INTEGER, StringPtr                             *
 *     AppleEvents.h    AppleEvent                                           *
 *     ProcessMgr.h     ProcessSerialNumber                                  *
 *     CQuickDraw.h     GDHandle                                             *
 *     Components.h     Component, ComponentInstance                         *
 *     QuickDraw.h      RgnHandle                                            *
 *     WindowMgr.h      WindowPtr                                            *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "AppleEvents.h"
#include "ProcessMgr.h"
#include "CQuickDraw.h"
#include "Components.h"
#include "QuickDraw.h"
#include "WindowMgr.h"

#define MODULE_NAME Displays
#include <base/api-module.h>


namespace Executor {

typedef UInt32 DisplayIDType;

using DMNotificationUPP = UPP<void(GUEST<AppleEvent> *theEvent)>;


enum {kDMGenErr = -6220,
kDMNotFoundErr = -6229,
};

// An extended notify proc's message: Installed at registration,
// Removed at removal; for a change Prep, Dependents and Event, the
// last two with the 'cnfg' event (docs/executor/DisplayModes.md).
enum {kDMNotifyRequestConnectionProbe = 0,
kDMNotifyInstalled = 1,
kDMNotifyEvent = 2,
kDMNotifyRemoved = 3,
kDMNotifyPrep = 4,
kDMNotifyExtendEvent = 5,
kDMNotifyDependents = 6,
};

// The Display Manager's 'aevt'/'cnfg' event: kAEDisplayNotice holds a
// record per display, keyed by its ID, of the old and new configuration.
enum {kAESystemConfigNotice = "cnfg"_4,
kAEDisplayNotice = "dspl"_4,
kAEDisplaySummary = "dsum"_4,
keyDMConfigVersion = "dmcv"_4,
keyDMConfigFlags = "dmcf"_4,
keyDMConfigReserved = "dmcr"_4,
keyDisplayID = "dmid"_4,
keyDisplayComponent = "dmdc"_4,
keyDisplayDevice = "dmdd"_4,
keyDisplayFlags = "dmdf"_4,
keyDisplayMode = "dmdm"_4,
keyDisplayModeReserved = "dmmr"_4,
keyDisplayReserved = "dmdr"_4,
keyDeviceFlags = "dddf"_4,
keyDeviceDepthMode = "dddm"_4,
keyDeviceRect = "dddr"_4,
keyPixMapRect = "dpdr"_4,
keyPixMapHResolution = "dphr"_4,
keyPixMapVResolution = "dpvr"_4,
keyPixMapPixelType = "dppt"_4,
keyPixMapPixelSize = "dpps"_4,
keyPixMapCmpCount = "dpcc"_4,
keyPixMapCmpSize = "dpcs"_4,
keyPixMapReserved = "dppr"_4,
keyPixMapColorTableSeed = "dpct"_4,
keyDisplayOldConfig = "dold"_4,
keyDisplayNewConfig = "dnew"_4,
};

// Like the real 7.5.5 dispatcher (through lowmem $7B0), only D0's low
// byte selects; the high byte is the parameter size in words.
DISPATCHER_TRAP(DisplayDispatch, 0xABEB, D0<0xFF>);


OSErr C_DMRegisterNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn);PASCAL_SUBTRAP(DMRegisterNotifyProc, 0xABEB, 0x0414, DisplayDispatch);


OSErr C_DMRemoveNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn);PASCAL_SUBTRAP(DMRemoveNotifyProc, 0xABEB, 0x0415, DisplayDispatch);


OSErr C_DMRegisterExtendedNotifyProc(ProcPtr proc, void *userData, uint16_t flags, ProcessSerialNumber *psn);PASCAL_SUBTRAP(DMRegisterExtendedNotifyProc, 0xABEB, 0x07EF, DisplayDispatch);


OSErr C_DMRemoveExtendedNotifyProc(ProcPtr proc, void *userData, ProcessSerialNumber *psn, uint16_t flags);PASCAL_SUBTRAP(DMRemoveExtendedNotifyProc, 0xABEB, 0x0726, DisplayDispatch);


GDHandle C_DMGetFirstScreenDevice(Boolean activeOnly);PASCAL_SUBTRAP(DMGetFirstScreenDevice, 0xABEB, 0x0000, DisplayDispatch);


GDHandle C_DMGetNextScreenDevice(GDHandle theDevice, Boolean activeOnly);PASCAL_SUBTRAP(DMGetNextScreenDevice, 0xABEB, 0x0001, DisplayDispatch);


OSErr C_DMGetDisplayIDByGDevice(GDHandle displayDevice, GUEST<DisplayIDType> *displayID, Boolean failToMain);PASCAL_SUBTRAP(DMGetDisplayIDByGDevice, 0xABEB, 0x051F, DisplayDispatch);


OSErr C_DMGetGDeviceByDisplayID(DisplayIDType displayID, GUEST<GDHandle> *displayDevice, Boolean failToMain);PASCAL_SUBTRAP(DMGetGDeviceByDisplayID, 0xABEB, 0x0520, DisplayDispatch);


using DMDisplayModeListIteratorUPP = UPP<void(void *userData, uint32_t itemIndex, Ptr displaymodeInfo)>;


OSErr C_DMNewDisplayModeList(DisplayIDType displayID, uint32_t modeListFlags, uint32_t reserved, GUEST<uint32_t> *thePanelCount, GUEST<Ptr> *thePanelList);PASCAL_SUBTRAP(DMNewDisplayModeList, 0xABEB, 0x0A36, DisplayDispatch);


OSErr C_DMGetIndexedDisplayModeFromList(Ptr panelList, uint32_t itemIndex, uint32_t reserved, DMDisplayModeListIteratorUPP listIterator, void *userData);PASCAL_SUBTRAP(DMGetIndexedDisplayModeFromList, 0xABEB, 0x0A37, DisplayDispatch);


// MacPhoenix: a display port component (7.6.1's PowerBook ZoomedVideo and the like) asks for an AVID as it registers. No AV ports here: kDMGenErr, and the component stays unregistered.
OSErr C_DMNewAVIDByPortComponent(Component thePortComponent, OSType portKind, uint32_t reserved, GUEST<uint32_t> *newID);PASCAL_SUBTRAP(DMNewAVIDByPortComponent, 0xABEB, 0x082E, DisplayDispatch);


OSErr C_DMGetPortComponentByAVID(DisplayIDType displayID, GUEST<Component> *portComponent, Ptr portDesc, GUEST<OSType> *portKind);PASCAL_SUBTRAP(DMGetPortComponentByAVID, 0xABEB, 0x082F, DisplayDispatch);


OSErr C_DMSendDependentNotification(OSType notifyType, OSType notifyClass, DisplayIDType displayID, ComponentInstance notifyComponent);PASCAL_SUBTRAP(DMSendDependentNotification, 0xABEB, 0x0830, DisplayDispatch);


// MacPhoenix: as DMNewAVIDByPortComponent, for a display device component: kDMGenErr.
OSErr C_DMNewAVIDByDeviceComponent(Component theDeviceComponent, OSType portKind, uint32_t reserved, GUEST<uint32_t> *newID);PASCAL_SUBTRAP(DMNewAVIDByDeviceComponent, 0xABEB, 0x0833, DisplayDispatch);


OSErr C_DMGetDeviceComponentByAVID(DisplayIDType displayID, GUEST<Component> *theDeviceComponent, Ptr theDesc, GUEST<OSType> *theKind);PASCAL_SUBTRAP(DMGetDeviceComponentByAVID, 0xABEB, 0x0835, DisplayDispatch);


OSErr C_DMGetDeviceAVIDByPortAVID(uint32_t portAVID, GUEST<uint32_t> *deviceAVID);PASCAL_SUBTRAP(DMGetDeviceAVIDByPortAVID, 0xABEB, 0x043B, DisplayDispatch);


OSErr C_DMDisposeList(Ptr panelList);PASCAL_SUBTRAP(DMDisposeList, 0xABEB, 0x022C, DisplayDispatch);


OSErr C_DMBeginConfigureDisplays(GUEST<Handle> *displayState);PASCAL_SUBTRAP(DMBeginConfigureDisplays, 0xABEB, 0x0206, DisplayDispatch);


OSErr C_DMEndConfigureDisplays(Handle displayState);PASCAL_SUBTRAP(DMEndConfigureDisplays, 0xABEB, 0x0207, DisplayDispatch);


OSErr C_DMSetDisplayMode(GDHandle theDevice, uint32_t mode, GUEST<uint32_t> *depthMode, uint32_t reserved, Handle displayState);PASCAL_SUBTRAP(DMSetDisplayMode, 0xABEB, 0x0A11, DisplayDispatch);


OSErr C_DMCheckDisplayMode(GDHandle theDevice, uint32_t mode, uint32_t depthMode, GUEST<uint32_t> *switchFlags, uint32_t reserved, Boolean *modeOk);PASCAL_SUBTRAP(DMCheckDisplayMode, 0xABEB, 0x0C12, DisplayDispatch);


OSErr C_DMGetDisplayMode(GDHandle theDevice, Ptr switchInfo);PASCAL_SUBTRAP(DMGetDisplayMode, 0xABEB, 0x043E, DisplayDispatch);


void C_DMDrawDesktopRect(Rect *globalRect);PASCAL_SUBTRAP(DMDrawDesktopRect, 0xABEB, 0x0002, DisplayDispatch);


void C_DMDrawDesktopRegion(RgnHandle globalRgn);PASCAL_SUBTRAP(DMDrawDesktopRegion, 0xABEB, 0x0003, DisplayDispatch);


OSErr C_DMGetDeskRegion(GUEST<RgnHandle> *desktopRegion);PASCAL_SUBTRAP(DMGetDeskRegion, 0xABEB, 0x0213, DisplayDispatch);


// MacPhoenix, as the rest below: Apple's Monitors control panel calls these.
OSErr C_DMEnableDisplay(GDHandle enableDevice, Handle displayState);PASCAL_SUBTRAP(DMEnableDisplay, 0xABEB, 0x040B, DisplayDispatch);


OSErr C_DMSetMainDisplay(GDHandle newMainDevice, Handle displayState);PASCAL_SUBTRAP(DMSetMainDisplay, 0xABEB, 0x0410, DisplayDispatch);


OSErr C_DMGetNextMirroredDevice(GDHandle gDevice, GUEST<GDHandle> *mirroredDevice);PASCAL_SUBTRAP(DMGetNextMirroredDevice, 0xABEB, 0x041B, DisplayDispatch);


OSErr C_DMMoveDisplay(GDHandle moveDevice, INTEGER x, INTEGER y, Handle displayState);PASCAL_SUBTRAP(DMMoveDisplay, 0xABEB, 0x0609, DisplayDispatch);


OSErr C_DMGetNameByAVID(uint32_t theID, uint32_t nameFlags, StringPtr name);PASCAL_SUBTRAP(DMGetNameByAVID, 0xABEB, 0x062D, DisplayDispatch);


OSErr C_DMSaveScreenPrefs(uint32_t reserved1, uint32_t saveFlags, uint32_t reserved2);PASCAL_SUBTRAP(DMSaveScreenPrefs, 0xABEB, 0x0632, DisplayDispatch);


// MacPhoenix: false and noErr, as a 7.6.1 Quadra answers (a probe on
// the ROM machine). The desktop asks at boot.
OSErr C_DMQDIsMirroringCapable(Boolean *qdIsMirroringCapable);PASCAL_SUBTRAP(DMQDIsMirroringCapable, 0xABEB, 0x0216, DisplayDispatch);


// MacPhoenix: -6226 and the Boolean left alone, as a 7.6.1 Quadra with
// one screen answers (a probe on the ROM machine).
OSErr C_DMCanMirrorNow(Boolean *canMirrorNow);PASCAL_SUBTRAP(DMCanMirrorNow, 0xABEB, 0x0217, DisplayDispatch);


OSErr C_DMIsMirroringOn(Boolean *isMirroringOn);PASCAL_SUBTRAP(DMIsMirroringOn, 0xABEB, 0x0218, DisplayDispatch);


// Private (selector -6). Name and meaning are a guess: on 7.5.5 it takes
// a window and a region and fits the window against the screen devices.
// Finder calls it for each of its windows (CODE 12 +60), ignoring the
// result.
OSErr C_DMKeepWindowOnScreen(WindowPtr window, RgnHandle rgn);PASCAL_SUBTRAP(DMKeepWindowOnScreen, 0xABEB, 0x04FA, DisplayDispatch);


// Private (selector -10). Name is a guess. On 7.5.5 it always returns
// false, and Finder (CODE 152 +6f4) then does its own placement.
Boolean C_DMPlacesWindow(WindowPtr window);PASCAL_SUBTRAP(DMPlacesWindow, 0xABEB, 0xFFF6, DisplayDispatch);


// Private (selector -13). Name is a guess. Apple's Monitors control
// panel calls it with a screen's GDHandle, a VDSwitchInfoRec, a depth
// mode and two result buffers; the second gets the screen's bounds in
// that mode, the Rect the panel draws the screen from in its arrangement
// (0,0,480,640 on the ROM machine at 640x480). The first (4 bytes) is
// not read by the panel; left alone.
OSErr C_DMGetModeBounds(GDHandle theDevice, Ptr switchInfo, uint32_t depthMode, Ptr result1, Rect *bounds);PASCAL_SUBTRAP(DMGetModeBounds, 0xABEB, 0x0AF3, DisplayDispatch);


// Private (selector -9). Name is a guess. Finder calls it after editing
// the region at ExpandMem+$1F4; on 7.5.5 it does nothing.
void C_DMDesktopIconRgnChanged();PASCAL_SUBTRAP(DMDesktopIconRgnChanged, 0xABEB, 0xFFF7, DisplayDispatch);


} /* namespace Executor */