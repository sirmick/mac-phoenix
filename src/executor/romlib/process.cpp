/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <menu/menu.h>

#include <ProcessMgr.h>
#include <SegmentLdr.h>
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
#include <AppleEvents.h>
#include <IntlUtil.h>
#include <OSEvent.h>
#include <TimeMgr.h>
#include <VRetraceMgr.h>
#include <rsys/launch.h>
#include <rsys/desk.h>
#include <rsys/component.h>
#include "appleevent/apple_events.h"
#include <osevent/osevent.h>
#include <res/resource.h>
#include <syn68k_public.h>
#include <base/trapglue.h>

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <ucontext.h>


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

/* ── Processes ──────────────────────────────────────────────────────────
 * Our Process Manager.  Every process but the first (the one Executor
 * boots, normally Finder) runs on its own host thread, so its C++ frames
 * (a trap that called back into guest code that called WaitNextEvent...)
 * stay on its own stack while others run.  Only one thread runs at a time:
 * the one holding the baton (pm_running).  A switch saves the outgoing
 * process's world and installs the incoming one's:
 *
 *   - the low memory 7.5.5's Process Manager switches: its list is the
 *     System file's 'lmem' -16458 (length.w, address.l pairs), plus
 *     ApplZone and ApplLimit (which it sets itself) and WindowList (the
 *     process's layer);
 *   - the 68k context (registers, interpreter state, nesting depth);
 *   - the trap tables: a process's own patches (Finder patches traps with
 *     code in its partition) only apply while it runs, as on 7.5.5;
 *   - host state modules register (ROMlib_process_register_state) and the
 *     Apple Event Manager's handler tables.
 *
 * As on 7.5.5, LaunchApplication only creates the process; it first runs
 * at the launcher's next event call, which switches to it.  A new process
 * starts from the low memory the first process had before its launch. */

struct process_info
{
    struct process_info *next = nullptr;

    uint32_t mode = 0;
    uint32_t type = 0;
    uint32_t signature = 0;
    uint32_t size = 0;
    uint32_t launch_ticks = 0;

    ProcessSerialNumber serial_number = {};
    ProcessSerialNumber launcher = {};
    Str31 name = { 0 };
    Handle icon = nullptr; /* icon suite in the System heap */

    /* Partition in the Process Manager heap. */
    Handle partition = nullptr;
    uint32_t partition_size = 0;

    enum { fresh, parked, running, dead } state = fresh;
    std::vector<uint8_t> lowmem;
    std::vector<uint8_t> cpu;
    std::vector<uint8_t> host;
    std::vector<syn68k_addr_t> traps;
    AE_zone_tables_h ae_tables = nullptr;       /* Executor's Apple Event Manager */
    AE_zone_tables_h guest_ae_tables = nullptr; /* ExpandMem+$154: the guest's */

    /* Every process but the first: its thread and what it runs. */
    bool own_thread = false;
    QThread *thread = nullptr;
    int stack_slot = -1;
    FSSpec app = {};
    /* A desk accessory's process (LaunchDeskAccessory): app is its file,
       da_name its DRVR (empty: the file's first). */
    bool desk_accessory = false;
    /* Its launcher supplied the opening Apple event (see process_launch). */
    bool posted_open_event = false;
    Str255 da_name = { 0 };

    /* Left by the process that quit and switched to us: screen to redraw,
       and (SIZE acceptAppDiedEvents) whose death to report. */
    RgnHandle uncovered = nullptr;
    bool died_pending = false;
    ProcessSerialNumber died = {};
    /* Brought to the front: what of its windows the layers that were in
       front covered, and suspend/resume to deliver (osEvt message). */
    RgnHandle raised = nullptr;
    bool activate_pending = false;
    bool os_event_pending = false;
    int32_t os_event_message = 0;
};
typedef struct process_info process_info_t;

static process_info_t *process_info_list;
static process_info_t *current_process_info;
static process_info_t *pending_launch;
static process_info_t *pending_front;
/* Layers, front first: whose windows are in front of whose. */
static std::vector<process_info_t *> layer_order;
static process_info_t *front_process;
/* 7.5.5 numbers processes from $2000 (Jigsaw launched by Finder: $2001). */
static uint32_t next_free_psn = 0x2000;

static const int default_process_mode_flags = 0;

static ProcessSerialNumber system_process = { 0, kSystemProcess };
static ProcessSerialNumber no_process = { 0, kNoProcess };
static ProcessSerialNumber current_process = { 0, kCurrentProcess };

#define PSN_EQ_P(psn0, psn1)                      \
    ((psn0).highLongOfPSN == (psn1).highLongOfPSN \
     && (psn0).lowLongOfPSN == (psn1).lowLongOfPSN)

static process_info_t *new_process()
{
    process_info_t *p = new process_info_t;
    p->serial_number.highLongOfPSN = 0;
    p->serial_number.lowLongOfPSN = next_free_psn++;
    p->launch_ticks = TickCount();
    process_info_t **pp = &process_info_list;
    while(*pp)
        pp = &(*pp)->next;
    *pp = p;
    return p;
}

static void forget_process(process_info_t *p)
{
    for(process_info_t **pp = &process_info_list; *pp; pp = &(*pp)->next)
        if(*pp == p)
        {
            *pp = p->next;
            break;
        }
}

/* The first process: Executor's own thread. */
static void process_bootstrap()
{
    if(current_process_info)
        return;
    current_process_info = new_process();
    current_process_info->state = process_info_t::running;
    layer_order.push_back(current_process_info);
    front_process = current_process_info;
}

