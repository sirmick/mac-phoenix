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
#include <file/fsm.h>

using namespace Executor;

/* MacPhoenix: power off ends the machine, every process with it (with
   several processes, ExitToShell would only end the caller). Shutdown
   procs are not run (ShutDwnInstall is unimplemented). */
void Executor::C_ShutDwnPower()
{
    ROMlib_fsm_unmount_all(); /* a DOS disk's file system writes back */
    ROMlib_exit = true;
    fprintf(stderr, "[Executor] ShutDwnPower: powering off\n");
    fflush(nullptr);
    /* _exit: atexit handlers would unmap the video buffer under the host's
       frame loop; the parent clears the IPC state when we are gone. Status
       0, as the ROM machines' guest shutdown (ShutdownEmulator): a normal
       end, not one the web UI reports. */
    _exit(0);
}

/* MacPhoenix: restart ends the machine as power off does, with the status
   the parent takes for a restart (IPC_EXIT_GUEST_RESTART, ipc_protocol.h):
   it starts the child again, as a ROM machine reboots. */
void Executor::C_ShutDwnStart()
{
    ROMlib_fsm_unmount_all();
    ROMlib_exit = true;
    fprintf(stderr, "[Executor] ShutDwnStart: restarting\n");
    fflush(nullptr);
    _exit(75);
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
