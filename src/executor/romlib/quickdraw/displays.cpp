#include <Displays.h>
#include <QuickDraw.h>
#include <WindowMgr.h>
#include <error/error.h>
#include <MemoryMgr.h>
#include <mman/mman.h>
#include <quickdraw/cquick.h>
#include <vdriver/vdriver.h>
#include <base/traps.impl.h>

enum { kDMNotFoundErr = -6229 };

using namespace Executor;

/* MacPhoenix: the parts of Display Manager 2.0 (Gestalt 'dplv' $00020006 on
   a real 7.5.5 boot) that Finder calls. One screen, no configuration
   changes, so no notifications are ever sent. */

OSErr Executor::C_DMRegisterNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn)
{
    return noErr;
}

OSErr Executor::C_DMRemoveNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn)
{
    return noErr;
}

/* The display never changes, so there is nothing to notify. */
OSErr Executor::C_DMRegisterExtendedNotifyProc(ProcPtr proc, void *userData,
                                               uint16_t flags, ProcessSerialNumber *psn)
{
    return noErr;
}

OSErr Executor::C_DMRemoveExtendedNotifyProc(ProcPtr proc, void *userData,
                                             ProcessSerialNumber *psn, uint16_t flags)
{
    return noErr;
}

/* One screen, the main device: its display ID is 1. (Warcraft II asks for
   the ID of the device it runs on.) */
enum { mainDisplayID = 1 };

OSErr Executor::C_DMGetDisplayIDByGDevice(GDHandle displayDevice, GUEST<uint32_t> *displayID,
                                          Boolean failToMain)
{
    if(displayDevice != LM(MainDevice) && !failToMain)
        return paramErr;
    *displayID = mainDisplayID;
    return noErr;
}

OSErr Executor::C_DMGetGDeviceByDisplayID(uint32_t displayID, GUEST<GDHandle> *displayDevice,
                                          Boolean failToMain)
{
    if(displayID != mainDisplayID && !failToMain)
        return paramErr;
    *displayDevice = LM(MainDevice);
    return noErr;
}

/* Display mode lists (Display Manager 2.0): one timing, the screen's
   resolution, at every depth the screen has. Warcraft II reads it to check
   for a 640x480 256-colour display and refuses to run without one. The
   records are laid out as in Apple's Video.h and Displays.h. */
namespace
{
struct VDSwitchInfoRec { GUEST_STRUCT;
    GUEST<uint16_t> csMode; GUEST<uint32_t> csData; GUEST<uint16_t> csPage;
    GUEST<Ptr> csBaseAddr; GUEST<uint32_t> csReserved; };
static_assert(sizeof(VDSwitchInfoRec) == 16);

struct VDResolutionInfoRec { GUEST_STRUCT;
    GUEST<uint32_t> csPreviousDisplayModeID, csDisplayModeID, csHorizontalPixels,
        csVerticalLines; GUEST<int32_t> csRefreshRate; GUEST<uint16_t> csMaxDepthMode;
    GUEST<uint32_t> csResolutionReserved, csReserved; };
static_assert(sizeof(VDResolutionInfoRec) == 30);

struct VDTimingInfoRec { GUEST_STRUCT;
    GUEST<uint32_t> csTimingMode, csTimingReserved, csTimingFormat, csTimingData, csTimingFlags; };
static_assert(sizeof(VDTimingInfoRec) == 20);

struct VPBlock { GUEST_STRUCT;
    GUEST<int32_t> vpBaseOffset; GUEST<int16_t> vpRowBytes; Rect vpBounds;
    GUEST<int16_t> vpVersion, vpPackType; GUEST<int32_t> vpPackSize, vpHRes, vpVRes;
    GUEST<int16_t> vpPixelType, vpPixelSize, vpCmpCount, vpCmpSize; GUEST<int32_t> vpPlaneBytes; };
static_assert(sizeof(VPBlock) == 42);

struct DMDepthInfoRec { GUEST_STRUCT;
    GUEST<Ptr> depthSwitchInfo, depthVPBlock; GUEST<uint32_t> depthFlags, depthReserved1, depthReserved2; };
static_assert(sizeof(DMDepthInfoRec) == 20);

struct DMDepthInfoBlockRec { GUEST_STRUCT;
    GUEST<uint32_t> depthBlockCount; GUEST<Ptr> depthVPBlock;
    GUEST<uint32_t> depthBlockFlags, depthBlockReserved1, depthBlockReserved2; };
static_assert(sizeof(DMDepthInfoBlockRec) == 20);

struct DMDisplayModeListEntryRec { GUEST_STRUCT;
    GUEST<uint32_t> displayModeFlags; GUEST<Ptr> displayModeSwitchInfo, displayModeResolutionInfo,
        displayModeTimingInfo, displayModeDepthBlockInfo; GUEST<uint32_t> displayModeVersion;
    GUEST<Ptr> displayModeName, displayModeDisplayInfo; };
static_assert(sizeof(DMDisplayModeListEntryRec) == 32);

enum { resolutionID = 0x80, firstDepthMode = 0x80, maxDepths = 6 };

/* A list: its one entry and everything the entry points at. */
struct ModeList { GUEST_STRUCT;
    GUEST<uint32_t> count;
    DMDisplayModeListEntryRec entry;
    VDSwitchInfoRec switchInfo;
    VDResolutionInfoRec resolution;
    VDTimingInfoRec timing;
    DMDepthInfoBlockRec depthBlock;
    DMDepthInfoRec depths[maxDepths];
    VDSwitchInfoRec depthSwitch[maxDepths];
    VPBlock vp[maxDepths];
    unsigned char name[32];
    unsigned char displayInfo[64];      /* DMDisplayTimingInfoRec, all zero */
};
}

