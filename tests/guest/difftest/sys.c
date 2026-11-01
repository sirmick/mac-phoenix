/* sys: what a program asks the system first, differential
   (tools/difftest/run.py sys): Gestalt selectors, FindFolder for each
   folder type (and the folder's name), value-holding low-memory globals,
   the process's own information, Script Manager variables, which libraries
   the Code Fragment Manager finds without preparing them. Machine-specific
   answers (RAM, free memory) are keys with "~": shown, not compared. */
#include "probe.h"
#include <Gestalt.h>
#include <Folders.h>
#include <Files.h>
#include <Processes.h>
#include <Script.h>
#include <Devices.h>
#include <CodeFragments.h>
#include <Quickdraw.h>

#define SET(x) memset(&(x), SENTINEL, sizeof(x))

static const OSType selectors[] = {
    'sysv', 'sysa', 'sysu', 'mach', 'proc', 'cput', 'fpu ', 'mmu ', 'ram ', 'lram', 'vm  ', 'vmbs',
    'qd  ', 'qdrw', 'qdtx', 'appr', 'apvr', 'dict', 'drag', 'thds', 'thd#', 'cfrg', 'cfm ', 'mixd',
    'ppcf', 'ppc ', 'ppct', 'os  ', 'osat', 'ascv', 'ascr', 'evnt', 'edtn', 'fold', 'font', 'fs  ',
    'fxfr', 'hfs+', 'help', 'icmp', 'icon', 'kbd ', 'mmap', 'menu', 'mnu ', 'nlib', 'nmgr', 'ntrb',
    'otan', 'otra', 'otvr', 'pclk', 'popt', 'prmp', 'prcs', 'prtv', 'ptch', 'qtim', 'resr', 'rsrc',
    'scri', 'scrn', 'sdvr', 'shut', 'slot', 'snd ', 'sclk', 'stdf', 'strm', 'tbtt', 'tsmv', 'tsmb',
    'text', 'teFT', 'tecv', 'tmgr', 'tsvr', 'ucvt', 'utsk', 'vers', 'wma.', 'wind', 'xttt', 'bugx',
    'alis', 'aete', 'hdwr', 'romv', 'rbv ', 'twsr', 'aevt', 'ctbx', 'ctrl', 'dlgx', 'fnt#', 'flst',
    'icmr', 'icns', 'inet', 'mbrk', 'mpcp', 'navf', 'pmgr', 'pwr ', 'qtvr', 'semp', 'sysp', 'tsfa' };

static const OSType folders[] = {
    'macs', 'pref', 'extn', 'ctrl', 'strt', 'desk', 'trsh', 'empt', 'amnu', 'prnt', 'font', 'temp',
    'pmsf', 'asup', 'sprf', 'sdev', 'ctlp', 'cmnu', 'flnt', 'shdf', 'shdl', 'vdev', 'ppfd', 'vdfd' };

static const long lowmem[][2] = {   /* address, size */
    { 0x15A, 2 }, { 0x18E, 2 }, { 0x190, 2 }, { 0x2F0, 4 }, { 0x2F4, 4 }, { 0x28E, 2 },
    { 0x31E, 4 }, { 0x322, 4 }, { 0x984, 2 }, { 0xBA6, 2 }, { 0xBA8, 2 }, { 0xBAA, 2 },
    { 0x260, 2 }, { 0x262, 2 }, { 0x8F0, 2 }, { 0x8F3, 1 }, { 0x910, 32 }, { 0xA4A, 2 },
    { 0x1FA, 2 }, { 0x204, 2 }, { 0x206, 2 }, { 0x208, 2 }, { 0x20A, 2 }, { 0x20C, 4 },
    { 0x938, 2 }, { 0xA60, 2 }, { 0xCCA, 2 }, { 0xDA2, 2 }, { 0x12F0, 2 }, { 0xB21, 1 } };

