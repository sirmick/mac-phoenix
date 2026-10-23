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
 * Both are called in the A5 world their creator had. A task may schedule
 * itself again; it then runs at the next opportunity, not inside itself.
 */

#include "ot_provider.h"

#include <rsys/opentransport.h>

#include <map>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

int Executor::ot::interrupt_level = 0;

namespace
{

struct Task
{
    uint32_t proc, arg, a5;
    bool system;
    bool scheduled = false;
};
std::map<uint32_t, Task> tasks; /* by cookie */
uint32_t next_cookie = 0x4F540000; /* 'OT' */

void run(uint32_t cookie)
{
    auto it = tasks.find(cookie);
    if(it == tasks.end() || !it->second.scheduled)
        return;
    it->second.scheduled = false;
    Task t = it->second;
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
        if(t.scheduled && t.system == system && (!system || t.a5 == a5))
            due.push_back(cookie);
    for(uint32_t cookie : due)
        run(cookie);
}

bool hooked = false;

uint32_t create(uint32_t proc, uint32_t arg, bool system)
{
    if(!hooked)
    {
        add_pump_hook([] { run_scheduled(false, 0); });
        hooked = true;
    }
    if(!proc)
        return 0;
    uint32_t cookie = next_cookie += 4;
    tasks[cookie] = { proc, arg, current_a5(), system };
    OT_LOG("%s task $%08x: proc $%08x arg $%08x\n", system ? "system" : "deferred", cookie,
           proc, arg);
    return cookie;
}

/* long OTCreateDeferredTaskPriv(OTProcessProcPtr, void *arg, client) */
uint32_t create_deferred(const Call &c)
{
    return create(c.l(0), c.l(1), false);
}

uint32_t create_system(const Call &c)
{
    return create(c.l(0), c.l(1), true);
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

/* OSStatus OTDestroyDeferredTask(long) */
uint32_t destroy(const Call &c)
{
    return tasks.erase(c.l(0)) ? kOTNoError : kOTBadReferenceErr;
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

const char cmn[] = "OTClnt$CmnShared";

const Def table[] = {
    { cmn, "OTCreateDeferredTaskPriv", Conv::C, "LLL", 'L', create_deferred },
    { cmn, "OTDESTROYDEFERREDTASK", Conv::Pascal, "L", 'L', destroy },
    { cmn, "OTSCHEDULEDEFERREDTASK", Conv::Pascal, "L", 'B', schedule },
    { cmn, "OTSCHEDULEINTERRUPTTASK", Conv::Pascal, "L", 'B', schedule },
    { cmn, "OTCreateSystemTaskPriv", Conv::C, "LLL", 'L', create_system },
    { cmn, "OTDESTROYSYSTEMTASK", Conv::Pascal, "L", 'L', destroy },
    { cmn, "OTSCHEDULESYSTEMTASK", Conv::Pascal, "L", 'B', schedule },
    { cmn, "OTCANCELSYSTEMTASK", Conv::Pascal, "L", 'B', cancel },
    { cmn, "OTCANMAKESYNCCALL", Conv::Pascal, "", 'B', can_make_sync_call },
    { cmn, "OTIsAtInterruptLevel", Conv::C, "", 'B', is_at_interrupt_level },
    { cmn, "OTCanLoadLibraries", Conv::C, "", 'B', can_load_libraries },
    { cmn, "OTENTERINTERRUPT", Conv::Pascal, "", 'V', enter_interrupt },
    { cmn, "OTLEAVEINTERRUPT", Conv::Pascal, "", 'V', leave_interrupt },
};

} // anonymous namespace

void Executor::ot::register_tasks()
{
    register_table(table);
}

/* A process quits: tasks whose code was in its partition. */
void Executor::ot::tasks_process_exit(uint32_t lo, uint32_t hi)
{
    for(auto it = tasks.begin(); it != tasks.end();)
    {
        if(it->second.proc >= lo && it->second.proc < hi)
            it = tasks.erase(it);
        else
            ++it;
    }
}

void Executor::ROMlib_ot_system_task()
{
    if(!ROMlib_open_transport || tasks.empty())
        return;
    run_scheduled(true, current_a5());
}
