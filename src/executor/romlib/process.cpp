/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <ProcessMgr.h>
#include <ResourceMgr.h>
#include <MemoryMgr.h>
#include <ToolboxEvent.h>

#include <mman/mman.h>
#include <rsys/process.h>
#include <ScrapMgr.h>
#include <wind/wind.h>
#include <quickdraw/cquick.h>
#include <WindowMgr.h>
#include <QuickDraw.h>
#include <vector>
#include <algorithm>

using namespace Executor;

#define declare_handle_type(type_prefix)        \
    typedef type_prefix##_t *type_prefix##_ptr; \
    typedef GUEST<type_prefix##_ptr> *type_prefix##_handle

declare_handle_type(size_resource);

#define SIZE_FLAGS(size) ((*size)->flags)

static size_resource_handle
get_size_resource()
{
    Handle size;

    size = Get1Resource("SIZE"_4, 0);
    if(size == nullptr)
        size = Get1Resource("SIZE"_4, -1);
    return (size_resource_handle)size;
}

/* ── Partitions ─────────────────────────────────────────────────────────
 * Slice 1 of our Process Manager: one process, laid out the way 7.5.5
 * lays out each application (measured on a real boot, see
 * docs/executor/MEMORY_MAP.md):
 *
 *   Process Manager heap: SysZone end .. BufPtr
 *     partition: a locked handle at the top, SIZE preferred size + 16K
 *       ApplZone          at the partition start, bkLim = ApplLimit - 24
 *       ApplLimit         = CurStackBase - DefltStack
 *       stack             grows down from CurStackBase
 *       A5 globals        belowA5 bytes, from CurStackBase up to CurrentA5
 *       A5 world above A5 abovea5 bytes, to the partition top
 *
 *   MemTop reads as SysZone's bkLim + the partition size: an
 *   application sees a machine as big as its partition.
 */
THz Executor::ROMlib_pm_zone;
static Handle partition;
static uint32_t partition_size;

/* Measured on 7.5.5: a partition is the SIZE preferred size plus 16K. */
static const uint32_t partition_extra = 16 * 1024;
/* No SIZE resource: Process Manager's default. */
static const uint32_t default_partition = 512 * 1024;

void Executor::process_reset_heap(THz pm_zone)
{
    ROMlib_pm_zone = pm_zone;
    partition = nullptr;
    partition_size = 0;
}

void Executor::process_layout_partition(ConstStringPtr app_name)
{
    uint32_t preferred = default_partition;
    uint32_t minimum = default_partition;
    int32_t abovea5 = 4, belowa5 = 0; /* no CODE 0 (PowerPC): A5 at the top */

    SetZone(ROMlib_pm_zone);
    if(partition)
    {
        /* Chain: the old partition goes, the heap stays. */
        HUnlock(partition);
        DisposeHandle(partition);
        partition = nullptr;
    }

    /* Peek at SIZE and CODE 0 before the partition exists, so the
       application's resource map can be opened inside it afterwards. */
    INTEGER rn = OpenResFile(app_name);
    if(rn != -1)
    {
        if(size_resource_handle size = get_size_resource())
        {
            preferred = (*size)->pref_size;
            minimum = (*size)->min_size;
        }
        if(Handle code0 = Get1Resource("CODE"_4, 0))
        {
            auto lp = (GUEST<int32_t> *)*code0;
            abovea5 = lp[0];
            belowa5 = lp[1];
        }
        CloseResFile(rn);
    }
    SetZone(ROMlib_pm_zone);

    GUEST<Size> grow;
    Size avail = MaxMem(&grow);
    uint32_t want = preferred + partition_extra;
    if(want > (uint32_t)avail)
        want = minimum + partition_extra;
    if(want > (uint32_t)avail)
    {
        warning_unexpected("partition: %u wanted, %d available", want, (int)avail);
        want = avail & ~3;
    }

    /* The Process Manager hands out partitions from the top of its heap.
       Claim the space below first, so the partition lands at the top. */
    Ptr below = nullptr;
    if((uint32_t)avail > want + 64)
        below = NewPtr(avail - want - 64);
    partition = NewHandle(want);
    if(below)
        DisposePtr(below);
    if(!partition)
        gui_fatal("unable to allocate a %u byte application partition", want);
    HLock(partition);
    partition_size = want;

    Ptr p = *partition;
    Ptr top = p + want;
    LM(CurrentA5) = top - abovea5;
    LM(CurStackBase) = LM(CurrentA5) - belowa5;
    LM(ApplLimit) = LM(CurStackBase) - (int32_t)LM(DefltStack);
    LM(ApplZone) = (THz)p;
    /* Executor keeps HeapEnd 8 above bkLim; this puts bkLim at
       ApplLimit - 24, as 7.5.5 does. */
    LM(HeapEnd) = LM(ApplLimit) - 16;
    InitZone(nullptr, 64, LM(HeapEnd) + 12, (THz)p);
    LM(MemTop) = (Ptr)LM(SysZone)->bkLim + want;
}