/* ── The switched world ── */

namespace
{
struct lm_range
{
    uint32_t addr;
    uint16_t len;
};
struct host_var
{
    void *p;
    size_t n;
};
}

static std::vector<lm_range> lm_ranges;
static size_t lm_bytes;
static size_t windowlist_offset; /* of WindowList in a saved world */
static std::vector<uint8_t> lowmem_template;
static std::vector<uint8_t> cpu_template;
static std::vector<syn68k_addr_t> traps_template;

static void save_traps(std::vector<syn68k_addr_t> &buf)
{
    buf.assign(tooltraptable, tooltraptable + NTOOLENTRIES);
    buf.insert(buf.end(), ostraptable, ostraptable + NOSENTRIES);
}

static void restore_traps(const std::vector<syn68k_addr_t> &buf)
{
    std::copy(buf.begin(), buf.begin() + NTOOLENTRIES, tooltraptable);
    std::copy(buf.begin() + NTOOLENTRIES, buf.end(), ostraptable);
}

static std::vector<host_var> &host_vars()
{
    static std::vector<host_var> vars;
    return vars;
}

void Executor::ROMlib_process_register_state(void *p, size_t n)
{
    host_vars().push_back({ p, n });
}

/* 7.5.5's 'lmem' -16458, should the System file lack it. */
static const uint8_t builtin_lmem[] = {
    0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0x00, 0x01, 0x08,
    0x00, 0x08, 0x00, 0x00, 0x01, 0x14, 0x00, 0x01, 0x00, 0x00, 0x01, 0x5c,
    0x00, 0x02, 0x00, 0x00, 0x02, 0x78, 0x00, 0x01, 0x00, 0x00, 0x02, 0xf8,
    0x00, 0x04, 0x00, 0x00, 0x03, 0x16, 0x00, 0x22, 0x00, 0x00, 0x03, 0x1e,
    0x00, 0x08, 0x00, 0x00, 0x08, 0xe0, 0x00, 0xe4, 0x00, 0x00, 0x08, 0xf2,
    0x00, 0x04, 0x00, 0x00, 0x09, 0xda, 0x00, 0x08, 0x00, 0x00, 0x09, 0xe6,
    0x00, 0x32, 0x00, 0x00, 0x09, 0xf2, 0x00, 0x16, 0x00, 0x00, 0x0a, 0x26,
    0x00, 0x4c, 0x00, 0x00, 0x0a, 0x44, 0x00, 0x50, 0x00, 0x00, 0x0a, 0x98,
    0x00, 0x10, 0x00, 0x00, 0x0a, 0xec, 0x00, 0x01, 0x00, 0x00, 0x0b, 0x21,
    0x00, 0x04, 0x00, 0x00, 0x0b, 0x2a, 0x00, 0x04, 0x00, 0x00, 0x0b, 0x4c,
    0x00, 0x0c, 0x00, 0x00, 0x0b, 0x54, 0x00, 0x04, 0x00, 0x00, 0x0b, 0xa6,
    0x00, 0x06, 0x00, 0x00, 0x0b, 0xae, 0x00, 0x02, 0x00, 0x00, 0x0b, 0xaa,
    0x00, 0x3c, 0x00, 0x00, 0x0b, 0xc2, 0x00, 0x11, 0x00, 0x00, 0x0d, 0x32,
    0x00, 0x04, 0x00, 0x00, 0x0d, 0xcc, 0x00, 0x04, 0x00, 0x00, 0x0c, 0xc8,
    0x00, 0x00,
};

static void load_switch_list()
{
    if(!lm_ranges.empty())
        return;

    std::vector<uint8_t> data;
    INTEGER save_map = LM(CurMap);
    UseResFile(LM(SysMap));
    Handle h = Get1Resource("lmem"_4, -16458);
    UseResFile(save_map);
    if(h)
        data.assign((uint8_t *)*h, (uint8_t *)*h + GetHandleSize(h));
    else
        data.assign(builtin_lmem, builtin_lmem + sizeof builtin_lmem);

    for(size_t i = 0; i + 6 <= data.size(); i += 6)
    {
        uint16_t len = data[i] << 8 | data[i + 1];
        if(!len)
            break;
        uint32_t addr = data[i + 2] << 24 | data[i + 3] << 16 | data[i + 4] << 8 | data[i + 5];
        lm_ranges.push_back({ addr, len });
    }
    lm_ranges.push_back({ 0x2AA, 4 }); /* ApplZone */
    lm_ranges.push_back({ 0x130, 4 }); /* ApplLimit */
    lm_ranges.push_back({ 0x9D6, 4 }); /* WindowList: the process's layer */
    /* Executor allocates these in the application's heap (7.5.5 shares
       them system-wide), so under Executor they belong to the process. */
    lm_ranges.push_back({ 0xCD0, 4 }); /* AuxWinHead */
    lm_ranges.push_back({ 0xCD4, 4 }); /* AuxCtlHead */
    lm_ranges.push_back({ 0xD50, 4 }); /* MenuCInfo */

    lm_bytes = 0;
    for(const lm_range &r : lm_ranges)
    {
        if(r.addr == 0x9D6)
            windowlist_offset = lm_bytes;
        lm_bytes += r.len;
    }
}

static void save_lowmem(std::vector<uint8_t> &buf)
{
    buf.resize(lm_bytes);
    size_t off = 0;
    for(const lm_range &r : lm_ranges)
    {
        memcpy(&buf[off], SYN68K_TO_US(r.addr), r.len);
        off += r.len;
    }
}

