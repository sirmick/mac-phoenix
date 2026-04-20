// Canonical per-EmulOp state trace for KPX-vs-Unicorn-PPC divergence debugging.
//
// Enable with MACEMU_PPC_TRACE=/path/to/trace.log. Each backend writes one
// fixed-format line per EmulOp entry; diff the two logs to find the first
// divergence point.
//
// Header-only: both backends include it and each gets its own static FILE*.
// Only one backend runs per process, so there's no sharing concern.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct PpcBoundaryState {
    uint32_t pc;
    uint32_t selector;
    uint32_t cr;
    uint32_t xer;
    uint32_t lr;
    uint32_t ctr;
    uint32_t gpr[32];
};

// NOTE: plain `inline` (not `static inline`) so function-local statics are
// shared across TUs. The KPX backend links both cpu_ppc_kpx.cpp and
// src/cpu/ppc/ppc-cpu.cpp — with internal linkage each TU would get its own
// seq counter and CR tracing would never activate for the interpreter loop.
inline FILE* ppc_trace_stream_() {
    static FILE* fp = nullptr;
    static bool inited = false;
    if (!inited) {
        inited = true;
        const char* path = std::getenv("MACEMU_PPC_TRACE");
        if (path && *path) {
            fp = std::fopen(path, "w");
            if (!fp) {
                std::fprintf(stderr, "[ppc_trace] fopen('%s') failed\n", path);
            } else {
                std::setvbuf(fp, nullptr, _IOLBF, 0);
                std::fprintf(stderr, "[ppc_trace] writing to %s\n", path);
            }
        }
    }
    return fp;
}

inline uint64_t& ppc_trace_seq_() {
    static uint64_t seq = 0;
    return seq;
}

inline void ppc_trace_emul_op(const PpcBoundaryState& s) {
    FILE* fp = ppc_trace_stream_();
    if (!fp) return;
    ++ppc_trace_seq_();
    std::fprintf(fp,
        "%08llu EMULOP pc=%08x sel=%02x cr=%08x xer=%08x lr=%08x ctr=%08x",
        (unsigned long long)ppc_trace_seq_(), s.pc, s.selector, s.cr, s.xer, s.lr, s.ctr);
    for (int i = 0; i < 32; ++i)
        std::fprintf(fp, " r%d=%08x", i, s.gpr[i]);
    std::fputc('\n', fp);
}

// Emit post-handler state for the most recently dispatched EmulOp. Uses the
// same sequence number as its EMULOP pre-state line so pairs align when the
// two backends' traces are diffed.
inline void ppc_trace_emul_op_post(const PpcBoundaryState& s) {
    FILE* fp = ppc_trace_stream_();
    if (!fp) return;
    std::fprintf(fp,
        "%08llu POST   pc=%08x sel=%02x cr=%08x xer=%08x lr=%08x ctr=%08x",
        (unsigned long long)ppc_trace_seq_(), s.pc, s.selector, s.cr, s.xer, s.lr, s.ctr);
    for (int i = 0; i < 32; ++i)
        std::fprintf(fp, " r%d=%08x", i, s.gpr[i]);
    std::fputc('\n', fp);
}

// Per-instruction CR tracer. Gated by MACEMU_PPC_CR2_TRACE=<lo>[:<hi>] where
// lo/hi are EmulOp seq bounds (half-open). Both backends emit the same line
// format so /tmp/kpx.cr.log and /tmp/unicorn.cr.log can be diffed directly.
// Output goes to stderr (lowest-overhead path; trace file is line-buffered
// and would serialize the ~3M-line bursts too slowly).
struct PpcCrTraceWindow {
    uint64_t lo;
    uint64_t hi;
    bool enabled;
};

inline const PpcCrTraceWindow& ppc_cr_trace_window_() {
    static PpcCrTraceWindow w = []() {
        PpcCrTraceWindow r{0, 0, false};
        const char* tr = std::getenv("MACEMU_PPC_CR2_TRACE");
        if (!tr || !*tr || *tr == '0') return r;
        const char* colon = std::strchr(tr, ':');
        if (colon) {
            r.lo = std::strtoull(tr, nullptr, 10);
            r.hi = std::strtoull(colon + 1, nullptr, 10);
        } else {
            r.lo = std::strtoull(tr, nullptr, 10);
            r.hi = r.lo + 1;
        }
        r.enabled = true;
        return r;
    }();
    return w;
}