OSErr Executor::C_DMNewDisplayModeList(uint32_t displayID, uint32_t modeListFlags, uint32_t reserved,
                                       GUEST<uint32_t> *thePanelCount, GUEST<Ptr> *thePanelList)
{
    if(displayID != mainDisplayID)
        return kDMNotFoundErr;
    ModeList *l = (ModeList *)NewPtrClear(sizeof(ModeList));
    if(!l)
        return MemError();

    int w = vdriver->width(), h = vdriver->height(), n = 0, current = 0;
    for(int d : { 1, 2, 4, 8, 16, 32 })
    {
        if(!vdriver->isAcceptableMode(0, 0, d, false))
            continue;
        if(d == vdriver->bpp())
            current = n;
        VDSwitchInfoRec& sw = l->depthSwitch[n];
        sw.csMode = firstDepthMode + n;
        sw.csData = resolutionID;
        VPBlock& vp = l->vp[n];
        vp.vpRowBytes = (w * d + 15) / 16 * 2;
        vp.vpBounds.bottom = h;
        vp.vpBounds.right = w;
        vp.vpHRes = vp.vpVRes = 72 << 16;
        vp.vpPixelType = d > 8 ? RGBDirect : 0;
        vp.vpPixelSize = d;
        vp.vpCmpCount = d > 8 ? 3 : 1;
        vp.vpCmpSize = d == 16 ? 5 : d == 32 ? 8 : d;
        l->depths[n].depthSwitchInfo = (Ptr)&sw;
        l->depths[n].depthVPBlock = (Ptr)&vp;
        n++;
    }

    l->switchInfo.csMode = firstDepthMode + current;
    l->switchInfo.csData = resolutionID;
    l->switchInfo.csBaseAddr = PIXMAP_BASEADDR(GD_PMAP(LM(MainDevice)));
    l->resolution.csDisplayModeID = resolutionID;
    l->resolution.csHorizontalPixels = w;
    l->resolution.csVerticalLines = h;
    l->resolution.csRefreshRate = 67 << 16;
    l->resolution.csMaxDepthMode = firstDepthMode + n - 1;
    l->timing.csTimingMode = resolutionID;
    l->timing.csTimingFormat = "decl"_4;
    l->timing.csTimingData = 130;                   /* timingApple_640x480_67hz */
    l->timing.csTimingFlags = 0xF;                  /* valid, safe, default, shown now */
    l->depthBlock.depthBlockCount = n;
    l->depthBlock.depthVPBlock = (Ptr)l->depths;
    snprintf((char *)l->name + 1, sizeof l->name - 1, "%d x %d", w, h);
    l->name[0] = strlen((char *)l->name + 1);

    DMDisplayModeListEntryRec& e = l->entry;
    e.displayModeSwitchInfo = (Ptr)&l->switchInfo;
    e.displayModeResolutionInfo = (Ptr)&l->resolution;
    e.displayModeTimingInfo = (Ptr)&l->timing;
    e.displayModeDepthBlockInfo = (Ptr)&l->depthBlock;
    e.displayModeVersion = 1;                       /* kDisplayModeEntryVersionOne */
    e.displayModeName = (Ptr)l->name;
    e.displayModeDisplayInfo = (Ptr)l->displayInfo;
    l->count = 1;

    *thePanelCount = 1;
    *thePanelList = (Ptr)l;
    return noErr;
}

OSErr Executor::C_DMGetIndexedDisplayModeFromList(Ptr panelList, uint32_t itemIndex, uint32_t reserved,
                                                  DMDisplayModeListIteratorUPP listIterator, void *userData)
{
    ModeList *l = (ModeList *)panelList;
    if(!l || itemIndex >= l->count)
        return kDMNotFoundErr;
    if(listIterator)
        listIterator(userData, itemIndex, (Ptr)&l->entry);
    return noErr;
}