/* ---- heap: where the Memory Manager puts a nonrelocatable block and what
   it moves, purges or asks the grow-zone procedure for to make room
   (Slithereens' sounds: HLockHi, NewPtr, HUnlock, HPurge per sound left
   its pointers among the sounds on Executor). Each case runs in a zone of
   its own (InitZone in a block of the application heap), so the layout is
   the same on both machines; results are kept while the zone is current
   and printed after: printf allocates. */
#define ZSIZE (256L * 1024)
static Ptr zmem;
static THz zone, appzone;
static long gz_calls, gz_need;
static int gz_purged_first;   /* the purgeable handle was already purged */
static Handle gz_watch;
static char hres[64][80];
static char hkey[64][48];
static int hn;

static pascal long probe_gz(Size cbNeeded)
{
    gz_calls++;
    gz_need = cbNeeded;
    if(gz_watch && gz_calls == 1)
        gz_purged_first = *gz_watch == NULL;
    return 0;
}

static void hout(const char *key, const char *fmt, ...)
{
    va_list ap;
    if(hn >= 64)
        return;
    strncpy(hkey[hn], key, sizeof hkey[0] - 1);
    va_start(ap, fmt);
    vsprintf(hres[hn], fmt, ap);
    va_end(ap);
    hn++;
}

static void zbegin(void)
{
    SetZone(appzone);
    InitZone(NULL, 64, zmem + ZSIZE, (THz)zmem);
    zone = (THz)zmem;
    SetZone(zone);
}

static const char *where(Ptr p, Ptr q)
{
    return !p || !q ? "nil" : p < q ? "below" : "above";
}

