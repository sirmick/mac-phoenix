/*
 * bridge_init.c - MacPhoenix Automation Bridge INIT
 *
 * A System extension that polls for command files on the ExtFS "Host" volume
 * and executes them. Communication is entirely file-based — no EmulOps,
 * no inline asm, no host-specific code. Works on any CPU (m68k, PPC)
 * and System 7+.
 *
 * Protocol:
 *   Host writes "Bridge:_bridge_cmd" with command text (e.g. "LAUNCH path")
 *   INIT reads the file, deletes it, executes the command
 *   INIT writes "Bridge:_bridge_result" with the error code
 *   Host polls for _bridge_result, reads it, deletes it
 *
 * Built with Retro68. Embedded in the emulator binary at compile time.
 */
#include <Files.h>
#include <Processes.h>
#include <Events.h>
#include <LowMem.h>
#include <Memory.h>
#include <Gestalt.h>
#include <Script.h>
#include <string.h>
#include <stdio.h>
#include "Retro68Runtime.h"

/* Forward declarations */
static void check_bridge(void);
extern OSErr do_launch(const unsigned char *path);

/* Saved old jGNEFilter (GetNextEventFilterUPP = pascal void (*)()) */
static GetNextEventFilterUPP gOldFilter = NULL;

/*
 * jGNEFilter callback.
 * Called from GetNextEvent — we check for bridge commands.
 */
static pascal void bridge_filter(void)
{
    check_bridge();

    /* Chain to old filter */
    if (gOldFilter)
        gOldFilter();
}

/*
 * Check for a command file on the Host volume and execute it.
 * Called from the jGNEFilter on every GetNextEvent.
 */
static void check_bridge(void)
{
    FSSpec cmd_spec;
    OSErr err;
    short refNum;
    long count;
    char cmd[256];

    /* Write heartbeat every ~2 seconds (120 ticks).
     * Uses _bridge_heartbeat file. Only writes if no command is pending
     * (so we don't interfere with the command/result flow). */
    {
        static unsigned long last_hb_ticks = 0;
        unsigned long now = *(unsigned long *)0x016A;  /* Ticks (low memory) */
        if (now - last_hb_ticks > 120) {
            last_hb_ticks = now;
            /* Try to open heartbeat file — if host pre-created it, write to it */
            FSSpec hb_spec;
            err = FSMakeFSSpec(0, 0, "\pHost:_bridge_heartbeat", &hb_spec);
            if (err == noErr) {
                short hb_ref;
                if (FSpOpenDF(&hb_spec, fsWrPerm, &hb_ref) == noErr) {
                    static int hb_count = 0;
                    char hb_buf[16];
                    long hb_len;
                    hb_count++;
                    hb_len = snprintf(hb_buf, sizeof(hb_buf), "%d\r", hb_count);
                    SetEOF(hb_ref, 0);
                    FSWrite(hb_ref, &hb_len, hb_buf);
                    FSClose(hb_ref);
                    FlushVol(NULL, 0);
                }
            }
        }
    }

    /* Check if command file exists */
    err = FSMakeFSSpec(0, 0, "\pHost:_bridge_cmd", &cmd_spec);
    if (err != noErr)
        return;  /* no command pending */

    /* Read command */
    err = FSpOpenDF(&cmd_spec, fsRdPerm, &refNum);
    if (err != noErr)
        return;

    count = sizeof(cmd) - 1;
    memset(cmd, 0, sizeof(cmd));
    err = FSRead(refNum, &count, cmd);
    FSClose(refNum);
    FSpDelete(&cmd_spec);

    if (err != noErr && err != eofErr)
        return;

    cmd[count] = '\0';

    /* Parse and execute command */
    OSErr result = -1;

    if (strncmp(cmd, "LAUNCH ", 7) == 0) {
        /* Build Pascal string from the C string path */
        unsigned char path[256];
        int len = strlen(cmd + 7);
        /* Strip trailing whitespace/newlines */
        while (len > 0 && (cmd[7 + len - 1] == '\n' || cmd[7 + len - 1] == '\r'
                        || cmd[7 + len - 1] == ' '))
            len--;
        if (len > 255) len = 255;
        path[0] = (unsigned char)len;
        memcpy(path + 1, cmd + 7, len);

        result = do_launch(path);

    } else if (strncmp(cmd, "QUIT", 4) == 0) {
        result = 0;
        /* Write result BEFORE quitting */
        {
            FSSpec res_spec;
            char res_str[16];
            long rlen;
            short rref;
            FSMakeFSSpec(0, 0, "\pHost:_bridge_result", &res_spec);
            FSpDelete(&res_spec);
            FSpCreate(&res_spec, 'ttxt', 'TEXT', smSystemScript);
            FSpOpenDF(&res_spec, fsWrPerm, &rref);
            rlen = snprintf(res_str, sizeof(res_str), "%d\r", 0);
            FSWrite(rref, &rlen, res_str);
            FSClose(rref);
            FlushVol(NULL, 0);
        }
        ExitToShell();
        return;  /* not reached */
    }

    /* Write result */
    {
        FSSpec res_spec;
        char res_str[16];
        long rlen;
        short rref;

        FSMakeFSSpec(0, 0, "\pHost:_bridge_result", &res_spec);
        FSpDelete(&res_spec);
        FSpCreate(&res_spec, 'ttxt', 'TEXT', smSystemScript);
        if (FSpOpenDF(&res_spec, fsWrPerm, &rref) == noErr) {
            rlen = snprintf(res_str, sizeof(res_str), "%d\r", (int)result);
            FSWrite(rref, &rlen, res_str);
            FSClose(rref);
            FlushVol(NULL, 0);
        }
    }
}

/*
 * INIT entry point.
 */
void _start(void)
{
    long sysVersion;

    RETRO68_RELOCATE();

    /* Check System version — require System 7+ for FSMakeFSSpec.
     * Use low memory SysVersion (0x015A) directly since Gestalt may not
     * be available at early boot when injected by the host. */
    sysVersion = *(long *)0x015A;
    if (sysVersion != 0 && sysVersion < 0x0700)
        return;

    /* Save old filter — will be restored/chained by bridge_filter */
    gOldFilter = LMGetGNEFilter();

    /* Install our filter.
     *
     * NOTE: This may be overwritten by other extensions during boot.
     * The host does a deferred reinstall at 0x29A after Finder starts.
     * We still install here in case no other extension overwrites us. */
    LMSetGNEFilter((GetNextEventFilterUPP)bridge_filter);
}