static void restore_lowmem(const std::vector<uint8_t> &buf)
{
    size_t off = 0;
    for(const lm_range &r : lm_ranges)
    {
        memcpy(SYN68K_TO_US(r.addr), &buf[off], r.len);
        off += r.len;
    }
}

static void save_world(process_info_t *p)
{
    save_lowmem(p->lowmem);
    p->cpu.resize(syn68k_context_size());
    syn68k_save_context(p->cpu.data());
    save_traps(p->traps);
    p->host.clear();
    for(const host_var &v : host_vars())
        p->host.insert(p->host.end(), (uint8_t *)v.p, (uint8_t *)v.p + v.n);
    if(ROMlib_ae_private())
        p->ae_tables = ROMlib_ae_private()->appl_zone_tables;
    if(LM(AE_info))
        p->guest_ae_tables = LM(AE_info)->appl_zone_tables;
}

static void restore_world(process_info_t *p)
{
    restore_lowmem(p->lowmem);
    syn68k_restore_context(p->cpu.data());
    restore_traps(p->traps);
    size_t off = 0;
    for(const host_var &v : host_vars())
    {
        /* State registered after this process was saved starts cleared. */
        if(off + v.n <= p->host.size())
            memcpy(v.p, &p->host[off], v.n);
        else
            memset(v.p, 0, v.n);
        off += v.n;
    }
    if(ROMlib_ae_private())
        ROMlib_ae_private()->appl_zone_tables = p->ae_tables;
    if(LM(AE_info))
        LM(AE_info)->appl_zone_tables = p->guest_ae_tables;
}

/* A new process's world: low memory and CPU as the first process had them
   before its launch, host state cleared. */
static void fresh_world(process_info_t *p)
{
    restore_lowmem(lowmem_template);
    syn68k_restore_context(cpu_template.data());
    restore_traps(traps_template);
    for(const host_var &v : host_vars())
        memset(v.p, 0, v.n);
    if(ROMlib_ae_private())
        ROMlib_ae_private()->appl_zone_tables = nullptr;
    if(LM(AE_info))
        LM(AE_info)->appl_zone_tables = nullptr;
}

void Executor::process_capture_template()
{
    process_bootstrap();
    load_switch_list();
    if(lowmem_template.empty())
    {
        save_lowmem(lowmem_template);
        cpu_template.resize(syn68k_context_size());
        syn68k_save_context(cpu_template.data());
        save_traps(traps_template);
    }
}

/* ── Layers ──
 * Each process's windows are its layer: its own window list (WindowList is
 * switched with the process).  The Window Manager works on the current
 * layer and asks here about the others: what the layers in front cover,
 * and to carry painting on into the layers behind.  A layer view lets it
 * look from another process's layer (LM(WindowList) is that process's
 * list meanwhile). */

static process_info_t *layer_view_p;
static GUEST<WindowPeek> current_list_stash; /* while a view looks elsewhere */

static process_info_t *viewer()
{
    return layer_view_p ? layer_view_p : current_process_info;
}

/* A process's window list, wherever it is kept right now. */
static WindowPeek chain_of(process_info_t *p)
{
    if(p == viewer())
        return LM(WindowList);
    if(p == current_process_info)
        return current_list_stash;
    if(p->lowmem.size() < windowlist_offset + 4)
        return nullptr;
    GUEST<WindowPeek> w;
    memcpy(&w, &p->lowmem[windowlist_offset], sizeof w);
    return w;
}

namespace
{
struct LayerView
{
    process_info_t *saved_view;
    GUEST<WindowPeek> saved_list;

    explicit LayerView(process_info_t *p)
        : saved_view(layer_view_p), saved_list(LM(WindowList))
    {
        WindowPeek head = chain_of(p);
        if(!layer_view_p)
            current_list_stash = LM(WindowList);
        layer_view_p = p;
        LM(WindowList) = head;
    }
    ~LayerView()
    {
        LM(WindowList) = saved_list;
        layer_view_p = saved_view;
    }
};
}

static std::vector<process_info_t *> layers_in_front_of(process_info_t *v)
{
    std::vector<process_info_t *> out;
    for(process_info_t *p : layer_order)
    {
        if(p == v)
            break;
        out.push_back(p);
    }
    return out;
}

static std::vector<process_info_t *> layers_behind(process_info_t *v)
{
    std::vector<process_info_t *> out;
    bool behind = false;
    for(process_info_t *p : layer_order)
    {
        if(behind)
            out.push_back(p);
        if(p == v)
            behind = true;
    }
    return out;
}

static void subtract_layer(process_info_t *p, RgnHandle rgn)
{
    for(WindowPeek w = chain_of(p); w; w = WINDOW_NEXT_WINDOW(w))
        if(WINDOW_VISIBLE(w))
            DiffRgn(rgn, WINDOW_STRUCT_REGION(w), rgn);
}

void Executor::ROMlib_layers_clip_above(RgnHandle rgn)
{
    if(!viewer())
        return;
    for(process_info_t *p : layers_in_front_of(viewer()))
        subtract_layer(p, rgn);
}

void Executor::ROMlib_layers_clip_below(RgnHandle rgn)
{
    if(!viewer())
        return;
    for(process_info_t *p : layers_behind(viewer()))
        subtract_layer(p, rgn);
}