inline bool ppc_cr_trace_active_() {
    const auto& w = ppc_cr_trace_window_();
    if (!w.enabled) return false;
    uint64_t s = ppc_trace_seq_();
    return s >= w.lo && s < w.hi;
}

inline void ppc_trace_cr_step(uint32_t pc, uint32_t op, uint32_t cr,
                                     uint32_t lr, uint32_t r24) {
    if (!ppc_cr_trace_active_()) return;
    std::fprintf(stderr, "[CR] seq=%llu pc=%08x op=%08x cr=%08x lr=%08x r24=%08x\n",
                 (unsigned long long)ppc_trace_seq_(), pc, op, cr, lr, r24);
}

// 68k-PC entry tracer. Gated by MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,<hex>...].
// Fires when gpr(24) (68k PC) matches any target; dumps D0..D7/A0..A7 in the
// same format as the Unicorn-PPC tracer so KPX and Unicorn logs can be diffed.
// First MACEMU_PPC_TRACE_68K_MAX (default 5) hits per target are emitted, then
// silenced to prevent flooding when the match lands inside a hot loop.
struct PpcTrace68kPc {
    uint32_t targets[8];
    int hit_count[8];
    int n;
    int max_per_target;
    bool enabled;
};

inline PpcTrace68kPc& ppc_trace_68k_pc_state_() {
    static PpcTrace68kPc s = []() {
        PpcTrace68kPc r{};
        const char* env = std::getenv("MACEMU_PPC_TRACE_68K_ENTRY");
        if (!env || !*env) return r;
        char buf[256];
        std::strncpy(buf, env, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char* saveptr = nullptr;
        for (char* tok = strtok_r(buf, ",", &saveptr);
             tok && r.n < 8;
             tok = strtok_r(nullptr, ",", &saveptr)) {
            uint32_t v = (uint32_t)std::strtoul(tok, nullptr, 0);
            if (!v) continue;
            r.targets[r.n++] = v;
        }
        if (r.n == 0) return r;
        const char* mx = std::getenv("MACEMU_PPC_TRACE_68K_MAX");
        r.max_per_target = (mx && *mx) ? std::atoi(mx) : 5;
        if (r.max_per_target <= 0) r.max_per_target = 1;
        r.enabled = true;
        return r;
    }();
    return s;
}

// Returns target index if gpr(24) matches and quota not exhausted, else -1.
inline int ppc_trace_68k_pc_match_(uint32_t r24) {
    auto& s = ppc_trace_68k_pc_state_();
    if (!s.enabled) return -1;
    for (int i = 0; i < s.n; ++i) {
        if (s.targets[i] == r24) {
            if (s.hit_count[i] >= s.max_per_target) return -1;
            return i;
        }
    }
    return -1;
}

inline void ppc_trace_68k_pc_dump_(int tgt_idx, uint32_t ppc_pc,
                                    const uint32_t* gprs) {
    auto& s = ppc_trace_68k_pc_state_();
    int hit = ++s.hit_count[tgt_idx];
    std::fprintf(stderr, "[68K-ENTRY #%d] r24=0x%08x ppc_pc=0x%08x target_idx=%d\n",
                 hit, gprs[24], ppc_pc, tgt_idx);
    std::fprintf(stderr, "  [68k D0..D7] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                 gprs[8], gprs[9], gprs[10], gprs[11],
                 gprs[12], gprs[13], gprs[14], gprs[15]);
    std::fprintf(stderr, "  [68k A0..A7] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                 gprs[16], gprs[17], gprs[18], gprs[19],
                 gprs[20], gprs[21], gprs[22], gprs[23]);
    std::fprintf(stderr, "  [gpr(1)=SP]  %08x\n", gprs[1]);
}
