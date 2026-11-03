/* PartProbe: this application's partition as the Process Manager lays it
   out for a SIZE of 100K (PartProbe.r), against what real Macs give:
   measured on the G3 (8.6: preferred + 56K, 9.0.4: + 68K; the stack
   about DefltStack + 16K on both). Executor follows them by System version
   (process_layout_partition in romlib/process.cpp). PartProbePPC, the
   PowerPC build, adds its fragment: the Code Fragment Manager loads it
   into the application heap, the Process Manager makes room for it.
   Results in Host:part_results.txt (tests/test_guest_suite.sh). */
#include <Types.h>
#include <Memory.h>
#include <Processes.h>
#include <Files.h>
#include <Gestalt.h>
#include <CodeFragments.h>
#include <stdio.h>
#include <string.h>

#define L(a) (*(long *)(a))

#ifdef __POWERPC__
/* A fragment bigger than the partition's heap (SIZE 100K). */
static const unsigned char big[512 * 1024] = { 1, 2, 3 };

static long big_sum(void)
{
    long s = 0;
    unsigned i;
    for(i = 0; i < sizeof big; i += 4096)
        s += big[i];
    return s;
}
#endif

int main(void)
{
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    ProcessInfoRec info;
    THz z = ApplicationZone();
    long sysv = 0, extra, stack, want_extra, want_stack, fragment = 0;
    FSSpec spec;

    freopen("Host:part_results.txt", "w", stdout);
    Gestalt(gestaltSystemVersion, &sysv);
    info.processInfoLength = sizeof info;
    info.processName = NULL;
    info.processAppSpec = &spec;
    GetProcessInformation(&psn, &info);
    extra = (long)info.processSize - 102400;
#ifdef __POWERPC__
    {
        CInfoPBRec pb;
        memset(&pb, 0, sizeof pb);
        pb.hFileInfo.ioNamePtr = spec.name;
        pb.hFileInfo.ioVRefNum = spec.vRefNum;
        pb.hFileInfo.ioDirID = spec.parID;
        PBGetCatInfoSync(&pb);
        /* the 'cfrg' entry: the whole data fork, rounded up to 4 */
        fragment = (pb.hFileInfo.ioFlLgLen + 3) & ~3L;
    }
#endif
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
#ifdef __POWERPC__
    /* The fragment's length and 28K (7.6.1) or 50K (8.6, 9.0.4) on top;
       7.6.1: + 44K in all, the stack DefltStack + 10. */
    if(sysv >= 0x800)
        want_extra += fragment + 50 * 1024;
    else if(sysv >= 0x760)
        want_extra = 16 * 1024 + 28 * 1024 + fragment, want_stack = L(0x322) + 10;
    printf("INFO fragment %ld bytes\n", fragment);
#endif
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
#ifdef __POWERPC__
    /* PartProbePPC: where the Code Fragment Manager put this application's
       fragment (code and data, the 512K table among them) with virtual
       memory off: in the partition, the application heap, or elsewhere. */
    {
        long code = *(long *)main, data = (long)big;
        long lo = (long)info.processLocation, hi = lo + (long)info.processSize;
        THz sz = SystemZone();
        printf("INFO fragment: code %08lx data %08lx (table %ld bytes, sum %ld)\n",
               code, data, (long)sizeof big, big_sum());
        printf("INFO in partition: code %d data %d; in ApplZone: code %d data %d; "
               "in SysZone: code %d data %d\n",
               code >= lo && code < hi, data >= lo && data < hi,
               code >= (long)z && code < (long)z->bkLim, data >= (long)z && data < (long)z->bkLim,
               code >= (long)sz && code < (long)sz->bkLim, data >= (long)sz && data < (long)sz->bkLim);
        printf("INFO FreeMem %ld, ApplZone size %ld, TempFreeMem %ld\n",
               FreeMem(), (long)z->bkLim - (long)z, TempFreeMem());
    }
    /* A fragment's memory comes back when its last connection closes:
       twenty private copies of this one, each closed again (CodeWarrior
       IDE prepares and closes each of its plug-ins). */
    {
        long free0 = FreeMem(), temp0 = TempFreeMem(), loaded = 0, i;
        int ok = 1;
        for(i = 0; i < 20 && ok; i++)
        {
            ConnectionID cid = 0;
            Ptr mainAddr;
            Str255 err;
            OSErr e = GetDiskFragment(&spec, 0, kWholeFork, "\pPartProbeCopy", kLoadNewCopy,
                                      &cid, &mainAddr, err);
            if(e != noErr)
            {
                printf("INFO copy %ld: GetDiskFragment %d\n", i, e);
                ok = 0;
                break;
            }
            if(i == 0)
                loaded = (free0 - FreeMem()) + (temp0 - TempFreeMem());
            CloseConnection(&cid);
        }
        printf("INFO copies: one took %ld bytes; FreeMem %ld -> %ld, TempFreeMem %ld -> %ld\n",
               loaded, free0, FreeMem(), temp0, TempFreeMem());
        printf("%s fragment copies closed: 20 loaded, memory back (within 32K)\n",
               ok && free0 - FreeMem() < 32768 && temp0 - TempFreeMem() < 32768 ? "PASS" : "FAIL");
    }
#endif
    printf("DONE 0\n---\n");
    return 0;
}
