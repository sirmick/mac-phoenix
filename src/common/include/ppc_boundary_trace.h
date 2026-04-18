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

struct PpcBoundaryState {
    uint32_t pc;
    uint32_t selector;
    uint32_t cr;
    uint32_t xer;
    uint32_t lr;
    uint32_t ctr;
    uint32_t gpr[32];
};

static inline FILE* ppc_trace_stream_() {
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

static inline void ppc_trace_emul_op(const PpcBoundaryState& s) {
    FILE* fp = ppc_trace_stream_();
    if (!fp) return;
    static uint64_t seq = 0;
    ++seq;
    std::fprintf(fp,
        "%08llu EMULOP pc=%08x sel=%02x cr=%08x xer=%08x lr=%08x ctr=%08x",
        (unsigned long long)seq, s.pc, s.selector, s.cr, s.xer, s.lr, s.ctr);
    for (int i = 0; i < 32; ++i)
        std::fprintf(fp, " r%d=%08x", i, s.gpr[i]);
    std::fputc('\n', fp);
}