void Executor::ROMlib_layers_paint_behind(RgnHandle rh)
{
    if(!viewer())
        return;
    for(process_info_t *p : layers_behind(viewer()))
    {
        if(EmptyRgn(rh))
            return;
        LayerView view(p);
        for(WindowPeek w = LM(WindowList); w && !EmptyRgn(rh); w = WINDOW_NEXT_WINDOW(w))
        {
            if(!WINDOW_VISIBLE(w))
                continue;
            RgnHandle t = NewRgn();
            SectRgn(rh, WINDOW_STRUCT_REGION(w), t);
            if(!EmptyRgn(t))
            {
                PaintOne(w, rh);
                DiffRgn(rh, WINDOW_STRUCT_REGION(w), rh);
            }
            DisposeRgn(t);
        }
    }
}

void Executor::ROMlib_layers_calcvis_behind(RgnHandle rh)
{
    if(!viewer())
        return;
    for(process_info_t *p : layers_behind(viewer()))
    {
        LayerView view(p);
        for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
            if(WINDOW_VISIBLE(w))
                CalcVis(w);
    }
}

/* Every window's visible region, after the layers changed order. */
static void recalc_all_layers()
{
    for(process_info_t *p : layer_order)
    {
        LayerView view(p);
        for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
            if(WINDOW_VISIBLE(w))
                CalcVis(w);
    }
}

/* The process whose layer has a window at pt (front first), else null. */
static process_info_t *layer_at(Point pt)
{
    for(process_info_t *p : layer_order)
    {
        for(WindowPeek w = chain_of(p); w; w = WINDOW_NEXT_WINDOW(w))
            if(WINDOW_VISIBLE(w) && PtInRgn(pt, WINDOW_STRUCT_REGION(w)))
                return p;
    }
    return nullptr;
}

/* A long of a process's switched low memory, wherever it is kept. */
static uint32_t lowmem_long(process_info_t *p, uint32_t addr)
{
    if(p == current_process_info)
        return *(GUEST<uint32_t> *)SYN68K_TO_US(addr);
    size_t off = 0;
    for(const lm_range &r : lm_ranges)
    {
        if(addr >= r.addr && addr + 4 <= r.addr + r.len && p->lowmem.size() >= off + r.len)
        {
            GUEST<uint32_t> v;
            memcpy(&v, &p->lowmem[off + (addr - r.addr)], sizeof v);
            return v;
        }
        off += r.len;
    }
    return 0;
}

static bool has_updates(process_info_t *p);

/* Something waits for this process's next event call. */
static bool wants_time(process_info_t *p)
{
    return has_updates(p) || p->os_event_pending
        || ROMlib_hle_pending(&p->serial_number)
        || lowmem_long(p, 0xA64) /* CurActivate */
        || lowmem_long(p, 0xA68) /* CurDeactive */;
}

static bool has_updates(process_info_t *p)
{
    for(WindowPeek w = chain_of(p); w; w = WINDOW_NEXT_WINDOW(w))
        if(WINDOW_VISIBLE(w) && !EmptyRgn(WINDOW_UPDATE_REGION(w)))
            return true;
    return false;
}

static void raise_layer(process_info_t *p)
{
    layer_order.erase(std::remove(layer_order.begin(), layer_order.end(), p), layer_order.end());
    layer_order.insert(layer_order.begin(), p);
}

/* ── The baton ── */

static QMutex pm_mutex;
static QWaitCondition pm_cv;
static process_info_t *pm_running;
static std::vector<process_info_t *> reap_list;

static void hand_baton(process_info_t *to)
{
    QMutexLocker lock(&pm_mutex);
    pm_running = to;
    pm_cv.wakeAll();
}

static void wait_for_baton(process_info_t *self)
{
    QMutexLocker lock(&pm_mutex);
    while(pm_running != self)
        pm_cv.wait(&pm_mutex);
}

/* ── Process threads ──
 * A QThread per process.  Guest code is handed pointers into the C++
 * frames of the Toolbox code it calls (an EventRecord on the stack...), and
 * guest addresses are host addresses below 4GB: so run() moves onto a stack
 * from a pool reserved below 4GB once, at startup of the first process, and
 * the whole process lives there (Executor's own thread does the same). */

static const int stack_slots = 16;
static const size_t stack_size = 4 * 1024 * 1024;
static char *stack_pool;
static bool stack_used[stack_slots];

static int take_stack_slot()
{
    if(!stack_pool)
        stack_pool = (char *)syn68k_alloc_low(stack_slots * stack_size);
    for(int i = 0; i < stack_slots; i++)
        if(!stack_used[i])
        {
            stack_used[i] = true;
            return i;
        }
    return -1;
}

static void process_body(process_info_t *p);

class ProcessThread : public QThread
{
public:
    explicit ProcessThread(process_info_t *p)
        : p(p)
    {
        setObjectName(QString::fromLatin1((const char *)p->app.name + 1, p->app.name[0]));
    }

protected:
    void run() override
    {
        ucontext_t low;
        getcontext(&low);
        low.uc_stack.ss_sp = stack_pool + p->stack_slot * stack_size;
        low.uc_stack.ss_size = stack_size;
        low.uc_link = &back;
        current = this;
        makecontext(&low, &ProcessThread::trampoline, 0);
        swapcontext(&back, &low);
    }

private:
    static void trampoline()
    {
        process_body(current->p);
    }

    process_info_t *p;
    ucontext_t back;
    static thread_local ProcessThread *current;
};
thread_local ProcessThread *ProcessThread::current;

static void start_thread(process_info_t *p)
{
    p->stack_slot = take_stack_slot();
    if(p->stack_slot < 0)
        gui_fatal("too many processes");
    p->thread = new ProcessThread(p);
    p->thread->start();
}

