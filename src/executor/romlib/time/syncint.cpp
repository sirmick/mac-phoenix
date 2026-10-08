#include <time/syncint.h>
#include <time/time.h>
#include <base/cpu.h>
#include <PowerCore.h>
#include <base/debugger.h>

#include <chrono>
#include <array>
#include <optional>
#include "../../../cpu/uae_cpu/vclock.h"

/* MacPhoenix: the timer is a Qt thread sleeping until the next deadline.
   It raises the interrupt and wakes whichever host thread is idle in
   syncint_wait_interrupt() -- under the Process Manager that is the thread
   of whatever process holds the baton, not a fixed one (upstream's SIGALRM
   was aimed at a single thread). */
#include <QDeadlineTimer>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

using namespace Executor;

namespace
{
    constexpr int maxInterrupts = 10;
        
        // pointer to std::function to avoid static constructor
        // (so Interrupt() can be constructed at static construction time)
    std::array<std::function<void ()>*, maxInterrupts> interruptFunctions;
    std::array<std::atomic_flag, maxInterrupts> interruptTriggered;
    int nInterrupts = 0;


    Interrupt timerInterrupt;

    /* Idle guest-running threads wait here for an interrupt. */
    QMutex wake_mutex;
    QWaitCondition wake_cond;

    struct SyncintTimer
    {
        using clock = std::chrono::steady_clock;
        QThread *thread = nullptr;
        QMutex mutex;
        QWaitCondition cond;
        bool terminate = false;
        clock::time_point last_interrupt;
        clock::time_point scheduled_interrupt;

        SyncintTimer();
        ~SyncintTimer();

        void start();
        void post(std::chrono::microseconds usecs, bool fromLast);
        void wait();
    };

    SyncintTimer timer;

    /* MacPhoenix --deterministic: the timer runs on the virtual clock. post()
       records a deadline, the CPU loop's poll fires it, wait() skips the
       clock forward instead of sleeping. */
    constexpr uint64_t noDeadline = UINT64_MAX;
    uint64_t vDeadline = noDeadline, vLast = 0;

    void vclockPoll()
    {
        if(vDeadline != noDeadline && vclock_usec() >= vDeadline)
        {
            vLast = vDeadline;
            vDeadline = noDeadline;
            timerInterrupt.trigger();
        }
    }
}

Interrupt::Interrupt(std::function<void ()> f)
{
    assert(nInterrupts < maxInterrupts);
    index = nInterrupts++;

    interruptFunctions[index] = new std::function<void()>(f);
    interruptTriggered[index].test_and_set();
}

void Interrupt::trigger()
{
    if(index < 0)
        return;

    interruptTriggered[index].clear();

    interrupt_generate(M68K_TIMER_PRIORITY);
    getPowerCore().requestInterrupt();


    QMutexLocker lock(&wake_mutex);
    wake_cond.wakeAll();
}

void Interrupt::handleCommon()
{
    cpu_state.interrupt_pending[M68K_TIMER_PRIORITY] = false;
    getPowerCore().cancelInterrupt();

    /* We save the 68k registers and cc bits away. */
    M68kReg saved_regs[16];
    CCRElement saved_ccnz, saved_ccn, saved_ccc, saved_ccv, saved_ccx;
    memcpy(saved_regs, &cpu_state.regs, sizeof saved_regs);
    saved_ccnz = cpu_state.ccnz;
    saved_ccn = cpu_state.ccn;
    saved_ccc = cpu_state.ccc;
    saved_ccv = cpu_state.ccv;
    saved_ccx = cpu_state.ccx;

    /* There's no reason to think we need to decrement A7 by 32;
     * it's just a paranoid thing to do.
     */
    EM_A7 = (EM_A7 - 32) & ~3; /* Might as well long-align it. */

    for(int i = 0; i < nInterrupts; i++)
    {
        if(!interruptTriggered[i].test_and_set())
            (*interruptFunctions[i])();
    }

    memcpy(&cpu_state.regs, saved_regs, sizeof saved_regs);
    cpu_state.ccnz = saved_ccnz;
    cpu_state.ccn = saved_ccn;
    cpu_state.ccc = saved_ccc;
    cpu_state.ccv = saved_ccv;
    cpu_state.ccx = saved_ccx;
}

