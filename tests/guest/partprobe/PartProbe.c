/* PartProbe: this application's partition as the Process Manager lays it
   out for a SIZE of 100K (PartProbe.r), against what real Macs give:
   measured on the G3 (8.6: preferred + 56K, 9.0.4: + 68K; the stack
   about DefltStack + 16K on both). Executor follows them by System version
   (process_layout_partition in romlib/process.cpp). Results in
   Host:part_results.txt (tests/test_guest_suite.sh). */
#include <Types.h>
#include <Memory.h>
#include <Processes.h>
#include <Gestalt.h>
#include <stdio.h>

#define L(a) (*(long *)(a))

int main(void)
{
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    ProcessInfoRec info;
    THz z = ApplicationZone();
    long sysv = 0, extra, stack, want_extra, want_stack;

    freopen("Host:part_results.txt", "w", stdout);
    Gestalt(gestaltSystemVersion, &sysv);
    info.processInfoLength = sizeof info;
    info.processName = NULL;
    info.processAppSpec = NULL;
    GetProcessInformation(&psn, &info);
    extra = (long)info.processSize - 102400;
    stack = L(0x908) - L(0x130);   /* CurStackBase - ApplLimit */
    printf("INFO System %lx: processSize %ld (SIZE + %ld) location %08lx\n",
           sysv, (long)info.processSize, extra, (long)info.processLocation);
    printf("INFO ApplZone %08lx bkLim %08lx ApplLimit %08lx CurStackBase %08lx CurrentA5 %08lx\n",
           (long)z, (long)z->bkLim, L(0x130), L(0x908), L(0x904));
    printf("INFO heap to ApplLimit %ld, stack %ld (DefltStack %ld), A5 world %ld\n",
           L(0x130) - (long)z, stack, L(0x322),
           (long)info.processLocation + (long)info.processSize - L(0x908));
    if(sysv >= 0x900)
        want_extra = 68 * 1024, want_stack = L(0x322) + 16 * 1024 + 8;
    else if(sysv >= 0x800)
        want_extra = 56 * 1024, want_stack = L(0x322) + 16 * 1024 + 8;
    else
        want_extra = -1, want_stack = -1;   /* not measured here */
    if(want_extra < 0)
        printf("SKIP partition: no measurement for System %lx\n", sysv);
    else
    {
        printf("%s partition: SIZE + %ld (a Mac: + %ld)\n",
               extra == want_extra ? "PASS" : "FAIL", extra, want_extra);
        /* a Mac's moves by a few bytes with the A5 world's size */
        printf("%s stack: %ld (a Mac: %ld, within 32)\n",
               stack >= want_stack - 32 && stack <= want_stack + 32 ? "PASS" : "FAIL",
               stack, want_stack);
    }
    printf("DONE 0\n---\n");
    return 0;
}
