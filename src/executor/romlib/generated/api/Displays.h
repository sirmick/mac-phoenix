/*****************************************************************************
 *                                                                           *
 *                                 Displays.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     AppleEvents.h    AppleEvent                                           *
 *     MacTypes.h       OSErr, Boolean, Ptr, OSType, Handle, Rect            *
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
#include "AppleEvents.h"
#include "ExMacTypes.h"
#include "ProcessMgr.h"
#include "CQuickDraw.h"
#include "Components.h"
#include "QuickDraw.h"
#include "WindowMgr.h"

#define MODULE_NAME Displays
#include <base/api-module.h>


namespace Executor {

using DMNotificationUPP = UPP<void(GUEST<AppleEvent> *theEvent)>;


// Like the real 7.5.5 dispatcher (through lowmem $7B0), only D0's low
// byte selects; the high byte is the parameter size in words.
DISPATCHER_TRAP(DisplayDispatch, 0xABEB, D0<0xFF>);


OSErr C_DMRegisterNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn);PASCAL_SUBTRAP(DMRegisterNotifyProc, 0xABEB, 0x0414, DisplayDispatch);


OSErr C_DMRemoveNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn);PASCAL_SUBTRAP(DMRemoveNotifyProc, 0xABEB, 0x0415, DisplayDispatch);


OSErr C_DMRegisterExtendedNotifyProc(ProcPtr proc, void *userData, uint16_t flags, ProcessSerialNumber *psn);PASCAL_SUBTRAP(DMRegisterExtendedNotifyProc, 0xABEB, 0x07EF, DisplayDispatch);


OSErr C_DMRemoveExtendedNotifyProc(ProcPtr proc, void *userData, ProcessSerialNumber *psn, uint16_t flags);PASCAL_SUBTRAP(DMRemoveExtendedNotifyProc, 0xABEB, 0x0726, DisplayDispatch);


GDHandle C_DMGetFirstScreenDevice(Boolean activeOnly);PASCAL_SUBTRAP(DMGetFirstScreenDevice, 0xABEB, 0x0000, DisplayDispatch);


GDHandle C_DMGetNextScreenDevice(GDHandle theDevice, Boolean activeOnly);PASCAL_SUBTRAP(DMGetNextScreenDevice, 0xABEB, 0x0001, DisplayDispatch);


OSErr C_DMGetDisplayIDByGDevice(GDHandle displayDevice, GUEST<uint32_t> *displayID, Boolean failToMain);PASCAL_SUBTRAP(DMGetDisplayIDByGDevice, 0xABEB, 0x051F, DisplayDispatch);


OSErr C_DMGetGDeviceByDisplayID(uint32_t displayID, GUEST<GDHandle> *displayDevice, Boolean failToMain);PASCAL_SUBTRAP(DMGetGDeviceByDisplayID, 0xABEB, 0x0520, DisplayDispatch);


using DMDisplayModeListIteratorUPP = UPP<void(void *userData, uint32_t itemIndex, Ptr displaymodeInfo)>;


OSErr C_DMNewDisplayModeList(uint32_t displayID, uint32_t modeListFlags, uint32_t reserved, GUEST<uint32_t> *thePanelCount, GUEST<Ptr> *thePanelList);PASCAL_SUBTRAP(DMNewDisplayModeList, 0xABEB, 0x0A36, DisplayDispatch);


OSErr C_DMGetIndexedDisplayModeFromList(Ptr panelList, uint32_t itemIndex, uint32_t reserved, DMDisplayModeListIteratorUPP listIterator, void *userData);PASCAL_SUBTRAP(DMGetIndexedDisplayModeFromList, 0xABEB, 0x0A37, DisplayDispatch);


// MacPhoenix: a display port component (7.6.1's PowerBook ZoomedVideo and the like) asks for an AVID as it registers. No AV ports here: kDMGenErr, and the component stays unregistered.
OSErr C_DMNewAVIDByPortComponent(Component thePortComponent, OSType portKind, uint32_t reserved, GUEST<GUEST<uint32_t>> *newID);PASCAL_SUBTRAP(DMNewAVIDByPortComponent, 0xABEB, 0x082E, DisplayDispatch);


OSErr C_DMGetPortComponentByAVID(uint32_t displayID, GUEST<GUEST<Component>> *portComponent, Ptr portDesc, GUEST<GUEST<OSType>> *portKind);PASCAL_SUBTRAP(DMGetPortComponentByAVID, 0xABEB, 0x082F, DisplayDispatch);


OSErr C_DMSendDependentNotification(OSType notifyType, OSType notifyClass, uint32_t displayID, ComponentInstance notifyComponent);PASCAL_SUBTRAP(DMSendDependentNotification, 0xABEB, 0x0830, DisplayDispatch);


// MacPhoenix: as DMNewAVIDByPortComponent, for a display device component: kDMGenErr.
OSErr C_DMNewAVIDByDeviceComponent(Component theDeviceComponent, OSType portKind, uint32_t reserved, GUEST<GUEST<uint32_t>> *newID);PASCAL_SUBTRAP(DMNewAVIDByDeviceComponent, 0xABEB, 0x0833, DisplayDispatch);


OSErr C_DMGetDeviceComponentByAVID(uint32_t displayID, GUEST<GUEST<Component>> *theDeviceComponent, Ptr theDesc, GUEST<GUEST<OSType>> *theKind);PASCAL_SUBTRAP(DMGetDeviceComponentByAVID, 0xABEB, 0x0835, DisplayDispatch);


OSErr C_DMGetDeviceAVIDByPortAVID(uint32_t portAVID, GUEST<GUEST<uint32_t>> *deviceAVID);PASCAL_SUBTRAP(DMGetDeviceAVIDByPortAVID, 0xABEB, 0x043B, DisplayDispatch);


OSErr C_DMDisposeList(Ptr panelList);PASCAL_SUBTRAP(DMDisposeList, 0xABEB, 0x022C, DisplayDispatch);


OSErr C_DMBeginConfigureDisplays(GUEST<Handle> *displayState);PASCAL_SUBTRAP(DMBeginConfigureDisplays, 0xABEB, 0x0206, DisplayDispatch);


OSErr C_DMEndConfigureDisplays(Handle displayState);PASCAL_SUBTRAP(DMEndConfigureDisplays, 0xABEB, 0x0207, DisplayDispatch);


OSErr C_DMSetDisplayMode(GDHandle theDevice, uint32_t mode, GUEST<uint32_t> *depthMode, uint32_t reserved, Handle displayState);PASCAL_SUBTRAP(DMSetDisplayMode, 0xABEB, 0x0A11, DisplayDispatch);


OSErr C_DMCheckDisplayMode(GDHandle theDevice, uint32_t mode, uint32_t depthMode, GUEST<uint32_t> *switchFlags, uint32_t reserved, Boolean *modeOk);PASCAL_SUBTRAP(DMCheckDisplayMode, 0xABEB, 0x0C12, DisplayDispatch);


OSErr C_DMGetDisplayMode(GDHandle theDevice, Ptr switchInfo);PASCAL_SUBTRAP(DMGetDisplayMode, 0xABEB, 0x043E, DisplayDispatch);


void C_DMDrawDesktopRect(Rect *globalRect);PASCAL_SUBTRAP(DMDrawDesktopRect, 0xABEB, 0x0002, DisplayDispatch);


void C_DMDrawDesktopRegion(RgnHandle globalRgn);PASCAL_SUBTRAP(DMDrawDesktopRegion, 0xABEB, 0x0003, DisplayDispatch);


OSErr C_DMGetDeskRegion(GUEST<GUEST<RgnHandle>> *desktopRegion);PASCAL_SUBTRAP(DMGetDeskRegion, 0xABEB, 0x0213, DisplayDispatch);


// Private (selector -6). Name and meaning are a guess: on 7.5.5 it takes
// a window and a region and fits the window against the screen devices.
// Finder calls it for each of its windows (CODE 12 +60), ignoring the
// result.
OSErr C_DMKeepWindowOnScreen(WindowPtr window, RgnHandle rgn);PASCAL_SUBTRAP(DMKeepWindowOnScreen, 0xABEB, 0x04FA, DisplayDispatch);


// Private (selector -10). Name is a guess. On 7.5.5 it always returns
// false, and Finder (CODE 152 +6f4) then does its own placement.
Boolean C_DMPlacesWindow(WindowPtr window);PASCAL_SUBTRAP(DMPlacesWindow, 0xABEB, 0xFFF6, DisplayDispatch);


// Private (selector -9). Name is a guess. Finder calls it after editing
// the region at ExpandMem+$1F4; on 7.5.5 it does nothing.
void C_DMDesktopIconRgnChanged();PASCAL_SUBTRAP(DMDesktopIconRgnChanged, 0xABEB, 0xFFF7, DisplayDispatch);


} /* namespace Executor */