#define PSN_EQ_P(psn0, psn1)                      \
    ((psn0).highLongOfPSN == (psn1).highLongOfPSN \
     && (psn0).lowLongOfPSN == (psn1).lowLongOfPSN)

#define PROCESS_INFO_SERIAL_NUMBER(info) ((info)->processNumber)
#define PROCESS_INFO_LAUNCHER(info) ((info)->processLauncher)

#define PROCESS_INFO_LENGTH(info) ((info)->processInfoLength)
#define PROCESS_INFO_NAME(info) ((info)->processName)
#define PROCESS_INFO_TYPE(info) ((info)->processType)
#define PROCESS_INFO_SIGNATURE(info) ((info)->processSignature)
#define PROCESS_INFO_MODE(info) ((info)->processMode)
#define PROCESS_INFO_LOCATION(info) ((info)->processLocation)
#define PROCESS_INFO_SIZE(info) ((info)->processSize)
#define PROCESS_INFO_FREE_MEM(info) ((info)->processFreeMem)
#define PROCESS_INFO_LAUNCH_DATE(info) ((info)->processLaunchDate)
#define PROCESS_INFO_ACTIVE_TIME(info) ((info)->processActiveTime)



typedef struct process_info
{
    struct process_info *next;

    uint32_t mode;
    uint32_t type;
    uint32_t signature;
    uint32_t size;
    uint32_t launch_ticks;

    ProcessSerialNumber serial_number;
} process_info_t;

static process_info_t *process_info_list;
static process_info_t *current_process_info;

static const int default_process_mode_flags = 0;

/* ### not currently used */
static ProcessSerialNumber system_process = { 0, kSystemProcess };
static ProcessSerialNumber no_process = { 0, kNoProcess };
static ProcessSerialNumber current_process = { 0, kCurrentProcess };

void Executor::process_create(bool desk_accessory_p,
                              uint32_t type, uint32_t signature)
{
    size_resource_handle size;
    process_info_t *info;
    static uint32_t next_free_psn = 4;

    size = get_size_resource();

    {
        TheZoneGuard guard(LM(SysZone));

        info = (process_info_t *)NewPtr(sizeof *info);
    }

    /* ### we are seriously fucked */
    if(info == nullptr)
        gui_fatal("unable to allocate process info record");

    info->mode = ((size
                       ? toHost(SIZE_FLAGS(size))
                       : default_process_mode_flags)
                  | (desk_accessory_p
                         ? modeDeskAccessory
                         : 0));
    info->type = type;
    info->signature = signature;

    info->size = partition_size ? partition_size : zone_size(LM(ApplZone));
    info->launch_ticks = TickCount();

    info->serial_number.highLongOfPSN = -1;
    info->serial_number.lowLongOfPSN = next_free_psn++;

    info->next = process_info_list;
    process_info_list = info;

    /* ### hack */
    current_process_info = info;

    /* MacPhoenix: the Process Manager hands every application an initialised
       desk scrap (Finder 7.5.5 dereferences ScrapHandle without checking
       ScrapState). */
    if(LM(ScrapState) < 0)
        ZeroScrap();
}

process_info_t *
get_process_info(ProcessSerialNumber *serial_number)
{
    process_info_t *t;

    if(PSN_EQ_P(*serial_number, current_process))
        return current_process_info;

    for(t = process_info_list; t; t = t->next)
    {
        if(PSN_EQ_P(*serial_number, t->serial_number))
            return t;
    }
    return nullptr;
}

OSErr Executor::C_GetCurrentProcess(ProcessSerialNumber *serial_number)
{
    *serial_number = current_process_info->serial_number;
    return noErr;
}

OSErr Executor::C_GetNextProcess(ProcessSerialNumber *serial_number)
{
    process_info_t *t;

    if(PSN_EQ_P(*serial_number, no_process))
    {
        if(!process_info_list)
            return procNotFound;
        else
        {
            *serial_number = process_info_list->serial_number;
            return noErr;
        }
    }

    t = get_process_info(serial_number);
    if(t == nullptr)
        return paramErr;
    else if(t->next == nullptr)
    {
        memset(serial_number, 0, sizeof *serial_number);
        return procNotFound;
    }
    else
    {
        *serial_number = t->next->serial_number;
        return noErr;
    }
}

OSErr Executor::C_GetProcessInformation(ProcessSerialNumber *serial_number,
                                        ProcessInfoPtr process_info)
{
    process_info_t *info;
    int32_t current_ticks;

    info = get_process_info(serial_number);
    if(info == nullptr
       || PROCESS_INFO_LENGTH(process_info) != sizeof *process_info)
        return paramErr;

    PROCESS_INFO_SERIAL_NUMBER(process_info) = info->serial_number;
    PROCESS_INFO_TYPE(process_info) = info->type;
    PROCESS_INFO_SIGNATURE(process_info) = info->signature;
    PROCESS_INFO_MODE(process_info) = info->mode;
    PROCESS_INFO_LOCATION(process_info) = guest_cast<Ptr>(LM(ApplZone));
    PROCESS_INFO_SIZE(process_info) = info->size;

    /* ### set current zone to applzone? */
    PROCESS_INFO_FREE_MEM(process_info) = FreeMem();

    PROCESS_INFO_LAUNCHER(process_info) = no_process;

    PROCESS_INFO_LAUNCH_DATE(process_info) = info->launch_ticks;
    current_ticks = TickCount();
    PROCESS_INFO_ACTIVE_TIME(process_info)
        = current_ticks - info->launch_ticks;

    return noErr;
}

