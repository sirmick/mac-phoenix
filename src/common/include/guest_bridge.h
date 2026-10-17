/*
 * guest_bridge.h - a call from guest code into the emulator: the bridge
 * trap ($ABE5) that the next BridgeAgent talks to instead of files
 * (docs/CommandBridge.md, "Bridge v2").
 *
 * The guest calls it like a register-based Toolbox trap: D0 a selector,
 * A0 a pointer, D1 a length or value; the answer comes back in D0. Every
 * machine installs it: the ROM machines (68k and PowerPC) point the trap
 * table entry at a stub [EmulOp BRIDGE_COMM; rts] (bridge_trap_install(),
 * text_feed.cpp), Executor at a callback (romlib init.cpp). All of them land
 * here, so the protocol has one implementation.
 *
 * Demo selectors (the de-risking step): HELLO, PENDING (asked from a Time
 * Manager task: should the agent wake up?), REPORT (a line from the guest;
 * answers how long since PENDING said yes, in ms).
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace guest_bridge {

constexpr uint16_t kTrap = 0xABE5;          /* unimplemented on 7.5.5, 7.6.1 (68k and
                                               PowerPC), 9.0.4 and in Executor */
constexpr uint32_t kMagic = 0x4D506878;             /* 'MPhx': HELLO's answer */

enum Selector : uint32_t {
    kHello = 1,         /* D1: the agent's protocol version -> kMagic */
    kPending = 2,       /* -> 1 if the agent should wake (once), else 0 */
    kReport = 3,        /* A0/D1: a line of text -> ms since PENDING last said 1 */
};

using Clock = std::chrono::steady_clock;

struct State {
    bool hello = false;
    int rounds = 0;                     /* wake-ups asked for so far */
    Clock::time_point due{};            /* next wake-up the host asks for */
    Clock::time_point woke{};           /* when PENDING last said 1 */
    unsigned long pending_calls = 0;
};

inline State &state()
{
    static State s;
    return s;
}

inline long ms_since(Clock::time_point t)
{
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

/* One call. read8: guest memory, a byte at a time (every machine has it). */
template <typename Read8>
uint32_t call(uint32_t selector, uint32_t a0, uint32_t d1, Read8 read8)
{
    State &s = state();
    switch (selector) {
    case kHello:
        s.hello = true;
        s.rounds = 0;
        s.due = Clock::now() + std::chrono::seconds(2);   /* the demo: wake it in 2 s */
        fprintf(stderr, "[bridge2] HELLO from an agent, protocol %u\n", (unsigned)d1);
        return kMagic;
    case kPending:
        s.pending_calls++;
        if (s.due != Clock::time_point{} && Clock::now() >= s.due) {
            s.due = Clock::time_point{};
            s.woke = Clock::now();
            return 1;
        }
        return 0;
    case kReport: {
        std::string line;
        for (uint32_t i = 0; i < d1 && i < 512; i++)
            line += static_cast<char>(read8(a0 + i));
        long ms = s.woke != Clock::time_point{} ? ms_since(s.woke) : -1;
        fprintf(stderr, "[bridge2] guest: %s (host: %ld ms since the wake-up was asked for)\n",
                line.c_str(), ms);
        if (++s.rounds < 3)
            s.due = Clock::now() + std::chrono::seconds(1);
        return (uint32_t)ms;
    }
    default:
        fprintf(stderr, "[bridge2] unknown selector %u\n", (unsigned)selector);
        return 0xFFFFFFFF;
    }
}

}  // namespace guest_bridge
