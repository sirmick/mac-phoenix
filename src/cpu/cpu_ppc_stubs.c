/*
 *  cpu_ppc_stubs.c - Stub PPC backend install functions
 *
 *  These stubs allow the project to link while PPC backends are being
 *  implemented. Will be replaced by real implementations.
 */

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>

void cpu_ppc_kpx_install(Platform *p)
{
    fprintf(stderr, "FATAL: PPC KPX backend not yet implemented\n");
    p->cpu_name = "PPC-KPX (stub)";
    exit(1);
}

void cpu_ppc_dualcpu_install(Platform *p)
{
    fprintf(stderr, "FATAL: PPC DualCPU backend not yet implemented\n");
    p->cpu_name = "PPC-DualCPU (stub)";
    exit(1);
}
