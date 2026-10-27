/* rtest.c — RetroTest: a Retro68 program built against the Multiversal
 * Interfaces generated from Executor's definitions (tests/guest/retro68,
 * build.sh), for 68k and PowerPC. It calls through the generated headers
 * and glue (traps, dispatchers, Component Manager and OSA inlines on 68k;
 * InterfaceLib imports on PowerPC) and writes "PASS name [got]" / "FAIL
 * name [got] (want [...])" lines to Host:retro68_results.txt, then "---"
 * (tests/test_guest_suite.sh --results). Run on Executor and on the ROM
 * machines, which are the reference. */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <Gestalt.h>
#include <Memory.h>
#include <Processes.h>
#include <Script.h>
#include <Components.h>
#include <OSA.h>
#include <AppleEvents.h>
#include <string.h>

QDGlobals qd;

static short out_ref;
static int failures;

static void put(const char *s)
{
    long n = strlen(s);
    FSWrite(out_ref, &n, s);
}

static void num(char *out, long v)
{
    char tmp[16];
    int n = 0, i = 0;
    unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
    do
        tmp[n++] = (char)('0' + u % 10);
    while(u /= 10);
    if(v < 0)
        out[i++] = '-';
    while(n)
        out[i++] = tmp[--n];
    out[i] = 0;
}

static void check(const char *name, const char *got, const char *want)
{
    int ok = strcmp(got, want) == 0;
    put(ok ? "PASS " : "FAIL ");
    put(name);
    put(" [");
    put(got);
    put("]");
    if(!ok)
    {
        put(" (want [");
        put(want);
        put("])");
        failures++;
    }
    put("\r");
}

static void checkl(const char *name, long got, long want)
{
    char g[16], w[16];
    num(g, got);
    num(w, want);
    check(name, g, w);
}

static void test_toolbox(void)
{
    long v = 0;
    Rect r;
    WindowPtr w;
    SetRect(&r, 40, 60, 240, 160);
    w = NewWindow(NULL, &r, "\pRetroTest", true, documentProc, (WindowPtr)-1, false, 0);
    checkl("window", w != NULL, 1);
    if(w)
    {
        SetPort(w);
        checkl("port", qd.thePort == w, 1);
        DisposeWindow(w);
    }
    checkl("gestalt_sysv", Gestalt('sysv', &v) == noErr && v >= 0x0700, 1);
    {
        Handle h = NewHandle(100);
        checkl("handle", h != NULL && GetHandleSize(h) == 100, 1);
        if(h)
            DisposeHandle(h);
    }
}

static void test_files(void)
{
    short vref;
    long dirid;
    FSSpec spec;
    checkl("findfolder_prefs", FindFolder(kOnSystemDisk, kPreferencesFolderType, kDontCreateFolder,
                                          &vref, &dirid), noErr);
    checkl("fsmakefsspec_host", FSMakeFSSpec(0, 0, "\pHost:retro68_results.txt", &spec), noErr);
}

/* The high-level HFS routines (glue over the parameter-block calls; on
 * PowerPC InterfaceLib's): a folder in the Preferences folder, a file in
 * it given Finder info, locked and unlocked, moved to a subfolder. */
static void test_hfs_glue(void)
{
    short vref;
    long prefs, dir = 0, sub = 0;
    FInfo fi;
    if(FindFolder(kOnSystemDisk, kPreferencesFolderType, kDontCreateFolder, &vref, &prefs) != noErr)
    {
        put("SKIP hfs_glue (no Preferences folder)\r");
        return;
    }
    checkl("dircreate", DirCreate(vref, prefs, "\pRetroTest folder", &dir), noErr);
    checkl("dircreate_sub", DirCreate(vref, dir, "\psub", &sub), noErr);
    checkl("hcreate", HCreate(vref, dir, "\pfile", 'ttxt', 'TEXT'), noErr);
    if(HGetFInfo(vref, dir, "\pfile", &fi) == noErr)
    {
        fi.fdType = 'RTst';
        checkl("hsetfinfo", HSetFInfo(vref, dir, "\pfile", &fi), noErr);
        fi.fdType = 0;
        checkl("hsetfinfo_type", HGetFInfo(vref, dir, "\pfile", &fi) == noErr && fi.fdType == 'RTst', 1);
    }
    checkl("hsetflock", HSetFLock(vref, dir, "\pfile"), noErr);
    checkl("hdelete_locked", HDelete(vref, dir, "\pfile"), fLckdErr);
    checkl("hrstflock", HRstFLock(vref, dir, "\pfile"), noErr);
    checkl("catmove", CatMove(vref, dir, "\pfile", sub, NULL), noErr);
    checkl("catmove_moved", HGetFInfo(vref, sub, "\pfile", &fi), noErr);
    HDelete(vref, sub, "\pfile");
    HDelete(vref, dir, "\psub");
    HDelete(vref, prefs, "\pRetroTest folder");
}

static void test_process(void)
{
    ProcessSerialNumber psn;
    checkl("getcurrentprocess", GetCurrentProcess(&psn), noErr);
}

static void test_components(void)
{
    ComponentDescription cd;
    Component c;
    ComponentInstance ci = NULL;
    long n;

    cd.componentType = kOSAComponentType;
    cd.componentSubType = 0;
    cd.componentManufacturer = 0;
    cd.componentFlags = 0;
    cd.componentFlagsMask = 0;
    n = CountComponents(&cd);
    c = FindNextComponent(NULL, &cd);
    checkl("findnext_matches_count", (n > 0) == (c != NULL), 1);
    if(!c)
    {
        put("SKIP osa (no scripting component)\r");
        return;
    }
    /* OpenComponent: 7.5.5's Component Manager predates OpenAComponent */
    ci = OpenComponent(c);
    checkl("opencomponent", ci != NULL, 1);
    if(ci)
    {
        AEDesc src, result;
        OSAID script = kOSANullScript, value = kOSANullScript;
        Handle h;
        char text[32];
        long len;

        AECreateDesc(typeChar, "6 * 7", 5, &src);
        checkl("osa_compile", OSACompile(ci, &src, kOSAModeNull, &script), noErr);
        checkl("osa_execute", OSAExecute(ci, script, kOSANullScript, kOSAModeNull, &value), noErr);
        checkl("osa_display", OSADisplay(ci, value, typeChar, kOSAModeNull, &result), noErr);
        h = result.dataHandle;
        len = h ? GetHandleSize(h) : 0;
        if(len > 31)
            len = 31;
        if(h)
            BlockMove(*h, text, len);
        text[len] = 0;
        check("osa_result", text, "42");
        AEDisposeDesc(&result);
        AEDisposeDesc(&src);
        OSADispose(ci, value);
        OSADispose(ci, script);
        checkl("closecomponent", CloseComponent(ci), noErr);
    }
}

int main(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    HDelete(0, 0, "\pHost:retro68_results.txt");
    if(HCreate(0, 0, "\pHost:retro68_results.txt", 'ttxt', 'TEXT') != noErr
       || HOpen(0, 0, "\pHost:retro68_results.txt", fsWrPerm, &out_ref) != noErr)
        return 1;
#if defined(__powerpc__) || defined(__POWERPC__)
    put("# PowerPC\r");
#else
    put("# 68k\r");
#endif
    test_toolbox();
    test_files();
    test_hfs_glue();
    test_process();
    test_components();
    put("---\r");
    FSClose(out_ref);
    return 0;
}