static void probe_heap(void)
{
    int i;
    GrowZoneUPP gzupp = NewGrowZoneUPP(probe_gz);
    appzone = GetZone();
    zmem = NewPtr(ZSIZE);
    if(!zmem)
    {
        out("zone", "no memory");
        return;
    }
    hn = 0;
    {
        /* An unlocked handle at the bottom of the zone: the pointer goes
           below it and the handle moves up? */
        Handle h;
        Ptr was, p;
        zbegin();
        h = NewHandle(8192);
        was = *h;
        p = NewPtr(1024);
        hout("unlocked.ptr", "%s the handle", where(p, *h));
        hout("unlocked.handle", "%s", *h == was ? "stayed" : "moved");
        /* moved just past the pointer, or to the top of the zone? */
        hout("unlocked.to", "%s", *h - (p + GetPtrSize(p)) < 64 ? "next to the pointer"
             : zmem + ZSIZE - (*h + GetHandleSize(h)) < 64 ? "the top of the zone" : "between");
        hout("~unlocked.at", "was %ld now %ld ptr %ld", (long)(was - zmem), (long)(*h - zmem),
             (long)(p - zmem));
        /* locked: it stays, the pointer goes above it */
        zbegin();
        h = NewHandle(8192);
        HLock(h);
        was = *h;
        p = NewPtr(1024);
        hout("locked.ptr", "%s the handle", where(p, *h));
        hout("locked.handle", "%s", *h == was ? "stayed" : "moved");
        /* purgeable, room to move it: moved or purged? */
        zbegin();
        h = NewHandle(8192);
        HPurge(h);
        was = *h;
        p = NewPtr(1024);
        hout("purgeable.ptr", "%s the handle", where(p, *h));
        hout("purgeable.handle", "%s", !*h ? "purged" : *h == was ? "stayed" : "moved");
    }
    {
        /* An unlocked handle too big to move above the pointer: the free
           space above it is smaller than it. */
        Handle h;
        Ptr was, p;
        long f;
        zbegin();
        f = zone->zcbFree;
        h = NewHandle(f * 6 / 10);
        was = *h;
        p = NewPtr(f * 3 / 10);
        hout("nomove.ptr", "%s", p ? where(p, *h) : "failed");
        hout("nomove.handle", "%s", *h == was ? "stayed" : "moved");
        hout("~nomove.at", "was %ld now %ld ptr %ld", (long)(was - zmem), (long)(*h - zmem),
             (long)(p - zmem));
        hout("nomove.err", "%d", MemError());
        /* the same, purgeable */
        zbegin();
        h = NewHandle(f * 6 / 10);
        HPurge(h);
        was = *h;
        p = NewPtr(f * 3 / 10);
        hout("nomove.purgeable.ptr", "%s", p ? (*h ? where(p, *h) : "handle purged") : "failed");
        hout("nomove.purgeable.handle", "%s", !*h ? "purged" : *h == was ? "stayed" : "moved");
    }
    {
        /* Free space split by an unlocked handle; a pointer bigger than
           either piece but smaller than both. */
        Handle low, h;
        Ptr was, p;
        long f;
        zbegin();
        f = zone->zcbFree;
        low = NewHandle(f * 4 / 10);
        h = NewHandle(f / 10);
        DisposeHandle(low);
        was = *h;
        p = NewPtr(f * 7 / 10);
        hout("split.ptr", "%s", p ? where(p, *h) : "failed");
        hout("split.handle", "%s", *h == was ? "stayed" : "moved");
        hout("split.err", "%d", MemError());
        /* the same with the handle locked: no room anywhere */
        zbegin();
        low = NewHandle(f * 4 / 10);
        h = NewHandle(f / 10);
        HLock(h);
        DisposeHandle(low);
        p = NewPtr(f * 7 / 10);
        hout("split.locked.ptr", "%s", p ? where(p, *h) : "failed");
        hout("split.locked.err", "%d", MemError());
    }
    {
        /* Purgeable blocks on both sides of a locked one, room for the
           pointer only once one of them is purged: which goes? */
        Handle lo, hi, l;
        Ptr p;
        long f;
        zbegin();
        f = zone->zcbFree;
        lo = NewHandle(f * 45 / 100);
        HPurge(lo);
        l = NewHandle(4096);
        HLock(l);
        hi = NewHandle(f * 45 / 100);
        HPurge(hi);
        p = NewPtr(f * 4 / 10);
        hout("inway.ptr", "%s the locked block", p ? where(p, *l) : "failed");
        hout("inway.low", "%s", *lo ? "kept" : "purged");
        hout("inway.high", "%s", *hi ? "kept" : "purged");
        /* A purgeable block under a locked one, room above it: purged to
           put the pointer low, or the pointer above? */
        zbegin();
        lo = NewHandle(f / 2);
        HPurge(lo);
        l = NewHandle(4096);
        HLock(l);
        p = NewPtr(f * 4 / 10);
        hout("purgeOrHigher.ptr", "%s the locked block", p ? where(p, *l) : "failed");
        hout("purgeOrHigher.low", "%s", *lo ? "kept" : "purged");
        /* the same with the lower block unlocked, not purgeable: room
           neither below (it can't move past the locked block) nor... above */
        zbegin();
        lo = NewHandle(f / 2);
        l = NewHandle(4096);
        HLock(l);
        p = NewPtr(f * 4 / 10);
        hout("stuck.ptr", "%s the locked block", p ? where(p, *l) : "failed");
    }
    {
        /* The grow-zone procedure: called only once compacting and purging
           haven't made room? A locked and a purgeable handle; a pointer
           that fits once the purgeable one is gone, then one that never
           fits. */
        Handle hp, hl;
        Ptr p;
        long f;
        zbegin();
        SetGrowZone(gzupp);
        f = zone->zcbFree;
        hl = NewHandle(f * 3 / 10);
        HLock(hl);
        hp = NewHandle(f * 3 / 10);
        HPurge(hp);
        gz_calls = 0;
        gz_watch = hp;
        gz_purged_first = -1;
        p = NewPtr(f * 6 / 10);
        hout("gz.fits.ptr", "%s", p ? "ok" : "failed");
        hout("gz.fits.calls", "%ld", gz_calls);
        hout("gz.fits.purged", "%s", !*hp ? "yes" : "no");
        if(p)
            DisposePtr(p);
        gz_calls = 0;
        gz_purged_first = -1;
        hp = NewHandle(1024);
        HPurge(hp);
        gz_watch = hp;
        p = NewPtr(ZSIZE);
        hout("gz.never.ptr", "%s", p ? "ok" : "failed");
        hout("gz.never.err", "%d", MemError());
        hout("gz.never.called", "%s", gz_calls ? "yes" : "no");
        hout("~gz.never.calls", "%ld", gz_calls);
        hout("gz.never.purgedBeforeGz", "%s", gz_purged_first < 0 ? "-" : gz_purged_first ? "yes" : "no");
        hout("gz.never.need", "%s", gz_need >= ZSIZE ? "at least the request" : "less than the request");
        hout("~gz.never.need", "%ld", gz_need);
        gz_watch = NULL;
    }
    {
        /* Slithereens' sounds: HLockHi, NewPtr, HUnlock, HPurge, five
           times; where the pointers end up. */
        Handle h[5];
        Ptr p[5];
        int adjacent = 1, below = 1, purged = 0, j;
        zbegin();
        for(i = 0; i < 5; i++)
        {
            h[i] = NewHandle(12000 + 1000 * i);
            HNoPurge(h[i]);
            HLockHi(h[i]);
            p[i] = NewPtr(13000 + 1000 * i);
            HUnlock(h[i]);
            HPurge(h[i]);
        }
        for(i = 0; i < 5; i++)
        {
            if(i && (!p[i] || !p[i - 1] || p[i] < p[i - 1]
                     || p[i] - p[i - 1] > GetPtrSize(p[i - 1]) + 32))
                adjacent = 0;
            for(j = 0; j < 5; j++)
                if(*h[j] && p[i] > *h[j])
                    below = 0;
            if(!*h[i])
                purged++;
        }
        hout("sounds.ptrs", "%s", p[4] ? "ok" : "failed");
        hout("sounds.adjacent", "%s", adjacent ? "yes" : "no");
        hout("sounds.belowHandles", "%s", below ? "yes" : "no");
        hout("sounds.purged", "%d", purged);
    }
    SetZone(appzone);
    for(i = 0; i < hn; i++)
        out(hkey[i], "%s", hres[i]);
    DisposePtr(zmem);
}

