/*
 * guest_bridge.h - a call from guest code into the emulator: the bridge
 * trap ($ABE5) BridgeAgent talks to instead of files (docs/CommandBridge.md,
 * "Bridge v2").
 *
 * The guest calls it like a register-based Toolbox trap: D0 a selector,
 * A0 a pointer, D1 a length or value; the answer comes back in D0. Every
 * machine installs it: the ROM machines (68k and PowerPC) point the trap
 * table entry at a stub [EmulOp BRIDGE_COMM; rts] (bridge_trap_install(),
 * text_feed.cpp), Executor at a callback (romlib init.cpp). All of them land
 * in call(), so the protocol has one implementation.
 *
 * Requests (POST /api/exec, ...) go through an IPCBridgeSlot: the HTTP side
 * calls request(), which writes the slot and waits; the agent's Time Manager
 * task sees AGENT_PENDING, wakes the agent (WakeUpProcess), which takes the
 * request with NEXT and answers with REPLY. A request: a verb line ending in
 * CR ("EXEC MPSX 600"), then its payload. A reply: what the verb defines
 * (EXEC: "STDOUT n\r..STDERR n\r..STATUS n\r", as the file transport's).
 *
 * The demo selectors (HELLO, REPORT; tests/guest/bridgedemo) stay: HELLO
 * asks the host to schedule three wake-ups, REPORT says how long each took.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "../../ipc/ipc_protocol.h"

namespace guest_bridge {

constexpr uint16_t kTrap = 0xABE5;          /* unimplemented on 7.5.5, 7.6.1 (68k and
                                               PowerPC), 9.0.4 and in Executor */
constexpr uint32_t kMagic = 0x4D506878;             /* 'MPhx': HELLO's answer */

enum Selector : uint32_t {
    kHello = 1,         /* demo: D1 protocol -> kMagic; schedules three wake-ups */
    kPending = 2,       /* demo: -> 1 once a scheduled wake-up is due */
    kReport = 3,        /* demo: A0/D1 a line of text -> ms since PENDING last said 1 */
    kAgentHello = 0x10, /* the agent: D1 its protocol version -> kMagic */
    kNext = 0x11,       /* A0/D1 the agent's buffer -> the request's length (copied in),
                           -1 none, $80000000 | length if the buffer is too small */
    kReply = 0x12,      /* A0/D1 the reply to the request taken last -> 0 */
    kAgentPending = 0x13, /* the agent's timer task: -> 1 if a request waits */
    kState = 0x14,      /* A0/D1 the front process's name -> 0 */
};

using Clock = std::chrono::steady_clock;

struct State {
    int rounds = 0;                     /* demo: wake-ups asked for so far */
    Clock::time_point due{};            /* demo: next wake-up the host asks for */
    Clock::time_point woke{};           /* demo: when PENDING last said 1 */
};

inline State &state()
{
    static State s;
    return s;
}

/* The slot: the IPC buffer's when the CPU runs in a child process (the
 * child's start-up and the parent's HTTP side point at it), else this
 * process's own. */
inline IPCBridgeSlot *&slot_ptr()
{
    static IPCBridgeSlot *p = nullptr;
    return p;
}

inline IPCBridgeSlot *local_slot()
{
    static IPCBridgeSlot *s = new IPCBridgeSlot();      /* 2 MB: not static storage in every user */
    return s;
}

inline void set_slot(IPCBridgeSlot *s) { slot_ptr() = s; }
inline IPCBridgeSlot *slot() { return slot_ptr() ? slot_ptr() : local_slot(); }

