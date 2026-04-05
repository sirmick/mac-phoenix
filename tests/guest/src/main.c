/*
 * MacTestSuite - Guest-side driver test suite for MacPhoenix
 *
 * Runs inside the emulated Mac OS environment.
 * Writes results to Host:test_results.txt (ExtFS volume).
 * Host-side runner collects results after app exits to Finder.
 *
 * Build with Retro68:
 *   m68k-apple-macos-gcc -o MacTestSuite main.c test_extfs.c test_audio.c \
 *       test_serial.c test_network.c test_disk.c
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>

#include "test_report.h"

/* Test function declarations */
extern void test_extfs(void);
extern void test_audio(void);
extern void test_serial(void);
extern void test_network(void);
extern void test_opentransport(void);
extern void test_disk(void);

int main(void)
{
    /* Standard Mac Toolbox init */
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    MaxApplZone();

    /* Open results file */
    report_init();

    /* Run test suites */
    test_disk();
    test_extfs();
    test_audio();
    test_serial();
    test_network();
    test_opentransport();

    /* Write summary and close */
    report_finish();

    /* Return to Finder — host detects this via /api/wait app=Finder */
    ExitToShell();
    return 0;
}
