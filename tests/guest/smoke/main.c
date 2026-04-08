/*
 * SmokeTest - Uses the same test functions as MacTestSuite
 * but with incremental expansion to find what hangs.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Files.h>
#include <Processes.h>
#include <Script.h>
#include <string.h>
#include <stdio.h>

#include "../src/test_report.h"

/* Same test functions as MacTestSuite */
extern void test_disk(void);
extern void test_extfs(void);
extern void test_audio(void);
extern void test_serial(void);
extern void test_network(void);
extern void test_opentransport(void);

int main(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    MaxApplZone();

    report_init();

    test_disk();
    test_extfs();
    test_audio();
    test_serial();
    test_network();
    test_opentransport();

    report_finish();
    ExitToShell();
    return 0;
}