inline long ms_since(Clock::time_point t)
{
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

inline bool request_waits(IPCBridgeSlot *b)
{
    uint32_t id = IPC_ATOMIC_LOAD(b->req_id);
    return id != 0 && id != IPC_ATOMIC_LOAD(b->taken_id);
}

/* One call from the guest. read8/write8: guest memory a byte at a time
 * (every machine has them). */
template <typename Read8, typename Write8>
uint32_t call(uint32_t selector, uint32_t a0, uint32_t d1, Read8 read8, Write8 write8)
{
    State &s = state();
    IPCBridgeSlot *b = slot();
    switch (selector) {
    case kHello:
        s.rounds = 0;
        s.due = Clock::now() + std::chrono::seconds(2);   /* the demo: wake it in 2 s */
        fprintf(stderr, "[bridge2] HELLO from the demo, protocol %u\n", (unsigned)d1);
        return kMagic;
    case kAgentHello:               /* D1 0: the agent is going (files only again) */
        IPC_ATOMIC_STORE(b->agent, d1);
        if (d1 == 0)
            IPC_ATOMIC_STORE(b->front_seq, 0u);
        fprintf(stderr, "[bridge2] BridgeAgent speaks protocol %u\n", (unsigned)d1);
        return kMagic;
    case kState: {
        uint32_t n = d1 < sizeof b->front_app - 1 ? d1 : (uint32_t)sizeof b->front_app - 1;
        for (uint32_t i = 0; i < n; i++)
            b->front_app[i] = (char)read8(a0 + i);
        b->front_app[n] = 0;
        IPC_ATOMIC_STORE(b->front_seq, IPC_ATOMIC_LOAD(b->front_seq) + 1);
        return 0;
    }
    case kAgentPending:
        return request_waits(b) ? 1 : 0;
    case kPending:
        if (s.due != Clock::time_point{} && Clock::now() >= s.due) {
            s.due = Clock::time_point{};
            s.woke = Clock::now();
            return 1;
        }
        return 0;
    case kNext: {
        if (!request_waits(b))
            return (uint32_t)-1;
        uint32_t id = IPC_ATOMIC_LOAD(b->req_id);
        uint32_t n = b->req_len;
        if (n > d1)
            return 0x80000000u | n;
        for (uint32_t i = 0; i < n; i++)
            write8(a0 + i, (uint8_t)b->req[i]);
        IPC_ATOMIC_STORE(b->taken_id, id);
        return n;
    }
    case kReply: {
        uint32_t n = d1 < IPC_BRIDGE_REP_MAX ? d1 : IPC_BRIDGE_REP_MAX;
        for (uint32_t i = 0; i < n; i++)
            b->rep[i] = (char)read8(a0 + i);
        b->rep_len = n;
        IPC_ATOMIC_STORE(b->rep_id, IPC_ATOMIC_LOAD(b->taken_id));
        return 0;
    }
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

/* The agent's protocol, 0 if no agent has said AGENT_HELLO. */
inline uint32_t agent(IPCBridgeSlot *b) { return IPC_ATOMIC_LOAD(b->agent); }

/* A machine reset: the agent and what it reported belong to the last boot. */
inline void reset()
{
    IPCBridgeSlot *b = slot();
    IPC_ATOMIC_STORE(b->agent, 0u);
    IPC_ATOMIC_STORE(b->front_seq, 0u);
}

/* The front process's name, as the agent reported it (STATE); empty if
 * no agent reports it. */
inline std::string front_app(IPCBridgeSlot *b)
{
    if (!b || agent(b) < 2 || IPC_ATOMIC_LOAD(b->front_seq) == 0)
        return {};
    return std::string(b->front_app);
}

/* The host side: one request, its reply. Serialized (one at a time, in
 * order of arrival); take_ms: how long the agent has to take it; reply_ms:
 * then to answer. False with err set on a timeout. */
inline bool request(IPCBridgeSlot *b, const std::string &req, std::string &reply,
                    int take_ms, int reply_ms, std::string &err)
{
    static std::mutex m;
    static uint32_t next_id = 0;
    std::lock_guard<std::mutex> lock(m);
    if (req.size() > IPC_BRIDGE_REQ_MAX) {
        err = "request too large";
        return false;
    }
    uint32_t id = ++next_id;
    if (id == 0)
        id = ++next_id;
    memcpy(b->req, req.data(), req.size());
    b->req_len = (uint32_t)req.size();
    IPC_ATOMIC_STORE(b->req_id, id);
    auto wait_for = [&](IPC_ATOMIC_UINT32 &field, int ms) {
        auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (IPC_ATOMIC_LOAD(field) != id) {
            if (Clock::now() > end)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    };
    bool ok = wait_for(b->taken_id, take_ms);
    if (!ok)
        err = "the agent did not take the request";
    else if (!(ok = wait_for(b->rep_id, reply_ms)))
        err = "no reply from the agent";
    else
        reply.assign(b->rep, b->rep_len);
    IPC_ATOMIC_STORE(b->req_id, 0u);
    return ok;
}

}  // namespace guest_bridge