/* The depth modes the list numbers, as SetDepth takes them. */
static uint32_t current_depth_mode()
{
    uint32_t mode = firstDepthMode;
    for(int d : { 1, 2, 4, 8, 16, 32 })
        if(vdriver->isAcceptableMode(0, 0, d, false))
        {
            if(d == vdriver->bpp())
                return mode;
            mode++;
        }
    return firstDepthMode;
}

OSErr Executor::C_DMGetDisplayMode(GDHandle theDevice, Ptr switchInfo)
{
    if(theDevice != LM(MainDevice))
        return kDMNotFoundErr;
    VDSwitchInfoRec *sw = (VDSwitchInfoRec *)switchInfo;
    sw->csMode = current_depth_mode();
    sw->csData = resolutionID;
    sw->csPage = 0;
    sw->csBaseAddr = PIXMAP_BASEADDR(GD_PMAP(LM(MainDevice)));
    sw->csReserved = 0;
    return noErr;
}

OSErr Executor::C_DMCheckDisplayMode(GDHandle theDevice, uint32_t mode, uint32_t depthMode,
                                     GUEST<uint32_t> *switchFlags, uint32_t reserved, Boolean *modeOk)
{
    *switchFlags = 0;
    *modeOk = theDevice == LM(MainDevice) && mode == resolutionID;
    return noErr;
}

/* The one resolution; a depth goes through SetDepth (which takes the
   list's depth modes). */
OSErr Executor::C_DMSetDisplayMode(GDHandle theDevice, uint32_t mode, GUEST<uint32_t> *depthMode,
                                   uint32_t reserved, Handle displayState)
{
    if(theDevice != LM(MainDevice) || mode != resolutionID)
        return kDMNotFoundErr;
    if(depthMode && *depthMode && *depthMode != current_depth_mode())
    {
        OSErr err = SetDepth(theDevice, *depthMode, 0, 0);
        if(err != noErr)
            return err;
    }
    if(depthMode)
        *depthMode = current_depth_mode();
    return noErr;
}

OSErr Executor::C_DMBeginConfigureDisplays(GUEST<Handle> *displayState)
{
    *displayState = nullptr;
    return noErr;
}

OSErr Executor::C_DMEndConfigureDisplays(Handle displayState)
{
    return noErr;
}

OSErr Executor::C_DMNewAVIDByPortComponent(Component thePortComponent, OSType portKind,
                                            uint32_t reserved, GUEST<uint32_t> *newID)
{
    if(newID)
        *newID = 0;
    return -6220; /* kDMGenErr: no display ports */
}

/* No AV ports or devices: nothing has an AVID. */
OSErr Executor::C_DMNewAVIDByDeviceComponent(Component theDeviceComponent, OSType portKind,
                                              uint32_t reserved, GUEST<uint32_t> *newID)
{
    if(newID)
        *newID = 0;
    return -6220; /* kDMGenErr */
}

OSErr Executor::C_DMGetPortComponentByAVID(uint32_t displayID, GUEST<Component> *portComponent,
                                            Ptr portDesc, GUEST<OSType> *portKind)
{
    return -6220;
}

OSErr Executor::C_DMGetDeviceComponentByAVID(uint32_t displayID, GUEST<Component> *theDeviceComponent,
                                              Ptr theDesc, GUEST<OSType> *theKind)
{
    return -6220;
}

OSErr Executor::C_DMGetDeviceAVIDByPortAVID(uint32_t portAVID, GUEST<uint32_t> *deviceAVID)
{
    return -6220;
}

OSErr Executor::C_DMSendDependentNotification(OSType notifyType, OSType notifyClass,
                                               uint32_t displayID, ComponentInstance notifyComponent)
{
    return noErr;
}

OSErr Executor::C_DMDisposeList(Ptr panelList)
{
    if(panelList)
        DisposePtr(panelList);
    return noErr;
}

GDHandle Executor::C_DMGetFirstScreenDevice(Boolean activeOnly)
{
    return GetDeviceList();
}

GDHandle Executor::C_DMGetNextScreenDevice(GDHandle theDevice, Boolean activeOnly)
{
    return theDevice ? GetNextDevice(theDevice) : nullptr;
}

void Executor::C_DMDrawDesktopRegion(RgnHandle globalRgn)
{
    PaintOne(nullptr, globalRgn);
}

void Executor::C_DMDrawDesktopRect(Rect *globalRect)
{
    RgnHandle rgn = NewRgn();
    RectRgn(rgn, globalRect);
    PaintOne(nullptr, rgn);
    DisposeRgn(rgn);
}

OSErr Executor::C_DMGetDeskRegion(GUEST<RgnHandle> *desktopRegion)
{
    *desktopRegion = LM(GrayRgn);
    return noErr;
}

OSErr Executor::C_DMKeepWindowOnScreen(WindowPtr window, RgnHandle rgn)
{
    return noErr;
}

Boolean Executor::C_DMPlacesWindow(WindowPtr window)
{
    return false;
}

void Executor::C_DMDesktopIconRgnChanged()
{
}