syn68k_addr_t Interrupt::handle68K(syn68k_addr_t interrupt_pc, void *unused)
{
    currentCPUMode = CPUMode::m68k;
    currentM68KPC = *ptr_from_longint<GUEST<uint32_t>*>(EM_A7 + 2);

    handleCommon();

    if(base::Debugger::instance && base::Debugger::instance->interruptRequested())
        return base::Debugger::instance->nmi68K(interrupt_pc);

    return MAGIC_RTE_ADDRESS;
}

void Interrupt::handlePowerPC(PowerCore& cpu)
{
    currentCPUMode = CPUMode::ppc;

    PowerCoreState saveState = (PowerCoreState&)cpu;

    cpu.r[1] -= 128;
    EM_A7 = cpu.r[1];
    handleCommon();
    cpu.r[1] += 128;
    EM_A7 = cpu.r[1];

    (PowerCoreState&)cpu = saveState;

    if(base::Debugger::instance && base::Debugger::instance->interruptRequested())
        base::Debugger::instance->nmiPPC();
}

void Executor::syncint_check_interrupt()
{
    if(INTERRUPT_PENDING())
    {
        syn68k_addr_t pc;

        pc = interrupt_process_any_pending(MAGIC_EXIT_EMULATOR_ADDRESS);
        if(pc != MAGIC_EXIT_EMULATOR_ADDRESS)
        {
            interpret_code(hash_lookup_code_and_create_if_needed(pc));
        }
    }
}

SyncintTimer::SyncintTimer()
{
    last_interrupt = scheduled_interrupt = clock::now();
}

void SyncintTimer::start()
{
    if(vclock_enabled)
        vclock_poll_hook = vclockPoll;
    thread = QThread::create([this] {
        QMutexLocker lock(&mutex);
        while(!terminate)
        {
            if(scheduled_interrupt > last_interrupt)
            {
                auto left = scheduled_interrupt - clock::now();
                if(left <= clock::duration::zero()
                   || !cond.wait(&mutex, QDeadlineTimer(
                          std::chrono::duration_cast<std::chrono::nanoseconds>(left),
                          Qt::PreciseTimer)))
                {
                    /* Not woken by a new post: the deadline passed. */
                    if(clock::now() >= scheduled_interrupt)
                    {
                        last_interrupt = scheduled_interrupt;
                        timerInterrupt.trigger();
                    }
                }
            }
            else
                cond.wait(&mutex);
        }
    });
    thread->setObjectName("syncint timer");
    thread->start(QThread::TimeCriticalPriority);
}

SyncintTimer::~SyncintTimer()
{
    if(thread)
    {
        {
            QMutexLocker lock(&mutex);
            terminate = true;
            cond.wakeAll();
        }
        thread->wait();
        delete thread;
    }
}

void SyncintTimer::post(std::chrono::microseconds usecs, bool fromLast)
{
    if(vclock_enabled)
    {
        vDeadline = (fromLast ? vLast : vclock_usec()) + usecs.count();
        return;
    }
    QMutexLocker lock(&mutex);

    auto time = (fromLast ? last_interrupt : clock::now()) + usecs;

    if(time <= last_interrupt)
        last_interrupt = clock::time_point();
    if(scheduled_interrupt <= last_interrupt || time < scheduled_interrupt)
        scheduled_interrupt = time;

    cond.wakeAll();
}

void SyncintTimer::wait()
{
    if(vclock_enabled)
    {
        /* Idle: jump to the next timer interrupt (or one tick if none). */
        vclock_skip_to(vDeadline != noDeadline ? vDeadline : vclock_usec() + 16667);
        vclockPoll();
        return;
    }
    /* Checked under the lock trigger() takes, so no wake-up is lost. */
    QMutexLocker lock(&wake_mutex);
    while(!INTERRUPT_PENDING())
        wake_cond.wait(&wake_mutex);
}

void Executor::syncint_init()
{
    /* Set up the trap vector for the timer interrupt. */
    *(GUEST<syn68k_addr_t> *)SYN68K_TO_US(M68K_TIMER_VECTOR * 4) = 
        callback_install(&Interrupt::handle68K, nullptr);

    getPowerCore().handleInterrupt = &Interrupt::handlePowerPC;

    for(auto& flag : interruptTriggered)
        flag.test_and_set();

    timerInterrupt = Interrupt(&timeInterruptHandler);
    timer.start();
}

void Executor::syncint_wait_interrupt()
{
    timer.wait();
    syncint_check_interrupt();
}

void Executor::syncint_post(std::chrono::microseconds usecs, bool fromLast)
{
    timer.post(usecs, fromLast);
}