/* Threads of processes that quit, collected once someone else runs. */
static void reap_dead()
{
    for(process_info_t *p : reap_list)
    {
        p->thread->wait();
        delete p->thread;
        stack_used[p->stack_slot] = false;
        delete p;
    }
    reap_list.clear();
}

std::vector<ROMlib_process_entry> Executor::ROMlib_process_entries()
{
    std::vector<ROMlib_process_entry> out;
    for(process_info_t *p = process_info_list; p; p = p->next)
    {
        if(!p->name[0])
            continue; /* not launched yet */
        ROMlib_process_entry e;
        e.psn = p->serial_number;
        memcpy(e.name, p->name, p->name[0] + 1);
        e.icon = p->icon;
        e.current = p == current_process_info;
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const ROMlib_process_entry &a, const ROMlib_process_entry &b) {
        return IUCompString(a.name, b.name) < 0;
    });
    return out;
}

bool Executor::ROMlib_process_with_signature(OSType sig, ProcessSerialNumber *psn)
{
    for(process_info_t *p = process_info_list; p; p = p->next)
        if(p->signature == sig && p->state != process_info_t::dead)
        {
            *psn = p->serial_number;
            return true;
        }
    return false;
}

/* Back on our thread after a switch: our world is in place. */
static void resumed()
{
    reap_dead();
    process_info_t *me = current_process_info;
    ROMlib_app_menu_update();
    DrawMenuBar();
    if(me->uncovered)
    {
        /* What the process that quit covered: the desktop and our windows. */
        RgnHandle rgn = me->uncovered;
        me->uncovered = nullptr;
        if(LM(WindowList))
        {
            PaintBehind(LM(WindowList), rgn);
            CalcVisBehind(LM(WindowList), rgn);
        }
        else
            PaintOne(nullptr, rgn);
        DisposeRgn(rgn);
        DrawMenuBar();
    }
    if(me->raised)
    {
        /* Brought to the front: redraw what the layers in front covered. */
        RgnHandle rgn = me->raised;
        me->raised = nullptr;
        recalc_all_layers();
        for(WindowPeek w = LM(WindowList); w && !EmptyRgn(rgn); w = WINDOW_NEXT_WINDOW(w))
        {
            if(!WINDOW_VISIBLE(w))
                continue;
            RgnHandle t = NewRgn();
            SectRgn(rgn, WINDOW_STRUCT_REGION(w), t);
            if(!EmptyRgn(t))
            {
                PaintOne(w, rgn);
                DiffRgn(rgn, WINDOW_STRUCT_REGION(w), rgn);
            }
            DisposeRgn(t);
        }
        DisposeRgn(rgn);
    }
    if(me->activate_pending)
    {
        me->activate_pending = false;
        if(WindowPtr w = FrontWindow())
        {
            HiliteWindow(w, true);
            if(!(me->mode & 0x0800)) /* SIZE doesActivateOnFGSwitch */
                LM(CurActivate) = w;
        }
        if(me->mode & 0x4000) /* SIZE acceptSuspendResumeEvents */
        {
            me->os_event_pending = true;
            me->os_event_message = 1 << 24 | 1; /* resume */
        }
    }
    if(me->died_pending)
    {
        /* 'aevt'/'obit' (kAEApplicationDied), as 7.5.5's Process Manager
           posts it: the dead process's serial number and its error. */
        me->died_pending = false;
        struct
        {
            GUEST<OSType> signature;
            GUEST<int16_t> major, minor;
            GUEST<OSType> marker;
            GUEST<OSType> err_key, err_type;
            GUEST<int32_t> err_size;
            GUEST<int32_t> err;
            GUEST<OSType> psn_key, psn_type;
            GUEST<int32_t> psn_size;
            GUEST<uint32_t> psn_high, psn_low;
        } msg = { "aevt"_4, 1, 1, ";;;;"_4,
                  "errn"_4, "long"_4, 4, 0,
                  "psn "_4, "psn "_4, 8, me->died.highLongOfPSN, me->died.lowLongOfPSN };
        static_assert(sizeof msg == 48);

        EventRecord evt = {};
        evt.what = kHighLevelEvent;
        evt.message = "aevt"_4;
        GUEST<uint32_t> id = "obit"_4;
        memcpy(&evt.where, &id, sizeof id);
        ProcessSerialNumber to = me->serial_number; /* guest-visible */
        PostHighLevelEvent(&evt, (Ptr)&to, 0, (Ptr)&msg, sizeof msg,
                           0x8000 /* receiverIDisPSN */);
    }
}

/* Called by the running process, on its own thread. */
static void switch_to(process_info_t *to)
{
    process_info_t *me = current_process_info;
    if(to == me)
        return;

    save_world(me);
    me->state = process_info_t::parked;
    current_process_info = to;
    if(to->state == process_info_t::fresh)
        start_thread(to);
    else
    {
        restore_world(to);
        to->state = process_info_t::running;
    }
    hand_baton(to);
    wait_for_baton(me);
    resumed();
}

/* Remove what a process leaves in system-wide queues: VBL and Time Manager
   tasks living in its partition. */
static void remove_tasks_in(Ptr lo, Ptr hi)
{
    auto inside = [lo, hi](void *p) { return (Ptr)p >= lo && (Ptr)p < hi; };
    VBLTaskPtr next_vbl;
    for(VBLTaskPtr v = (VBLTaskPtr)LM(VBLQueue).qHead; v; v = next_vbl)
    {
        next_vbl = (VBLTaskPtr)v->qLink;
        if(inside(v))
            VRemove(v);
    }
}

