/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
   Development, Inc.  All rights reserved.

   shutdown.c; ShutDown Manager routines */

#include <base/common.h>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <ShutDown.h>
#include <SegmentLdr.h>

#include <rsys/segment.h>

using namespace Executor;

/* MacPhoenix: power off ends the machine, every process with it (with
   several processes, ExitToShell would only end the caller). Shutdown
   procs are not run (ShutDwnInstall is unimplemented). */
void Executor::C_ShutDwnPower()
{
    ROMlib_exit = true;
    fprintf(stderr, "[Executor] ShutDwnPower: powering off\n");
    fflush(nullptr);
    /* _exit: atexit handlers would unmap the video buffer under the host's
       frame loop; the parent clears the IPC state when we are gone. 3 is
       the host's "guest powered off" (main.cpp, exit_with_guest). */
    _exit(3);
}

void Executor::C_ShutDwnStart()
{
    ExitToShell();
}

void Executor::C_ShutDwnInstall(ProcPtr shutdown_proc, int16_t flags)
{
    /* #warning "ShutDwnInstall unimplemented" */
    warning_unimplemented("");
}

void Executor::C_ShutDwnRemove(ProcPtr shutdown_proc)
{
    /* #warning "ShutDwnRemove unimplemented" */
    warning_unimplemented("");
}
