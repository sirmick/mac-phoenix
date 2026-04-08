/*
 * SmokeTest - Minimal guest app for MacPhoenix
 *
 * Iteration 1: Just call ExitToShell to prove launch + return works.
 * Iteration 2: Add Toolbox init.
 * Iteration 3: Add file I/O (write results to Host:test_results.txt).
 */
#include <Quickdraw.h>
#include <Files.h>
#include <Processes.h>
#include <Script.h>
#include <string.h>

int main(void)
{
    FSSpec spec;
    short refNum;
    OSErr err;
    long count;
    const char *msg = "PASS smoke_test\r";

    /* Minimal Toolbox init */
    InitGraf(&qd.thePort);

    /* Write result to Host:test_results.txt */
    err = FSMakeFSSpec(0, 0, "\pHost:test_results.txt", &spec);
    if (err == fnfErr)
        FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);

    err = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    if (err == noErr) {
        SetEOF(refNum, 0);
        count = strlen(msg);
        FSWrite(refNum, &count, msg);
        FSClose(refNum);
        FlushVol(NULL, 0);
    }

    ExitToShell();
    return 0;
}
