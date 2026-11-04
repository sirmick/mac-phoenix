#include <Displays.h>
#include <QuickDraw.h>
#include <VDriver.h>
#include <WindowMgr.h>
#include <error/error.h>
#include <MemoryMgr.h>
#include <mman/mman.h>
#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <rsys/process.h>
#include <vdriver/vdriver.h>
#include <AppleEvents.h>
#include <ProcessMgr.h>
#include <OSUtil.h>
#include <MenuMgr.h>
#include <base/common.h>
#include <SysErr.h>
#include <rsys/displaymode.h>
#include <menu/menu.h>
#include <SegmentLdr.h>
#include <MixedMode.h>
#include <base/cpu.h>
#include <base/traps.impl.h>

#include <iterator>
#include <vector>

using namespace Executor;

/* MacPhoenix: Display Manager 2.0 (Gestalt 'dplv' $00020202 on the Quadra's
   7.6.1, $00020404 on the G3's 8.6) for one screen whose resolution can
   change: DMSetDisplayMode to another resolution of the mode list, or the
   host's request (Settings, the browser window: VideoDriver::requestSize,
   taken at the front process's event call). docs/executor/DisplayModes.md;
   what a Mac does, measured with tests/guest/dmprobe:

   - the GDevice, the screen's ports, GrayRgn, every process's windows
     (moved back on screen) and screenBits change before DMSetDisplayMode
     returns; between DMBeginConfigureDisplays and DMEndConfigureDisplays,
     the GDevice at once, the rest at the end;
   - extended notify procs get kDMNotifyInstalled at registration,
     kDMNotifyRemoved at removal and, for a change, kDMNotifyPrep,
     kDMNotifyDependents and kDMNotifyEvent with an 'aevt'/'cnfg' event;
     then the notify procs get the same event (its 'dspl' record has one
     record per display, keyed by its ID, with 'dold' and 'dnew');
   - no Apple Event is sent to Display Manager aware applications.

   Here a process's notify procs, QuickDraw globals and menu bar follow at
   its own next event call (ROMlib_dm_poll): its procs run in its own
   world, as they would on a Mac where the Process Manager switched to it.
   The one that changed the mode gets them before DMSetDisplayMode returns. */

namespace
{
using DMExtendedNotificationUPP = UPP<void(void *userData, INTEGER theMessage, void *notifyData)>;

struct NotifyProc
{
    bool extended;
    void *proc;             /* a DMNotificationUPP or DMExtendedNotificationUPP */
    void *user_data;
    uint16_t flags;
    ProcessSerialNumber psn;
};
std::vector<NotifyProc> notify_procs;

/* The screen as each change left it, oldest first: a process that missed
   several changes is told about them as one, from the screen it last saw
   to the one there is. */
struct ScreenState
{
    Rect bounds;
    uint32_t resolution;
    int depth;
};
std::vector<ScreenState> history;
void start_history();

/* Per process (ROMlib_process_register_state): how many changes it has
   seen; 0 in a new process, which catches up on its first event call
   (its screenBits are made from the GDevice anyway). */
int32_t seen_changes;
const bool seen_registered = (ROMlib_process_register_state(&seen_changes, sizeof seen_changes), true);

int configuring;            /* DMBeginConfigureDisplays without its End */
bool configured_change;     /* a change in it, finished at the End */
Rect configure_old;

bool current_process(ProcessSerialNumber *psn)
{
    return GetCurrentProcess(psn) == noErr;
}

bool same_psn(const ProcessSerialNumber &a, const ProcessSerialNumber &b)
{
    return a.highLongOfPSN == b.highLongOfPSN && a.lowLongOfPSN == b.lowLongOfPSN;
}

/* kCurrentProcess as the caller's own. */
ProcessSerialNumber owner_psn(ProcessSerialNumber *psn)
{
    ProcessSerialNumber me = {};
    current_process(&me);
    if(!psn || (psn->highLongOfPSN == 0 && psn->lowLongOfPSN == kCurrentProcess))
        return me;
    return *psn;
}
}

static void call_notify(const NotifyProc &n, INTEGER message, AppleEvent *ae);

OSErr Executor::C_DMRegisterNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn)
{
    if(!proc)
        return paramErr;
    notify_procs.push_back({ false, proc.ptr, nullptr, 0, owner_psn(psn) });
    start_history();
    seen_changes = (int32_t)history.size();     /* from now on */
    return noErr;
}