/* The current process quits: tidy up, switch to its launcher (or the
   first process) and let the thread end. */
static void exit_current()
{
    process_info_t *me = current_process_info;

    /* The screen its windows covered. */
    RgnHandle uncovered;
    {
        TheZoneGuard guard(LM(SysZone));
        uncovered = NewRgn();
    }
    for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
    {
        if(WINDOW_VISIBLE(w))
            UnionRgn(WINDOW_STRUCT_REGION(w), uncovered, uncovered);
        /* Host-side records of its windows (palettes) go with them. */
        pm_window_closed((WindowPtr)w);
    }
    LM(WindowList) = nullptr;

    /* Its resource files: everything above the System file. */
    while(LM(TopMapHndl) && (*(resmaphand)LM(TopMapHndl))->resfn != LM(SysMap))
        CloseResFile((*(resmaphand)LM(TopMapHndl))->resfn);

    if(me->partition)
    {
        Ptr lo = *me->partition;
        remove_tasks_in(lo, lo + me->partition_size);
        TheZoneGuard guard(ROMlib_pm_zone);
        HUnlock(me->partition);
        DisposeHandle(me->partition);
        me->partition = nullptr;
    }

    if(me->icon)
        DisposeIconSuite(me->icon, true);
    me->icon = nullptr;
    layer_order.erase(std::remove(layer_order.begin(), layer_order.end(), me), layer_order.end());
    if(pending_front == me)
        pending_front = nullptr;
    forget_process(me);
    ROMlib_hle_forget(&me->serial_number);
    ROMlib_components_process_exit(&me->serial_number);
    me->state = process_info_t::dead;
    reap_list.push_back(me);

    process_info_t *next = nullptr;
    for(process_info_t *p = process_info_list; p; p = p->next)
        if(PSN_EQ_P(p->serial_number, me->launcher))
            next = p;
    if(!next)
        next = process_info_list;

    if(front_process == me)
    {
        raise_layer(next);
        front_process = next;
        next->activate_pending = true;
    }
    current_process_info = next;
    restore_world(next);
    next->state = process_info_t::running;
    next->uncovered = uncovered;
    if(next->mode & 0x0100) /* SIZE acceptAppDiedEvents */
    {
        next->died_pending = true;
        next->died = me->serial_number;
    }
    hand_baton(next);
}

/* A process's life, on its low stack. */
static void process_body(process_info_t *p)
{
    wait_for_baton(p);
    fresh_world(p);
    p->state = process_info_t::running;
    try
    {
        if(p->desk_accessory)
            ROMlib_run_desk_accessory(&p->app, p->da_name[0] ? p->da_name : nullptr);
        else
            ROMlib_launch_process(&p->app);
    }
    catch(const ExitToShellException &)
    {
    }
    exit_current();
}

bool Executor::ROMlib_process_open_event_posted()
{
    return current_process_info && current_process_info->posted_open_event;
}

bool Executor::ROMlib_process_has_thread()
{
    return current_process_info && current_process_info->own_thread;
}

/* The front process steps back: its front window is unhilited (7.5.5's
   Process Manager does this itself), a deactivate event follows unless the
   application handles that on suspend (SIZE doesActivateOnFGSwitch), and
   it gets a suspend event (SIZE acceptSuspendResumeEvents). */
static void step_back(process_info_t *p)
{
    if(WindowPtr w = FrontWindow())
    {
        HiliteWindow(w, false);
        if(!(p->mode & 0x0800))
            LM(CurDeactive) = w;
    }
    if(p->mode & 0x4000)
    {
        p->os_event_pending = true;
        p->os_event_message = 1 << 24; /* suspend */
    }
}

/* Major switch, from the front process: q comes to the front. */
static void bring_to_front(process_info_t *q)
{
    step_back(current_process_info);
    RgnHandle covered;
    {
        TheZoneGuard guard(LM(SysZone));
        covered = NewRgn();
    }
    for(process_info_t *p : layers_in_front_of(q))
        for(WindowPeek w = chain_of(p); w; w = WINDOW_NEXT_WINDOW(w))
            if(WINDOW_VISIBLE(w))
                UnionRgn(WINDOW_STRUCT_REGION(w), covered, covered);
    raise_layer(q);
    front_process = q;
    q->raised = covered;
    q->activate_pending = true;
    switch_to(q);
}

/* Every event call: where processes switch, as 7.5.5's Process Manager
   does in GetNextEvent/WaitNextEvent/EventAvail. */
void Executor::ROMlib_process_event_hook()
{
    process_info_t *me = current_process_info;
    if(!me)
        return;

    if(pending_launch)
    {
        /* A process just launched comes to the front. */
        process_info_t *p = pending_launch;
        pending_launch = nullptr;
        step_back(me);
        raise_layer(p);
        front_process = p;
        switch_to(p);
        return;
    }

    if(me != front_process)
    {
        /* Background time: back to the front once we are up to date. */
        if(!wants_time(me))
            switch_to(front_process);
        return;
    }

    if(pending_front)
    {
        process_info_t *q = pending_front;
        pending_front = nullptr;
        if(q != me)
        {
            bring_to_front(q);
            return;
        }
    }

    /* A click in another process's window (or on the desktop, which is
       Finder's) brings that process to the front.  It keeps the click if
       its SIZE says getFrontClicks. */
    EventRecord evt;
    if(OSEventAvail(mDownMask, &evt))
    {
        Point pt = evt.where.get();
        if(pt.v >= LM(MBarHeight))
        {
            process_info_t *hit = layer_at(pt);
            if(!hit)
                hit = process_info_list; /* the desktop */
            if(hit && hit != me && hit->state == process_info_t::parked)
            {
                if(!(hit->mode & 0x0200))
                    GetOSEvent(mDownMask, &evt);
                bring_to_front(hit);
                return;
            }
        }
    }

    /* Minor switch: a process behind with something waiting gets time. */
    for(process_info_t *p : layer_order)
        if(p != me && p->state == process_info_t::parked && wants_time(p))
        {
            switch_to(p);
            return;
        }
}