OSErr Executor::C_SameProcess(ProcessSerialNumber *serial_number0,
                              ProcessSerialNumber *serial_number1,
                              Boolean *same_out)
{
    process_info_t *info0, *info1;

    info0 = get_process_info(serial_number0);
    info1 = get_process_info(serial_number1);

    if(info0 == nullptr
       || info1 == nullptr)
        return paramErr;

    *same_out = (info0 == info1);
    return noErr;
}

OSErr Executor::C_GetFrontProcess(ProcessSerialNumber *serial_number, int32_t dummy)
{
    *serial_number = current_process_info->serial_number;
    return noErr;
}

OSErr Executor::C_SetFrontProcess(ProcessSerialNumber *serial_number)
{
    warning_unimplemented("");
    return paramErr;
}

OSErr Executor::C_WakeUpProcess(ProcessSerialNumber *serial_number)
{
    warning_unimplemented("");
    return paramErr;
}

OSErr Executor::C_GetProcessSerialNumberFromPortName(
    PPCPortPtr port_name, ProcessSerialNumber *serial_number)
{
    warning_unimplemented("");
    return paramErr;
}

OSErr Executor::C_GetPortNameFromProcessSerialNumber(
    PPCPortPtr port_name, ProcessSerialNumber *serial_number)
{
    warning_unimplemented("");
    return paramErr;
}

/* ### temp memory spew; these go elsewhere */

/* ### launch/da spew; these go elsewhere */

/* ── Private OSDispatch selectors Finder 7.5.5 uses ─────────────────────
 * Single process for now: per-process state lives here until the Process
 * Manager keeps real records (slice 2). Names are guesses (learned.yaml). */

static WindowPtr desktop_layer;

OSErr Executor::C_TakeDesktopLayer(GUEST<WindowPtr> *layer)
{
    if(desktop_layer)
        return -603; /* protocolErr; not in multiversal */

    /* Like the real one: a GrafPort over the whole screen in a window record,
       visRgn the desktop, clipRgn wide open, updateRgn empty, no strucRgn or
       contRgn. Finder takes visRgn as its desktop window's visRgn and contRgn,
       and updateRgn as its updateRgn. */
    GUEST<GrafPtr> save_port = qdGlobals().thePort;
    {
        TheZoneGuard guard(LM(SysZone));
        desktop_layer = (WindowPtr)NewPtrClear(sizeof(WindowRecord));
        OpenPort(desktop_layer);
        CopyRgn(LM(GrayRgn), PORT_VIS_REGION(desktop_layer));
        WINDOW_UPDATE_REGION(desktop_layer) = NewRgn();
    }
    qdGlobals().thePort = save_port;
    if(layer)
        *layer = desktop_layer;
    return noErr;
}

struct ProcessCallback
{
    Ptr proc;
    int32_t refCon;
};
static std::vector<ProcessCallback> process_callbacks;

OSErr Executor::C_AddProcessCallback(Ptr proc, int32_t refCon)
{
    process_callbacks.push_back({ proc, refCon });
    return noErr;
}

OSErr Executor::C_GetTempMemInfo(GUEST<int32_t> *freeBytes, GUEST<int32_t> *maxBlock)
{
    if(freeBytes)
        *freeBytes = TempFreeMem();
    if(maxBlock)
    {
        GUEST<Size> grow;
        *maxBlock = TempMaxMem(&grow);
    }
    return noErr;
}

static int32_t process_drag_hooks;

OSErr Executor::C_SetProcessDragHooks(int32_t hooks)
{
    process_drag_hooks = hooks;
    return noErr;
}

/* The Apple menu's items as Finder hands them over; drawing the Apple menu
   from them is Process Manager work still to come. */
struct AppleMenuItem
{
    int32_t key;
    Ptr item;
    Handle iconSuite;
};
static std::vector<AppleMenuItem> apple_menu_items;

OSErr Executor::C_AddAppleMenuItem(int16_t flags, Handle iconSuite, int16_t reserved,
                                   Ptr item, Ptr key)
{
    (void)flags;
    (void)reserved;
    if(!key)
        return paramErr;
    apple_menu_items.push_back({ (int32_t)US_TO_SYN68K(key), item, iconSuite });
    return noErr;
}

OSErr Executor::C_RemoveAppleMenuItems(int32_t key)
{
    auto n = apple_menu_items.size();
    apple_menu_items.erase(std::remove_if(apple_menu_items.begin(), apple_menu_items.end(),
                                          [key](const AppleMenuItem& i) { return !key || i.key == key; }),
                           apple_menu_items.end());
    return (key && n == apple_menu_items.size()) ? paramErr : noErr;
}
