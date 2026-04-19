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
