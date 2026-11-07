/* ot_tasks.cpp — Open Transport's deferred and system tasks, and its idea
 * of interrupt level ("OTClnt$CmnShared").
 *
 * A task is a pascal procedure and an argument, made once and scheduled
 * any number of times; scheduling an already scheduled task does nothing.
 *   - A deferred task runs at deferred task time: from the OT interrupt
 *     (the pump), soon after it was scheduled. OTScheduleInterruptTask is
 *     the same here.
 *   - A system task runs at system task time: from SystemTask (which
 *     WaitNextEvent calls), in its own process.
 *   - A timer task (OT 1.1.1, OTClnt$NtvUtils / OTNtvUtilLib) runs at
 *     deferred task time too, once the milliseconds it was scheduled for
 *     have gone by. Each has a Time Manager task of its own whose routine
 *     (a host callback) marks it due and raises the OT interrupt.
 * Both are called in the A5 world their creator had. A task may schedule
 * itself again; it then runs at the next opportunity, not inside itself.
 */

#include "ot_provider.h"

#include <MemoryMgr.h>
#include <TimeMgr.h>
#include <rsys/opentransport.h>

#include <map>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

int Executor::ot::interrupt_level = 0;

namespace Executor::ot
{

namespace {
struct Task
{
    uint32_t proc, arg, a5;
    bool system;
    bool ppc; /* a PowerPC client's (proc is a TV) */
    bool scheduled = false;
    TMTask *tm = nullptr; /* a timer task's Time Manager task */
    bool due = false;     /* its time has come: it runs at the next pump */
};
}

std::map<uint32_t, Task> tasks; /* by cookie */
uint32_t next_cookie = 0x4F540000; /* 'OT' */

void run(uint32_t cookie)
{
    auto it = tasks.find(cookie);
    if(it == tasks.end() || !it->second.scheduled)
        return;
    it->second.scheduled = false;
    it->second.due = false;
    Task t = it->second;
    if(t.ppc)
    {
        callPPC(t.proc, { t.arg });
        return;
    }
    uint32_t saved_a7 = EM_A7, saved_a5 = EM_A5;
    if(t.a5)
        EM_A5 = t.a5;
    EM_A7 -= 4;
    WRITEUL(EM_A7, t.arg);
    execute68K(t.proc);
    EM_A7 = saved_a7;
    EM_A5 = saved_a5;
}

/* The scheduled tasks of one kind (and, for system tasks, one A5 world),
   as they stand now: what they schedule runs next time. */
void run_scheduled(bool system, uint32_t a5)
{
    std::vector<uint32_t> due;
    for(auto &[cookie, t] : tasks)
        if(t.scheduled && t.system == system && (!system || t.a5 == a5) && (!t.tm || t.due))
            due.push_back(cookie);
    for(uint32_t cookie : due)
        run(cookie);
}

bool tasks_hooked = false;

uint32_t create(uint32_t proc, uint32_t arg, bool system, bool ppc)
{
    if(!tasks_hooked)
    {
        add_pump_hook([] { run_scheduled(false, 0); });
        tasks_hooked = true;
    }
    if(!proc)
        return 0;
    uint32_t cookie = next_cookie += 4;
    tasks[cookie] = { proc, arg, current_a5(), system, ppc };
    OT_LOG("%s task $%08x: proc $%08x arg $%08x\n", system ? "system" : "deferred", cookie,
           proc, arg);
    return cookie;
}

/* long OTCreateDeferredTaskPriv(OTProcessProcPtr, void *arg, client) */
uint32_t create_deferred(const Call &c)
{
    return create(c.l(0), c.l(1), false, c.ppc());
}

uint32_t create_system(const Call &c)
{
    return create(c.l(0), c.l(1), true, c.ppc());
}

/* Boolean OTScheduleDeferredTask(long): false if it was scheduled already
   (or isn't a task). */
uint32_t schedule(const Call &c)
{
    auto it = tasks.find(c.l(0));
    if(it == tasks.end() || it->second.scheduled)
        return 0;
    it->second.scheduled = true;
    if(!it->second.system)
        wake();
    return 1;
}

/* Boolean OTCancelSystemTask(long): true if it was scheduled. */
uint32_t cancel(const Call &c)
{
    auto it = tasks.find(c.l(0));
    if(it == tasks.end() || !it->second.scheduled)
        return 0;
    it->second.scheduled = false;
    return 1;
}

/* A task gone: a timer's Time Manager task out of the queue and freed. */
void drop(std::map<uint32_t, Task>::iterator it)
{
    if(TMTask *tm = it->second.tm)
    {
        RmvTime((QElemPtr)tm);
        DisposePtr((Ptr)tm);
    }
    tasks.erase(it);
}

/* OSStatus OTDestroyDeferredTask(long) */
uint32_t destroy(const Call &c)
{
    auto it = tasks.find(c.l(0));
    if(it == tasks.end())
        return kOTBadReferenceErr;
    drop(it);
    return kOTNoError;
}

/* ---- timer tasks (OT 1.1.1) ---- */

/* A timer's Time Manager task expired (A1: the TMTask): it is due, at the
   next pump. Called as the task's routine, so it returns past its return
   address. */
syn68k_addr_t timer_expired(syn68k_addr_t, void *)
{
    uint32_t tm = EM_A1;
    for(auto &[cookie, t] : tasks)
        if(t.tm && guest_addr(t.tm) == tm && t.scheduled)
        {
            t.due = true;
            wake();
        }
    uint32_t ret = READUL(EM_A7);
    EM_A7 += 4;
    return ret;
}

/* long OTCreateTimerTask(OTProcessProcPtr, void *arg): 0 without a
   procedure or the memory for its Time Manager task. */
uint32_t create_timer(const Call &c)
{
    uint32_t cookie = create(c.l(0), c.l(1), false, c.ppc());
    if(!cookie)
        return 0;
    static syn68k_addr_t routine = callback_install(timer_expired, nullptr);
    TMTask *tm = (TMTask *)NewPtrSysClear(sizeof(TMTask));
    if(!tm)
    {
        tasks.erase(cookie);
        return 0;
    }
    tm->tmAddr = (ProcPtr)SYN68K_TO_US(routine);
    InsTime((QElemPtr)tm);
    tasks[cookie].tm = tm;
    return cookie;
}

/* Boolean OTScheduleTimerTask(long, OTTimeout milliSeconds): it runs once
   that long from now; scheduled again before it ran, from the new call. */
uint32_t schedule_timer(const Call &c)
{
    auto it = tasks.find(c.l(0));
    if(it == tasks.end() || !it->second.tm)
        return false;
    Task &t = it->second;
    t.scheduled = true;
    t.due = false;
    uint32_t ms = c.l(1);
    if(ms == 0)
    {
        t.due = true;
        wake();
    }
    else
        PrimeTime((QElemPtr)t.tm, std::min<uint32_t>(ms, 0x7FFFFFFF));
    OT_LOG("timer task $%08x in %u ms\n", c.l(0), ms);
    return true;
}

/* Boolean OTCancelTimerTask(long): true if it was scheduled; it won't run. */
uint32_t cancel_timer(const Call &c)
{
    auto it = tasks.find(c.l(0));
    if(it == tasks.end() || !it->second.tm || !it->second.scheduled)
        return false;
    Task &t = it->second;
    t.scheduled = false;
    t.due = false;
    RmvTime((QElemPtr)t.tm); /* back in the queue, not counting down */
    InsTime((QElemPtr)t.tm);
    return true;
}

/* void OTDestroyTimerTask(long): cancelled and gone. */
uint32_t destroy_timer(const Call &c)
{
    auto it = tasks.find(c.l(0));
    if(it != tasks.end() && it->second.tm)
        drop(it);
    return 0;
}

/* Boolean OTCanMakeSyncCall(): not at interrupt or deferred task time. */
uint32_t can_make_sync_call(const Call &)
{
    return interrupt_level == 0;
}

uint32_t is_at_interrupt_level(const Call &)
{
    return interrupt_level != 0;
}

/* Boolean OTCanLoadLibraries(): only at system task time, as OT's. */
uint32_t can_load_libraries(const Call &)
{
    return interrupt_level == 0;
}

/* pascal void OTEnterInterrupt(void), OTLeaveInterrupt(void): a Time
   Manager task or the like says it is one. */
uint32_t enter_interrupt(const Call &)
{
    interrupt_level++;
    return 0;
}

uint32_t leave_interrupt(const Call &)
{
    if(interrupt_level > 0)
        interrupt_level--;
    return 0;
}

} // namespace Executor::ot

/* A process quits: tasks whose code was in its partition. */
void Executor::ot::tasks_process_exit(uint32_t lo, uint32_t hi)
{
    for(auto it = tasks.begin(); it != tasks.end();)
    {
        auto next = std::next(it);
        if(it->second.proc >= lo && it->second.proc < hi)
            drop(it);
        it = next;
    }
}

void Executor::ROMlib_ot_system_task()
{
    if(!ROMlib_open_transport || tasks.empty())
        return;
    run_scheduled(true, current_a5());
}