bool Executor::ROMlib_process_is_front()
{
    return !current_process_info || current_process_info == front_process;
}

bool Executor::ROMlib_process_os_event(EventRecord *evt, bool remove)
{
    process_info_t *me = current_process_info;
    if(!me || !me->os_event_pending)
        return false;
    evt->what = osEvt;
    evt->message = me->os_event_message;
    if(remove)
        me->os_event_pending = false;
    return true;
}

OSErr Executor::process_launch(LaunchParamBlockRec *lpbp)
{
    process_info_t *p = new_process();
    p->own_thread = true;
    p->app = *lpbp->launchAppSpec;
    p->launcher = current_process_info->serial_number;
    pending_launch = p;
    lpbp->launchProcessSN = p->serial_number;

    /* As 7.5.5's Process Manager: the launcher's AppParameters (Finder's
       'oapp'/'odoc', in Apple event wire format) become the new process's
       first high-level event: {EventRecord, refcon.l, length.l, message}. */
    if(Ptr ap = (Ptr)lpbp->launchAppParameters)
    {
        const uint8_t *b = (const uint8_t *)ap;
        if(((b[0] << 8) | b[1]) == kHighLevelEvent)
        {
            EventRecord evt = *(EventRecord *)ap;
            int32_t refcon = *(GUEST<int32_t> *)(ap + 16);
            int32_t length = *(GUEST<int32_t> *)(ap + 20);
            ProcessSerialNumber to = p->serial_number; /* guest-visible */
            PostHighLevelEvent(&evt, (Ptr)&to, refcon, ap + 24, length,
                               0x8000 /* receiverIDisPSN */);
            p->posted_open_event = true;
        }
    }
    return noErr;
}

/* 7.5.5: a desk accessory opened from its file gets a process of its own
   (the DA Handler, desk.cpp), created like a launchContinue launch. */
/* MacPhoenix. System mode is only counted (status: guess): a resource
   file opened in it still belongs to the calling process and closes when
   that process quits, where on 7.5.5 it would stay open for the system. */
static int system_mode_depth;

OSErr Executor::C_BeginSystemMode()
{
    system_mode_depth++;
    return noErr;
}

OSErr Executor::C_EndSystemMode()
{
    if(system_mode_depth == 0)
        return paramErr;
    system_mode_depth--;
    return noErr;
}

OSErr Executor::C_LaunchDeskAccessory(const FSSpec *spec, ConstStringPtr name)
{
    process_bootstrap();
    process_info_t *p = new_process();
    p->own_thread = true;
    p->desk_accessory = true;
    if(spec)
        p->app = *spec;
    if(name)
        memcpy(p->da_name, name, name[0] + 1);
    if(!spec && !name)
        return paramErr;
    p->launcher = current_process_info->serial_number;
    pending_launch = p;
    return noErr;
}

/* ── Partitions ─────────────────────────────────────────────────────────
 * Laid out the way 7.5.5 lays out each application (measured on a real
 * boot, see docs/executor/MEMORY_MAP.md):
 *
 *   Process Manager heap: SysZone end .. BufPtr
 *     partition: a locked handle, SIZE preferred size + 16K
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

/* Measured on 7.5.5: a partition is the SIZE preferred size plus 16K. */
static const uint32_t partition_extra = 16 * 1024;
/* No SIZE resource: Process Manager's default. */
static const uint32_t default_partition = 512 * 1024;

void Executor::process_reset_heap(THz pm_zone)
{
    ROMlib_pm_zone = pm_zone;
    if(current_process_info)
    {
        current_process_info->partition = nullptr;
        current_process_info->partition_size = 0;
    }
}