int main(void)
{
    int i;
    char k[64];
    probe_open("sys");
    if(SECTION("preload"))
    {
        /* The probe's own resources marked preload (sys.r): in memory
           already when it starts? A fat application's 68k segments are
           often marked so (Exile III). */
        static const struct { ResType type; short id; const char *key; } rs[] = {
            { 'CODE', 2, "CODE" }, { 'PRLD', 128, "PRLD" } };
        for(i = 0; i < 2; i++)
        {
            Handle h;
            SetResLoad(false);
            h = Get1Resource(rs[i].type, rs[i].id);
            SetResLoad(true);
            out(rs[i].key, "%s", !h ? "missing" : *h ? "loaded" : "not loaded");
        }
    }
    if(SECTION("container"))
    {
        /* Where CFM put the probe's code: the heap block holding main's
           code, against the data fork (the whole container) — does it
           keep the container, or the code section alone? */
        ProcessSerialNumber psn = { 0, kCurrentProcess };
        ProcessInfoRec info;
        FSSpec spec;
        short ref;
        long eof = 0;
        Ptr code = *(Ptr *)main, b;
        THz z = ApplicationZone();
        memset(&info, 0, sizeof info);
        info.processInfoLength = sizeof info;
        info.processAppSpec = &spec;
        if(GetProcessInformation(&psn, &info) == noErr && FSpOpenDF(&spec, fsRdPerm, &ref) == noErr)
        {
            GetEOF(ref, &eof);
            FSClose(ref);
        }
        out("~dataFork", "%ld", eof);
        for(b = (Ptr)&z->heapData; b < z->bkLim;)
        {
            long size = *(long *)(b + 4);
            if(size < 12)
                break;
            if(code >= b + 12 && code < b + size)
                break;
            b += size;
        }
        if(b < z->bkLim && *(long *)(b + 4) >= 12)
        {
            long size = *(long *)(b + 4) - 12;
            out("~codeBlock", "%ld", size);
            out("codeBlock", "%s", size >= eof ? "the container" : "smaller than the container");
        }
        else
            out("codeBlock", "outside the application heap");
    }
    if(SECTION("partition"))
    {
        /* The partition beyond the heap and the stack: the A5 world. The
           probe's CODE 0 asks for 256K above A5 (sys.r); does a PowerPC
           launch reserve it, as a fat application's 68k launch would? */
        ProcessSerialNumber psn = { 0, kCurrentProcess };
        ProcessInfoRec info;
        long heap = (long)LMGetApplLimit() - (long)ApplicationZone();
        long stack = (long)LMGetCurStackBase() - (long)LMGetApplLimit();
        memset(&info, 0, sizeof info);
        info.processInfoLength = sizeof info;
        GetProcessInformation(&psn, &info);
        long rest = (long)info.processSize - heap - stack;
        out("~heap", "%ld stack %ld rest %ld", heap, stack, rest);
        out("a5world", "%s", rest >= 256 * 1024 ? "CODE 0's" : "not reserved");
    }
    if(SECTION("video"))
    {
        /* The main screen's driver: cscGetGamma and cscSetGamma, csParam
           holding a pointer to a VDGammaRecord (Slithereens' fade). */
        static struct { Ptr csGTable; } rec;
        CntrlParam pb;
        OSErr err;
        GDHandle gd = GetMainDevice();
        memset(&pb, 0, sizeof pb);
        pb.ioCRefNum = (**gd).gdRefNum;
        pb.csCode = 8;                          /* cscGetGamma */
        rec.csGTable = (Ptr)0xA5A5A5A5;
        *(Ptr *)pb.csParam = (Ptr)&rec;
        err = PBStatusSync((ParmBlkPtr)&pb);
        out("getGamma.err", "%d", err);
        out("getGamma.csParam", "%s", *(Ptr *)pb.csParam == (Ptr)&rec ? "unchanged" : "overwritten");
        if(rec.csGTable == (Ptr)0xA5A5A5A5)
            out("getGamma.csGTable", "(unset)");
        else if(!rec.csGTable)
            out("getGamma.csGTable", "(nil)");
        else
        {
            short *g = (short *)rec.csGTable;
            out("getGamma.csGTable", "version %d type %d formula %d chans %d count %d width %d",
                g[0], g[1], g[2], g[3], g[4], g[5]);
            memset(&pb, 0, sizeof pb);
            pb.ioCRefNum = (**gd).gdRefNum;
            pb.csCode = 4;                      /* cscSetGamma, the same table */
            *(Ptr *)pb.csParam = (Ptr)&rec;
            err = PBControlSync((ParmBlkPtr)&pb);
            out("setGamma.err", "%d", err);
        }
    }
    if(SECTION("gestalt"))
        for(i = 0; i < (int)(sizeof selectors / sizeof selectors[0]); i++)
        {
            long v;
            OSErr err;
            SET(v);
            err = Gestalt(selectors[i], &v);
            if(selectors[i] == 'ram ' || selectors[i] == 'lram')
                sprintf(k, "~%.4s", (char *)&selectors[i]);
            else
                sprintf(k, "%.4s", (char *)&selectors[i]);
            out(k, "err %d %08lx", err, v);
        }
    if(SECTION("folder"))
        for(i = 0; i < (int)(sizeof folders / sizeof folders[0]); i++)
        {
            short vref;
            long dir;
            OSErr err;
            SET(vref); SET(dir);
            err = FindFolder(kOnSystemDisk, folders[i], kDontCreateFolder, &vref, &dir);
            sprintf(k, "%.4s", (char *)&folders[i]);
            out(k, "err %d", err);
            if(!err)
            {
                CInfoPBRec pb;
                Str255 name;
                memset(&pb, 0, sizeof pb);
                name[0] = 0;
                pb.dirInfo.ioNamePtr = name;
                pb.dirInfo.ioVRefNum = vref;
                pb.dirInfo.ioDrDirID = dir;
                pb.dirInfo.ioFDirIndex = -1;
                err = PBGetCatInfoSync(&pb);
                sprintf(k, "%.4s.name", (char *)&folders[i]);
                if(!err)
                    out_str(k, name);
                else
                    out(k, "err %d", err);
            }
        }
    if(SECTION("lowmem"))
        for(i = 0; i < (int)(sizeof lowmem / sizeof lowmem[0]); i++)
        {
            sprintf(k, "%04lx", lowmem[i][0]);
            out_hex(k, (void *)lowmem[i][0], lowmem[i][1]);
        }
    if(SECTION("process"))
    {
        ProcessSerialNumber psn = { 0, kCurrentProcess };
        ProcessInfoRec info;
        Str31 name;
        FSSpec spec;
        OSErr err;
        SET(info); SET(name); SET(spec);
        info.processInfoLength = sizeof info;
        info.processName = name;
        info.processAppSpec = &spec;
        err = GetProcessInformation(&psn, &info);
        out("err", "%d", err);
        out_str("name", name);
        out("type", "%.4s %.4s mode %08lx", (char *)&info.processType, (char *)&info.processSignature,
            (long)info.processMode);
        out("~size", "%ld free %ld", (long)info.processSize, (long)info.processFreeMem);
        out_str("spec.name", spec.name);
    }
    if(SECTION("script"))
    {
        static const short verbs[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46 };
        static const short sverbs[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 78, 80, 82, 84, 86, 88, 90, 92 };
        for(i = 0; i < (int)(sizeof verbs / sizeof verbs[0]); i++)
        {
            sprintf(k, "sm.%d", verbs[i]);
            out(k, "%08lx", (long)GetScriptManagerVariable(verbs[i]));
        }
        for(i = 0; i < (int)(sizeof sverbs / sizeof sverbs[0]); i++)
        {
            sprintf(k, "roman.%d", sverbs[i]);
            out(k, "%08lx", (long)GetScriptVariable(smRoman, sverbs[i]));
        }
    }
    if(SECTION("gworld"))
    {
        /* What an offscreen pixmap's baseAddr holds with its pixels
           unlocked, locked and unlocked again (programs copy the pixmap
           into ports of their own: Mars Rising), for each flag set. */
        static const long flags[] = { 0, pixPurge, keepLocal, useTempMem };
        for(i = 0; i < (int)(sizeof flags / sizeof flags[0]); i++)
        {
            GWorldPtr gw = NULL;
            Rect r = { 0, 0, 64, 64 };
            QDErr err = NewGWorld(&gw, 8, &r, NULL, NULL, flags[i]);
            sprintf(k, "%lx.err", flags[i]);
            out(k, "%d", err);
            if(err != noErr || !gw)
                continue;
            PixMapHandle pm = GetGWorldPixMap(gw);
            static const char *const when[] = { "new", "locked", "unlocked" };
            int w;
            for(w = 0; w < 3; w++)
            {
                if(w == 1)
                    LockPixels(pm);
                if(w == 2)
                    UnlockPixels(pm);
                Ptr base = (**pm).baseAddr, pix = GetPixBaseAddr(pm);
                const char *what = base == pix ? "pixels"
                    : base && *(Ptr *)base == pix ? "handle to pixels" : "other";
                sprintf(k, "%lx.%s.baseAddr", flags[i], when[w]);
                out(k, "%s", what);
                sprintf(k, "%lx.%s.pmVersion", flags[i], when[w]);
                out(k, "%d", (**pm).pmVersion);
                sprintf(k, "%lx.%s.state", flags[i], when[w]);
                out(k, "%08lx", (long)GetPixelsState(pm));
            }
            DisposeGWorld(gw);
        }

        /* A window made while a GWorld's device is current (SetGWorld(gw,
           nil), then NewCWindow: Mars Rising), the GWorld's pixels
           unlocked and locked: the device then, where the pixmap points. */
        for(i = 0; i < 2; i++)
        {
            GWorldPtr gw = NULL;
            Rect r = { 0, 0, 64, 64 }, wr = { 100, 100, 164, 164 };
            CGrafPtr savePort;
            GDHandle saveDev;
            const char *when = i ? "locked" : "unlocked";
            if(NewGWorld(&gw, 8, &r, NULL, NULL, 0) != noErr || !gw)
                continue;
            PixMapHandle gpm = GetGWorldPixMap(gw);
            if(i == 1)
                LockPixels(gpm);
            GetGWorld(&savePort, &saveDev);
            SetGWorld(gw, NULL);
            WindowPtr w = NewCWindow(NULL, &wr, "\p", false, documentProc, (WindowPtr)-1, false, 0);
            sprintf(k, "window.%s.device", when);
            out(k, "%s", GetGDevice() == GetMainDevice() ? "main"
                : GetGDevice() == GetGWorldDevice(gw) ? "gworld's" : "other");
            SetGWorld(savePort, saveDev);
            if(w)
            {
                PixMapHandle wpm = ((CGrafPtr)w)->portPixMap;
                Ptr base = (**wpm).baseAddr, pix = GetPixBaseAddr(gpm);
                sprintf(k, "window.%s.baseAddr", when);
                out(k, "%s", base == (**(**GetMainDevice()).gdPMap).baseAddr ? "screen"
                    : base == pix ? "gworld's pixels"
                    : base && *(Ptr *)base == pix ? "handle to gworld's pixels" : "other");
                sprintf(k, "window.%s.pixelSize", when);
                out(k, "%d", (**wpm).pixelSize);
                sprintf(k, "window.%s.pmVersion", when);
                out(k, "%d", (**wpm).pmVersion);
                sprintf(k, "window.%s.bounds", when);
                out_rect(k, &(**wpm).bounds);
                DisposeWindow(w);
            }
            DisposeGWorld(gw);
        }
    }
    /* Where an offscreen's pixels go (the 8.6 Finder asks for its windows'
       offscreens in temporary memory: in its 1 MB partition two of them
       filled it), and what a GWorld too big for any heap answers. */
    if(SECTION("gworldmem"))
    {
        static const long flags[] = { 0, useTempMem, pixPurge | noNewDevice | useTempMem };
        for(i = 0; i < (int)(sizeof flags / sizeof flags[0]); i++)
        {
            GWorldPtr gw = NULL;
            Rect r = { 0, 0, 218, 404 };
            QDErr err = NewGWorld(&gw, 32, &r, NULL,
                                  (flags[i] & noNewDevice) ? GetMainDevice() : NULL, flags[i]);
            sprintf(k, "%lx.err", flags[i]);
            out(k, "%d", err);
            if(err != noErr || !gw)
                continue;
            THz z = HandleZone((Handle)(**GetGWorldPixMap(gw)).baseAddr);
            sprintf(k, "%lx.zone", flags[i]);
            out(k, "%s", z == ApplicationZone() ? "application"
                : z == SystemZone() ? "system" : "other");
            DisposeGWorld(gw);
        }
        for(i = 0; i < 2; i++)
        {
            GWorldPtr gw = (GWorldPtr)-1;
            Rect r = { 0, 0, 8000, 8000 };
            QDErr err = NewGWorld(&gw, 32, &r, NULL, NULL, i ? useTempMem : 0);
            sprintf(k, "toobig.%s.err", i ? "temp" : "appl");
            out(k, "%d", err);
            sprintf(k, "toobig.%s.gworld", i ? "temp" : "appl");
            out(k, "%s", gw == NULL ? "nil" : gw == (GWorldPtr)-1 ? "untouched" : "set");
            if(err == noErr && gw && gw != (GWorldPtr)-1)
                DisposeGWorld(gw);
        }
    }
    /* the screen the system starts on: depth and size (8.6 on the G3 at
       640x480; Executor starts 8.x Systems as it does) */
    if(SECTION("screen"))
    {
        GDHandle gd = GetMainDevice();
        PixMapHandle pm = (**gd).gdPMap;
        Rect r = (**gd).gdRect;
        out("main.depth", "%d", (**pm).pixelSize);
        out("main.~rect", "%d,%d,%d,%d", r.top, r.left, r.bottom, r.right);
        out("main.type", "%d", (**pm).pixelType);
    }
    if(SECTION("cfm"))
    {
        /* kFindLib: 0 for a library connected to this process (its
           imports), cfragLibConnErr for one that is there but not
           connected, cfragNoLibraryErr for none; connected once prepared */
        static const char *const libs[] = {
            "InterfaceLib", "AppearanceLib", "WindowsLib", "MenusLib", "ControlsLib",
            "FontManager", "DialogsLib", "NoSuchLibrary" };
        for(i = 0; i < (int)(sizeof libs / sizeof libs[0]); i++)
        {
            Str255 name, err;
            ConnectionID conn;
            Ptr main;
            OSErr e;
            name[0] = strlen(libs[i]);
            memcpy(name + 1, libs[i], name[0]);
            sprintf(k, "find.%s", libs[i]);
            out(k, "%d", GetSharedLibrary(name, kPowerPCArch, kFindLib, &conn, &main, err));
            if(strcmp(libs[i], "AppearanceLib"))
                continue;
            e = GetSharedLibrary(name, kPowerPCArch, kReferenceCFrag, &conn, &main, err);
            out("load.AppearanceLib", "%d", e);
            sprintf(k, "find.after.%s", libs[i]);
            out(k, "%d", GetSharedLibrary(name, kPowerPCArch, kFindLib, &conn, &main, err));
        }
    }
    if(SECTION("heap"))
        probe_heap();
    if(SECTION("boot"))
    {
        /* BootDrive ($210): a volume reference number or a working
           directory's? The 8.6 Finder looks the startup disk's VCB up by
           vcbVRefNum. */
        short bd = *(short *)0x210, vref = 0;
        long dir = 0;
        QHdrPtr q = GetVCBQHdr();
        VCB *v;
        WDPBRec wd;
        int found = 0;
        for(v = (VCB *)q->qHead; v; v = (VCB *)v->qLink)
            if(v->vcbVRefNum == bd)
                found = 1;
        out("bootdrive.vcb", "%s", found ? "found by vcbVRefNum" : "no VCB with that vcbVRefNum");
        FindFolder(kOnSystemDisk, kSystemFolderType, false, &vref, &dir);
        out("bootdrive.is.system.vref", "%s", bd == vref ? "yes" : "no");
        memset(&wd, 0, sizeof wd);
        wd.ioVRefNum = bd;
        out("bootdrive.wdinfo", "%d", PBGetWDInfoSync(&wd));
        out("bootdrive.wdinfo.dir", "%s", wd.ioWDDirID == dir ? "System Folder"
                                          : wd.ioWDDirID == 2 ? "root" : "other");
        out("bootdrive.wdinfo.vref", "%s", wd.ioWDVRefNum == vref ? "the system volume" : "other");
        {
            /* GetWDInfo gives the working directory's volume: the 8.6
               Finder makes its startup disk from GetWDInfo(BootDrive) */
            short gv = 0;
            long gd = 0, gp = 0;
            out("getwdinfo", "%d", GetWDInfo(bd, &gv, &gd, &gp));
            out("getwdinfo.vref", "%s", gv == vref ? "the system volume" : gv == bd ? "the WD number" : "other");
            out("getwdinfo.dir", "%s", gd == dir ? "System Folder" : "other");
        }
    }
    probe_close();
    return 0;
}