OSErr Executor::C_DMRemoveNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn)
{
    ProcessSerialNumber who = owner_psn(psn);
    for(auto it = notify_procs.begin(); it != notify_procs.end(); ++it)
        if(!it->extended && it->proc == proc.ptr && same_psn(it->psn, who))
        {
            notify_procs.erase(it);
            return noErr;
        }
    return paramErr;
}

OSErr Executor::C_DMRegisterExtendedNotifyProc(ProcPtr proc, void *userData,
                                               uint16_t flags, ProcessSerialNumber *psn)
{
    if(!proc)
        return paramErr;
    NotifyProc n = { true, (void *)proc, userData, flags, owner_psn(psn) };
    notify_procs.push_back(n);
    start_history();
    seen_changes = (int32_t)history.size();
    call_notify(n, kDMNotifyInstalled, nullptr);
    return noErr;
}

OSErr Executor::C_DMRemoveExtendedNotifyProc(ProcPtr proc, void *userData,
                                             ProcessSerialNumber *psn, uint16_t flags)
{
    ProcessSerialNumber who = owner_psn(psn);
    for(auto it = notify_procs.begin(); it != notify_procs.end(); ++it)
        if(it->extended && it->proc == (void *)proc && it->user_data == userData
           && same_psn(it->psn, who))
        {
            NotifyProc n = *it;
            notify_procs.erase(it);
            call_notify(n, kDMNotifyRemoved, nullptr);
            return noErr;
        }
    return paramErr;
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

/* The resolutions: the classic Macs' screens (Settings' list), each with
   its own ID from 0x80, and the screen's own size if it is none of them
   (the browser window's), as the ID after them. Depth modes are numbered
   from 0x80 over the depths the screen has, as SetDepth takes them. */
namespace
{
const struct { int width, height; } classic_sizes[] = {
    { 512, 342 }, { 512, 384 }, { 640, 400 }, { 640, 480 }, { 800, 600 }, { 832, 624 },
    { 1024, 768 }, { 1152, 870 }, { 1280, 1024 }, { 1600, 1200 },
};
enum { firstResolutionID = 0x80, firstDepthMode = 0x80, maxDepths = 6 };
const uint32_t customResolutionID = firstResolutionID + std::size(classic_sizes);

/* The largest screen the host may ask for: one framebuffer block that
   size is kept (vdriver.h Framebuffer::reserve). The wasm build's low
   allocator has 64 MB for the coroutines' stacks and the screen. */
#if defined(__EMSCRIPTEN__)
const int maxWidth = 1920, maxHeight = 1200;
#else
const int maxWidth = 3840, maxHeight = 2400;
#endif

uint32_t resolution_id(int width, int height)
{
    for(size_t i = 0; i < std::size(classic_sizes); i++)
        if(classic_sizes[i].width == width && classic_sizes[i].height == height)
            return firstResolutionID + i;
    return customResolutionID;
}

uint32_t current_resolution()
{
    return resolution_id(vdriver->width(), vdriver->height());
}

bool size_acceptable(int width, int height)
{
    return width >= VDRIVER_MIN_SCREEN_WIDTH && height >= VDRIVER_MIN_SCREEN_HEIGHT
        && width <= maxWidth && height <= maxHeight;
}
}

uint32_t Executor::ROMlib_dm_current_resolution()
{
    return current_resolution();
}

bool Executor::ROMlib_dm_size_acceptable(int width, int height)
{
    return size_acceptable(width, height);
}

std::vector<DisplayResolution> Executor::ROMlib_dm_resolutions()
{
    std::vector<DisplayResolution> out;
    for(size_t i = 0; i < std::size(classic_sizes); i++)
        if(size_acceptable(classic_sizes[i].width, classic_sizes[i].height))
            out.push_back({ uint32_t(firstResolutionID + i), classic_sizes[i].width, classic_sizes[i].height });
    if(current_resolution() == customResolutionID)
        out.push_back({ customResolutionID, vdriver->width(), vdriver->height() });
    return out;
}

static bool resolution_size(uint32_t id, int *width, int *height)
{
    for(const DisplayResolution &r : ROMlib_dm_resolutions())
        if(r.id == id)
        {
            *width = r.width;
            *height = r.height;
            return true;
        }
    return false;
}

/* Display mode lists (Display Manager 2.0): every resolution, at every
   depth the screen has. Warcraft II reads it to check for a 640x480
   256-colour display and refuses to run without one; the Monitors control
   panel lists the resolutions. The records are laid out as in Apple's
   Video.h and Displays.h. */
namespace
{
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

/* One resolution's entry and everything it points at. */
struct ModeEntry { GUEST_STRUCT;
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
/* A list: its count, then the entries. */
struct ModeList { GUEST_STRUCT;
    GUEST<uint32_t> count;
};

void fill_entry(ModeEntry *m, const DisplayResolution &r)
{
    int n = 0, current = 0;
    for(int d : { 1, 2, 4, 8, 16, 32 })
    {
        if(!vdriver->isAcceptableMode(0, 0, d, false))
            continue;
        if(d == vdriver->bpp())
            current = n;
        VDSwitchInfoRec& sw = m->depthSwitch[n];
        sw.csMode = firstDepthMode + n;
        sw.csData = r.id;
        VPBlock& vp = m->vp[n];
        vp.vpRowBytes = (r.width * d + 15) / 16 * 2;
        vp.vpBounds.bottom = r.height;
        vp.vpBounds.right = r.width;
        vp.vpHRes = vp.vpVRes = 72 << 16;
        vp.vpPixelType = d > 8 ? RGBDirect : 0;
        vp.vpPixelSize = d;
        vp.vpCmpCount = d > 8 ? 3 : 1;
        vp.vpCmpSize = d == 16 ? 5 : d == 32 ? 8 : d;
        m->depths[n].depthSwitchInfo = (Ptr)&sw;
        m->depths[n].depthVPBlock = (Ptr)&vp;
        n++;
    }

    m->switchInfo.csMode = firstDepthMode + current;
    m->switchInfo.csData = r.id;
    if(r.id == current_resolution())
        m->switchInfo.csBaseAddr = PIXMAP_BASEADDR(GD_PMAP(LM(MainDevice)));
    m->resolution.csDisplayModeID = r.id;
    m->resolution.csHorizontalPixels = r.width;
    m->resolution.csVerticalLines = r.height;
    m->resolution.csRefreshRate = 67 << 16;
    m->resolution.csMaxDepthMode = firstDepthMode + n - 1;
    m->timing.csTimingMode = r.id;
    m->timing.csTimingFormat = "decl"_4;
    m->timing.csTimingData = 130;                   /* timingApple_640x480_67hz */
    m->timing.csTimingFlags = r.id == current_resolution() ? 0xF : 0x7;  /* valid, safe, default, (shown now) */
    m->depthBlock.depthBlockCount = n;
    m->depthBlock.depthVPBlock = (Ptr)m->depths;
    snprintf((char *)m->name + 1, sizeof m->name - 1, "%d x %d", r.width, r.height);
    m->name[0] = strlen((char *)m->name + 1);

    DMDisplayModeListEntryRec& e = m->entry;
    e.displayModeSwitchInfo = (Ptr)&m->switchInfo;
    e.displayModeResolutionInfo = (Ptr)&m->resolution;
    e.displayModeTimingInfo = (Ptr)&m->timing;
    e.displayModeDepthBlockInfo = (Ptr)&m->depthBlock;
    e.displayModeVersion = 1;                       /* kDisplayModeEntryVersionOne */
    e.displayModeName = (Ptr)m->name;
    e.displayModeDisplayInfo = (Ptr)m->displayInfo;
}
}

OSErr Executor::C_DMNewDisplayModeList(uint32_t displayID, uint32_t modeListFlags, uint32_t reserved,
                                       GUEST<uint32_t> *thePanelCount, GUEST<Ptr> *thePanelList)
{
    if(displayID != mainDisplayID)
        return kDMNotFoundErr;
    std::vector<DisplayResolution> res = ROMlib_dm_resolutions();
    ModeList *l = (ModeList *)NewPtrClear(sizeof(ModeList) + res.size() * sizeof(ModeEntry));
    if(!l)
        return MemError();
    ModeEntry *entries = (ModeEntry *)(l + 1);
    for(size_t i = 0; i < res.size(); i++)
        fill_entry(&entries[i], res[i]);
    l->count = res.size();

    *thePanelCount = res.size();
    *thePanelList = (Ptr)l;
    return noErr;
}

OSErr Executor::C_DMGetIndexedDisplayModeFromList(Ptr panelList, uint32_t itemIndex, uint32_t reserved,
                                                  DMDisplayModeListIteratorUPP listIterator, void *userData)
{
    ModeList *l = (ModeList *)panelList;
    if(!l || itemIndex >= l->count)
        return kDMNotFoundErr;
    ModeEntry *entries = (ModeEntry *)(l + 1);
    if(listIterator)
        listIterator(userData, itemIndex, (Ptr)&entries[itemIndex].entry);
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
    sw->csData = current_resolution();
    sw->csPage = 0;
    sw->csBaseAddr = PIXMAP_BASEADDR(GD_PMAP(LM(MainDevice)));
    sw->csReserved = 0;
    return noErr;
}

OSErr Executor::C_DMCheckDisplayMode(GDHandle theDevice, uint32_t mode, uint32_t depthMode,
                                     GUEST<uint32_t> *switchFlags, uint32_t reserved, Boolean *modeOk)
{
    int w, h;
    *switchFlags = 0;
    *modeOk = theDevice == LM(MainDevice) && resolution_size(mode, &w, &h);
    return noErr;
}

/* ---- the change ------------------------------------------------------ */

/* The 'dold' or 'dnew' record of the 'cnfg' event, as 7.6.1 and 8.6 make
   them (tests/guest/dmprobe/ref): the display, its device, its pixmap. */
static OSErr put_long(GUEST<AERecord> *rec, OSType key, DescType type, int32_t v, Size size = 4)
{
    GUEST<int32_t> g = v;
    AEDesc d;
    OSErr err = AECreateDesc(type, &g, size, &d);
    if(err == noErr)
    {
        err = AEPutParamDesc(rec, key, &d);
        AEDisposeDesc(&d);
    }
    return err;
}

static OSErr put_short(GUEST<AERecord> *rec, OSType key, int16_t v)
{
    GUEST<int16_t> g = v;
    AEDesc d;
    OSErr err = AECreateDesc("shor"_4, &g, 2, &d);
    if(err == noErr)
    {
        err = AEPutParamDesc(rec, key, &d);
        AEDisposeDesc(&d);
    }
    return err;
}

static OSErr put_rect(GUEST<AERecord> *rec, OSType key, const Rect &r)
{
    AEDesc d;
    OSErr err = AECreateDesc("qdrt"_4, &r, sizeof r, &d);
    if(err == noErr)
    {
        err = AEPutParamDesc(rec, key, &d);
        AEDisposeDesc(&d);
    }
    return err;
}

static OSErr config_record(const ScreenState &st, GUEST<AERecord> *rec)
{
    OSErr err = AECreateList(nullptr, 0, true, rec);
    if(err != noErr)
        return err;
    GDHandle gd = LM(MainDevice);
    PixMapHandle pm = GD_PMAP(gd);
    int d = st.depth;
    bool eight = (uint16_t)LM(SysVersion) >= 0x0800;
    put_long(rec, keyDMConfigVersion, "long"_4, 0);
    put_long(rec, keyDMConfigFlags, "long"_4, 0);
    put_long(rec, keyDMConfigReserved, "long"_4, 0);
    put_long(rec, keyDisplayID, "long"_4, mainDisplayID);
    put_long(rec, keyDisplayComponent, "long"_4, 0);                     /* no display component */
    put_long(rec, keyDisplayDevice, "long"_4, US_TO_SYN68K((void *)gd));
    put_long(rec, keyDisplayFlags, "long"_4, eight ? 0x14 : 0x05);
    put_long(rec, keyDisplayMode, "long"_4, st.resolution);
    put_long(rec, keyDisplayModeReserved, "long"_4, 0);
    put_long(rec, keyDisplayReserved, "long"_4, 0);
    put_short(rec, keyDeviceFlags, GD_FLAGS(gd));
    put_long(rec, keyDeviceDepthMode, "long"_4, current_depth_mode());
    put_rect(rec, keyDeviceRect, st.bounds);
    put_rect(rec, keyPixMapRect, st.bounds);
    put_long(rec, keyPixMapHResolution, "fixd"_4, 72 << 16);
    put_long(rec, keyPixMapVResolution, "fixd"_4, 72 << 16);
    put_short(rec, keyPixMapPixelType, d > 8 ? RGBDirect : 0);
    put_short(rec, keyPixMapPixelSize, d);
    put_short(rec, keyPixMapCmpCount, d > 8 ? 3 : 1);
    put_short(rec, keyPixMapCmpSize, d == 16 ? 5 : d == 32 ? 8 : d);
    put_long(rec, keyPixMapReserved, "long"_4, 0, 2);                    /* (2 bytes, as on the Mac) */
    CTabHandle ct = PIXMAP_TABLE(pm);
    put_long(rec, keyPixMapColorTableSeed, "long"_4, ct && *ct ? (int32_t)CTAB_SEED(ct) : 0);
    return noErr;
}

/* 'aevt'/'cnfg': 'dspl' { display ID: { 'dold': ..., 'dnew': ... } } */
static OSErr config_event(const ScreenState &from, const ScreenState &to, GUEST<AppleEvent> *ae)
{
    ProcessSerialNumber me = { 0, kCurrentProcess };
    GUEST<AEAddressDesc> target;
    OSErr err = AECreateDesc(typeProcessSerialNumber, &me, sizeof me, &target);
    if(err != noErr)
        return err;
    err = AECreateAppleEvent(kCoreEventClass, kAESystemConfigNotice, &target, kAutoGenerateReturnID,
                             kAnyTransactionID, ae);
    AEDisposeDesc(&target);
    if(err != noErr)
        return err;
    GUEST<AERecord> dold, dnew, display, dspl;
    config_record(from, &dold);
    config_record(to, &dnew);
    AECreateList(nullptr, 0, true, &display);
    AEPutParamDesc(&display, keyDisplayOldConfig, &dold);
    AEPutParamDesc(&display, keyDisplayNewConfig, &dnew);
    AECreateList(nullptr, 0, true, &dspl);
    AEPutParamDesc(&dspl, mainDisplayID, &display);
    err = AEPutParamDesc(ae, kAEDisplayNotice, &dspl);
    AEDisposeDesc(&dold);
    AEDisposeDesc(&dnew);
    AEDisposeDesc(&display);
    AEDisposeDesc(&dspl);
    return err;
}

static ScreenState screen_now()
{
    return { GD_RECT(LM(MainDevice)), current_resolution(), vdriver->bpp() };
}

/* The screen before the first change, the one a process registering a
   notify proc has seen. */
namespace
{
void start_history()
{
    if(history.empty())
        history.push_back(screen_now());
}
}

/* A notify proc that is PowerPC code (a routine descriptor with a
   PowerPC routine: 8.6's Finder): its transition vector. Called with
   callPPC, which keeps the registers of the PowerPC code in the system
   call this runs in (WaitNextEvent); entered through the descriptor from
   68k, it came back to address 0. */
static uint32_t ppc_routine(void *proc)
{
    auto *d = (RoutineDescriptor *)proc;
    if(!d || d->goMixedModeTrap != MIXED_MODE_TRAP)
        return 0;
    for(int i = 0; i <= d->routineCount; i++)
    {
        RoutineRecord &r = d->routineRecords[i];
        if((r.ISA & 0xF) == kPowerPCISA)
            return guest_cast<uint32_t>(r.procDescriptor.get());
    }
    return 0;
}

/* The PowerPC routine behind tv, on a frame below both stacks: at an
   event call from PowerPC code (8.6's Finder in WaitNextEvent, through
   the 68k trap) callPPC put its frame below the 68k stack only, over the
   Finder's own PowerPC frames just below it, and the Finder returned to 0. */
static void call_ppc_below(uint32_t tv, std::initializer_list<uint32_t> args)
{
    PowerCore &cpu = getPowerCore();
    uint32_t r1 = cpu.r[1], a7 = EM_A7;
    uint32_t top = (ppcInCall() && r1 < a7 ? r1 : a7) - 256;
    callPPCOnStack(tv, top & ~15u, args);
    cpu.r[1] = r1;
    EM_A7 = a7;
}

/* A 68k proc, the same: 8.6 at an event call from the PowerPC Finder has
   the Finder's PowerPC frames just below A7, and a 68k extension's proc
   pushing there sent the Finder to address 0 when its call returned. */
template<typename F>
static void call_68k_below(F f)
{
    uint32_t r1 = getPowerCore().r[1], a7 = EM_A7;
    if(ppcInCall() && r1 < a7)
        EM_A7 = (r1 - 256) & ~3u;
    f();
    EM_A7 = a7;
}

static void call_notify(const NotifyProc &n, INTEGER message, AppleEvent *ae)
{
    uint32_t tv = ppc_routine(n.proc);
    uint32_t ae_addr = ae ? US_TO_SYN68K(ae) : 0;
    if(n.extended)
    {
        if(tv)
            call_ppc_below(tv, { US_TO_SYN68K(n.user_data), (uint32_t)(int32_t)message, ae_addr });
        else
            call_68k_below([&] { DMExtendedNotificationUPP(n.proc)(n.user_data, message, ae); });
    }
    else
    {
        if(tv)
            call_ppc_below(tv, { ae_addr });
        else
            call_68k_below([&] { DMNotificationUPP(n.proc)(ae); });
    }
}

/* This process's part of the changes it hasn't seen: its QuickDraw
   globals, its menu bar if it is in front, its notify procs. */
static void catch_up(bool changer = false)
{
    if(seen_changes >= (int32_t)history.size())
        return;
    ScreenState from = history[std::max<int32_t>(seen_changes - 1, 0)];
    seen_changes = history.size();
    if(configuring)
        return;

    if(LM(QDExist) == EXIST_YES)
    {
        PixMapHandle pm = GD_PMAP(LM(MainDevice));
        qdGlobals().screenBits.baseAddr = PIXMAP_BASEADDR(pm);
        qdGlobals().screenBits.rowBytes = PIXMAP_ROWBYTES(pm) / PIXMAP_PIXEL_SIZE(pm);
        qdGlobals().screenBits.bounds = PIXMAP_BOUNDS(pm);
    }
    /* the menu bar's layout (the menus at its right end) for the new
       width; drawn by the process in front */
    if(LM(WWExist) == EXIST_YES && LM(MenuList))
    {
        ROMlib_mbdfcall(mbCalc, 0, 0);
        if(ROMlib_process_is_front())
            DrawMenuBar();
    }

    ProcessSerialNumber me;
    if(!current_process(&me))
        return;
    /* its own procs; the System's (extensions register for
       kSystemProcess: the Control Strip) run in the process that changed
       the mode, before DMSetDisplayMode returns */
    const ProcessSerialNumber system_psn = { 0, kSystemProcess };
    std::vector<NotifyProc> mine;
    for(const NotifyProc &n : notify_procs)
        if(same_psn(n.psn, me) || (changer && same_psn(n.psn, system_psn)))
            mine.push_back(n);
    if(mine.empty())
        return;
    GUEST<AppleEvent> ae;
    if(config_event(from, screen_now(), &ae) != noErr)
        return;
    for(const NotifyProc &n : mine)
        if(n.extended)
        {
            call_notify(n, kDMNotifyPrep, nullptr);
            call_notify(n, kDMNotifyDependents, &ae);
            call_notify(n, kDMNotifyEvent, &ae);
        }
    for(const NotifyProc &n : mine)
        if(!n.extended)
            call_notify(n, 0, &ae);
    AEDisposeDesc(&ae);
}

/* The rest of a change, once the GDevice has its new size: the desktop,
   the windows, the screen repainted, then this process's part. */
static void finish_change(const Rect &old_screen)
{
    Rect now = GD_RECT(LM(MainDevice));
    if(LM(WWExist) == EXIST_YES)
    {
        ROMLib_InitGrayRgn();
        ROMlib_layers_screen_changed(old_screen, now);
        ROMlib_rootless_update();
    }
    history.push_back(screen_now());
    catch_up(true);
    /* all of it converted again, whatever the repaint reported (setMode's
       mark can go out with a render before the repaint, on native) */
    vdriver->updateScreen(now.top, now.left, now.bottom, now.right);
}

bool Executor::ROMlib_dm_set_screen_size(int width, int height)
{
    if(!size_acceptable(width, height))
        return false;
    start_history();
    Rect old_screen = GD_RECT(LM(MainDevice));
    if(width == old_screen.right && height == old_screen.bottom)
        return true;
    if(!Framebuffer::reserve)
        Framebuffer::reserve = (size_t)maxWidth * maxHeight * 4;
    if(!gd_set_screen_size(width, height))
        return false;
    if(configuring)
    {
        if(!configured_change)
            configure_old = old_screen;
        configured_change = true;
        return true;
    }
    finish_change(old_screen);
    return true;
}

void Executor::ROMlib_dm_poll()
{
    int w, h;
    if(!configuring && vdriver->takeSizeRequest(&w, &h))
    {
        /* the front process's menu bar and windows: the request waits for
           it (a background process's event call comes first, often) */
        if(ROMlib_process_is_front())
            ROMlib_dm_set_screen_size(w, h);
        else
            vdriver->requestSize(w, h);
    }
    catch_up();
}

/* Another resolution of the list, or another depth (through SetDepth,
   which takes the list's depth modes). */
OSErr Executor::C_DMSetDisplayMode(GDHandle theDevice, uint32_t mode, GUEST<uint32_t> *depthMode,
                                   uint32_t reserved, Handle displayState)
{
    int w, h;
    if(theDevice != LM(MainDevice) || !resolution_size(mode, &w, &h))
        return kDMNotFoundErr;
    if(mode != current_resolution() && !ROMlib_dm_set_screen_size(w, h))
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

/* A configuration: the GDevice changes at once, the rest at its end
   (measured: screenBits and GrayRgn stay the old ones until then). */
OSErr Executor::C_DMBeginConfigureDisplays(GUEST<Handle> *displayState)
{
    Handle h = NewHandleSysClear(4);
    if(!h)
        return MemError();
    if(!configuring++)
        configured_change = false;
    *displayState = h;
    return noErr;
}

OSErr Executor::C_DMEndConfigureDisplays(Handle displayState)
{
    if(!displayState || !configuring)
        return paramErr;
    DisposeHandle(displayState);
    if(--configuring == 0 && configured_change)
    {
        configured_change = false;
        finish_change(configure_old);
    }
    return noErr;
}

/* One screen: always enabled and main, at (0, 0), mirroring nothing.
   Moving it would move the desktop's origin, which nothing does here. */
OSErr Executor::C_DMEnableDisplay(GDHandle enableDevice, Handle displayState)
{
    return enableDevice == LM(MainDevice) ? noErr : paramErr;
}

OSErr Executor::C_DMSetMainDisplay(GDHandle newMainDevice, Handle displayState)
{
    return newMainDevice == LM(MainDevice) ? noErr : paramErr;
}

OSErr Executor::C_DMGetNextMirroredDevice(GDHandle gDevice, GUEST<GDHandle> *mirroredDevice)
{
    *mirroredDevice = nullptr;
    return noErr;
}

OSErr Executor::C_DMMoveDisplay(GDHandle moveDevice, INTEGER x, INTEGER y, Handle displayState)
{
    return moveDevice == LM(MainDevice) ? noErr : paramErr;
}

/* MacPhoenix: one screen, no mirroring, answered as a 7.6.1 Quadra does
   (measured: a probe on the ROM machine) */
OSErr Executor::C_DMQDIsMirroringCapable(Boolean *qdIsMirroringCapable)
{
    if(qdIsMirroringCapable)
        *qdIsMirroringCapable = false;
    return noErr;
}

OSErr Executor::C_DMCanMirrorNow(Boolean *canMirrorNow)
{
    (void)canMirrorNow;
    return -6226;
}

OSErr Executor::C_DMIsMirroringOn(Boolean *isMirroringOn)
{
    *isMirroringOn = false;
    return noErr;
}

/* The screen's name for display ID 1; with kDMForceNumbersMask (2) numbered
   as when there are several. */
OSErr Executor::C_DMGetNameByAVID(uint32_t theID, uint32_t nameFlags, StringPtr name)
{
    if(theID != mainDisplayID)
        return kDMNotFoundErr;
    const char *s = (nameFlags & 2) ? "Display 1" : "Display";
    name[0] = strlen(s);
    memcpy(name + 1, s, name[0]);
    return noErr;
}

/* The screen's settings are never changed, so there is nothing to save. */
OSErr Executor::C_DMSaveScreenPrefs(uint32_t reserved1, uint32_t saveFlags, uint32_t reserved2)
{
    return noErr;
}

OSErr Executor::C_DMNewAVIDByPortComponent(Component thePortComponent, OSType portKind,
                                            uint32_t reserved, GUEST<uint32_t> *newID)
{
    if(newID)
        *newID = 0;
    return kDMGenErr; /* no display ports */
}

/* No AV ports or devices: nothing has an AVID. */
OSErr Executor::C_DMNewAVIDByDeviceComponent(Component theDeviceComponent, OSType portKind,
                                              uint32_t reserved, GUEST<uint32_t> *newID)
{
    if(newID)
        *newID = 0;
    return kDMGenErr;
}

OSErr Executor::C_DMGetPortComponentByAVID(uint32_t displayID, GUEST<Component> *portComponent,
                                            Ptr portDesc, GUEST<OSType> *portKind)
{
    return kDMGenErr;
}

OSErr Executor::C_DMGetDeviceComponentByAVID(uint32_t displayID, GUEST<Component> *theDeviceComponent,
                                              Ptr theDesc, GUEST<OSType> *theKind)
{
    return kDMGenErr;
}

OSErr Executor::C_DMGetDeviceAVIDByPortAVID(uint32_t portAVID, GUEST<uint32_t> *deviceAVID)
{
    return kDMGenErr;
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

/* MacPhoenix: the desktop painted, not made an update: the 8.x Finder calls
   it while it updates its desktop window, which then never stopped getting
   update events (and its Apple events waited behind them). Painted only
   where the desktop layer may draw: inside that update the Finder erases
   each icon's whole label rectangle with DMDrawDesktopRect, then redraws
   the label clipped to the update; a label partly outside it lost the rest
   (left wiped where an application's windows had uncovered the desktop). */
void Executor::C_DMDrawDesktopRegion(RgnHandle globalRgn)
{
    RgnHandle rgn = NewRgn();
    CopyRgn(globalRgn, rgn);
    if(RgnHandle vis = ROMlib_desktop_layer_vis_region())
        SectRgn(rgn, vis, rgn);
    ROMlib_painting_desktop_only = true;
    PaintOne(nullptr, rgn);
    ROMlib_painting_desktop_only = false;
    DisposeRgn(rgn);
}

void Executor::C_DMDrawDesktopRect(Rect *globalRect)
{
    RgnHandle rgn = NewRgn();
    RectRgn(rgn, globalRect);
    DMDrawDesktopRegion(rgn);
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

/* The bounds the screen has in a mode of the list. */
OSErr Executor::C_DMGetModeBounds(GDHandle theDevice, Ptr switchInfo, uint32_t depthMode,
                                  Ptr result1, Rect *bounds)
{
    if(theDevice != LM(MainDevice))
        return kDMNotFoundErr;
    int w = vdriver->width(), h = vdriver->height();
    if(switchInfo && !resolution_size(((VDSwitchInfoRec *)switchInfo)->csData, &w, &h))
        return kDMNotFoundErr;
    *bounds = Rect{ 0, 0, (INTEGER)h, (INTEGER)w };
    return noErr;
}

/* Measured on the Quadra (7.6.1) and the G3 (8.6), 1024x768 or larger to
   640x480 (tests/guest/dmprobe, and SimpleText's window dragged about): a
   window stays where it is while its whole title bar is in the new screen's
   height and some of it in its width; otherwise its title bar's bottom
   comes up to the screen's bottom edge, and it moves left by as much as its
   part on the old screen ran past the new screen's right edge. */
Point Executor::ROMlib_dm_window_offset(const Rect &s, INTEGER content_top,
                                        const Rect &old_screen, const Rect &new_screen)
{
    enum { minVisible = 32 };   /* of the title bar's width, to drag it by */
    int title_bottom = std::max<int>(content_top, s.top + 1);
    int overlap = std::min(s.right, new_screen.right) - std::max(s.left, new_screen.left);
    bool fits = s.top >= new_screen.top && title_bottom <= new_screen.bottom
        && overlap >= std::min<int>(minVisible, s.right - s.left);
    Point d = { 0, 0 };
    /* no title bar (the Control Strip, a palette's frame): all of it */
    if(content_top - s.top < 4)
    {
        fits = s.bottom <= new_screen.bottom && s.right <= new_screen.right;
        title_bottom = s.bottom;
    }
    if(fits)
        return d;
    if(title_bottom > new_screen.bottom)
        d.v = new_screen.bottom - title_bottom;
    int right = std::min(s.right, old_screen.right);
    if(right > new_screen.right)
        d.h = new_screen.right - right;
    if(s.left + d.h < new_screen.left && s.left >= new_screen.left)
        d.h = new_screen.left - s.left;  /* not off the left edge for it */
    return d;
}
