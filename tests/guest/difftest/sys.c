/* sys: what a program asks the system first, differential
   (tools/difftest/run.py sys): Gestalt selectors, FindFolder for each
   folder type (and the folder's name), value-holding low-memory globals,
   the process's own information, Script Manager variables. Machine-specific
   answers (RAM, free memory) are keys with "~": shown, not compared. */
#include "probe.h"
#include <Gestalt.h>
#include <Folders.h>
#include <Files.h>
#include <Processes.h>
#include <Script.h>

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
    probe_close();
    return 0;
}