void Executor::process_layout_partition(ConstStringPtr app_name)
{
    uint32_t preferred = default_partition;
    uint32_t minimum = default_partition;
    int32_t abovea5 = 4, belowa5 = 0; /* no CODE 0 (PowerPC): A5 at the top */

    process_bootstrap();
    process_info_t *me = current_process_info;

    SetZone(ROMlib_pm_zone);
    if(me->partition)
    {
        /* Chain: the old partition goes, the heap stays. */
        HUnlock(me->partition);
        DisposeHandle(me->partition);
        me->partition = nullptr;
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
    me->partition = NewHandle(want);
    if(below)
        DisposePtr(below);
    if(!me->partition)
        gui_fatal("unable to allocate a %u byte application partition", want);
    HLock(me->partition);
    me->partition_size = want;

    Ptr p = *me->partition;
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

/* The current process is launching: record what it is. */
void Executor::process_create(bool desk_accessory_p,
                              uint32_t type, uint32_t signature)
{
    size_resource_handle size;
    process_info_t *info;

    process_bootstrap();
    info = current_process_info;
    size = get_size_resource();

    info->mode = ((size
                       ? toHost(SIZE_FLAGS(size))
                       : default_process_mode_flags)
                  | (desk_accessory_p
                         ? modeDeskAccessory
                         : 0));
    info->type = type;
    info->signature = signature;
    info->size = info->partition_size ? info->partition_size : zone_size(LM(ApplZone));
    info->launch_ticks = TickCount();
    memcpy(info->name, LM(CurApName), std::min<int>(LM(CurApName)[0], 31) + 1);
    info->name[0] = std::min<int>(info->name[0], 31);
    if(!info->icon)
        info->icon = ROMlib_app_icon_suite();
    /* The first process was not launched through us: its file is the
       application's resource file. */
    if(!info->app.name[0])
    {
        FCBPBRec fcb = {};
        Str255 fname;
        fcb.ioNamePtr = fname;
        fcb.ioRefNum = LM(CurApRefNum);
        if(PBGetFCBInfo(&fcb, false) == noErr)
        {
            info->app.vRefNum = fcb.ioFCBVRefNum;
            info->app.parID = fcb.ioFCBParID;
            memcpy(info->app.name, fname, std::min<int>(fname[0], 63) + 1);
            info->app.name[0] = std::min<int>(fname[0], 63);
        }
    }

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

    PROCESS_INFO_LAUNCHER(process_info) = info->launcher;
    /* MacPhoenix: the name and the application's file, when asked for
       (AppleScript makes an alias to its client application). */
    if(StringPtr name = PROCESS_INFO_NAME(process_info))
    {
        ConstStringPtr from = info == current_process_info || !info->name[0]
            ? (ConstStringPtr)LM(CurApName) : info->name;
        int len = std::min<int>(from[0], 31);
        memcpy(name + 1, from + 1, len);
        name[0] = len;
    }
    if(FSSpecPtr spec = process_info->processAppSpec)
        *spec = info->app;

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
    *serial_number = front_process->serial_number;
    return noErr;
}

/* Takes effect at the front process's next event call, as on 7.5.5. */
OSErr Executor::C_SetFrontProcess(ProcessSerialNumber *serial_number)
{
    process_info_t *p = get_process_info(serial_number);
    if(!p)
        return procNotFound;
    if(p != front_process)
        pending_front = p;
    return noErr;
}

OSErr Executor::C_WakeUpProcess(ProcessSerialNumber *serial_number)
{
    return get_process_info(serial_number) ? noErr : procNotFound;
}

/* MacPhoenix: a process's PPC port, as the Process Manager registers it:
   named after the process, by creator and type ('ep01'). */
/* A process's PPC port: its name, by creator and type 'ep01'. */
static void port_name_of(process_info_t *p, PPCPortRec *port)
{
    memset(port, 0, sizeof *port);
    port->nameScript = 0; /* smRoman */
    ConstStringPtr name = p == current_process_info || !p->name[0] ? (ConstStringPtr)LM(CurApName) : p->name;
    int len = std::min<int>(name[0], 32);
    port->name[0] = len;
    memcpy(&port->name[1], &name[1], len);
    port->portKindsSelector = 1; /* ppcByCreatorAndType */
    port->u.port.creator = p->signature;
    port->u.port.type = "ep01"_4;
}

void Executor::ROMlib_process_port_name(PPCPortRec *port)
{
    process_bootstrap();
    port_name_of(current_process_info, port);
}

bool Executor::ROMlib_process_port_name_of(const ProcessSerialNumber *psn, PPCPortRec *port)
{
    for(process_info_t *p = process_info_list; p; p = p->next)
        if(PSN_EQ_P(*psn, p->serial_number))
        {
            port_name_of(p, port);
            return true;
        }
    return false;
}

OSErr Executor::C_GetProcessSerialNumberFromPortName(
    PPCPortPtr port_name, ProcessSerialNumber *serial_number)
{
    for(process_info_t *p = process_info_list; p; p = p->next)
    {
        PPCPortRec theirs;
        port_name_of(p, &theirs);
        if(EqualString(port_name->name, theirs.name, true, true))
        {
            *serial_number = p->serial_number;
            return noErr;
        }
    }
    return procNotFound;
}

OSErr Executor::C_GetPortNameFromProcessSerialNumber(
    PPCPortPtr port_name, ProcessSerialNumber *serial_number)
{
    ProcessSerialNumber psn = *serial_number;
    if(psn.highLongOfPSN == 0 && psn.lowLongOfPSN == kCurrentProcess)
        GetCurrentProcess(&psn);
    return ROMlib_process_port_name_of(&psn, port_name) ? noErr : procNotFound;
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

void Executor::ROMlib_desktop_layer_invalidate(RgnHandle rgn)
{
    if(desktop_layer && WINDOW_UPDATE_REGION(desktop_layer))
        UnionRgn(rgn, WINDOW_UPDATE_REGION(desktop_layer), WINDOW_UPDATE_REGION(desktop_layer));
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

/* The Apple menu's items as Finder hands them over (menu/sysmenu.cpp
   keeps them and builds the Apple menu). */
OSErr Executor::C_AddAppleMenuItem(StringPtr name, int16_t sortGroup, int16_t flagA,
                                   Handle iconSuite, int16_t flagB, StringPtr info,
                                   int32_t key)
{
    (void)flagA;
    (void)flagB;
    (void)info;
    if(!name || !name[0] || !key)
        return paramErr;
    ROMlib_apple_menu_add(name, sortGroup, iconSuite, key);
    return noErr;
}

OSErr Executor::C_RemoveAppleMenuItems(int32_t key)
{
    return ROMlib_apple_menu_remove(key) ? noErr : paramErr;
}
