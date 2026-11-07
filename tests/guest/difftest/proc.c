/* proc: the processes running once the Finder is up (tools/difftest/run.py
   proc), as GetNextProcess lists them: on the G3's 8.6 the Extensions
   folder's background-only applications ('appe': Control Strip Extension,
   Time Synchronizer, Folder Actions, ...) that the Process Manager starts
   after the INITs. For each: its file's type and creator, SIZE flags,
   partition, the folder it is in, its launcher; the list's order and the
   launch order (processLaunchDate). */
#include "probe.h"
#include <Processes.h>
#include <Files.h>
#include <Gestalt.h>

#define MAX_PROCS 32

typedef struct
{
    ProcessSerialNumber psn;
    Str31 name;
    unsigned long launched;
} Proc;

static void cname(char *out, const unsigned char *p)
{
    memcpy(out, p + 1, p[0]);
    out[p[0]] = 0;
}

static void type4(char *out, OSType t)
{
    memcpy(out, &t, 4);
    out[4] = 0;
}

static const char *name_of(Proc *procs, int n, const ProcessSerialNumber *psn)
{
    static char buf[32];
    int i;
    Boolean same;
    if(psn->highLongOfPSN == 0 && psn->lowLongOfPSN == kNoProcess)
        return "none";
    if(psn->highLongOfPSN == 0 && psn->lowLongOfPSN == kSystemProcess)
        return "system";
    for(i = 0; i < n; i++)
        if(SameProcess(&procs[i].psn, psn, &same) == noErr && same)
        {
            cname(buf, procs[i].name);
            return buf;
        }
    return "other";
}

static void list(void)
{
    Proc procs[MAX_PROCS];
    int n = 0, i, j;
    ProcessSerialNumber psn = { 0, kNoProcess };
    char line[1024], nm[32], k[64], t[5], c[5];

    while(n < MAX_PROCS && GetNextProcess(&psn) == noErr)
    {
        ProcessInfoRec info;
        memset(&info, 0, sizeof info);
        info.processInfoLength = sizeof info;
        info.processName = procs[n].name;
        info.processAppSpec = NULL;
        procs[n].name[0] = 0;
        procs[n].psn = psn;
        procs[n].launched = GetProcessInformation(&psn, &info) == noErr ? info.processLaunchDate : 0;
        n++;
    }
    out("count", "%d", n);

    line[0] = 0;
    for(i = 0; i < n; i++)
    {
        cname(nm, procs[i].name);
        strcat(line, i ? ", " : "");
        strcat(line, nm);
    }
    out("order", "%s", line);

    /* by launch date, the earliest first (stable for equal dates) */
    {
        int idx[MAX_PROCS];
        for(i = 0; i < n; i++)
            idx[i] = i;
        for(i = 1; i < n; i++)
            for(j = i; j > 0 && procs[idx[j - 1]].launched > procs[idx[j]].launched; j--)
            {
                int x = idx[j];
                idx[j] = idx[j - 1];
                idx[j - 1] = x;
            }
        line[0] = 0;
        for(i = 0; i < n; i++)
        {
            cname(nm, procs[idx[i]].name);
            strcat(line, i ? ", " : "");
            strcat(line, nm);
        }
        out("launch_order", "%s", line);
    }

    for(i = 0; i < n; i++)
    {
        ProcessInfoRec info;
        FSSpec spec;
        Str31 name;
        OSErr err;
        CInfoPBRec pb;
        Str63 folder;
        memset(&info, 0, sizeof info);
        info.processInfoLength = sizeof info;
        info.processName = name;
        info.processAppSpec = &spec;
        err = GetProcessInformation(&procs[i].psn, &info);
        cname(nm, procs[i].name);
        if(err)
        {
            sprintf(k, "%s.err", nm);
            out(k, "%d", err);
            continue;
        }
        type4(t, info.processType);
        type4(c, info.processSignature);
        sprintf(k, "%s.type", nm);
        out(k, "%s %s", t, c);
        sprintf(k, "%s.mode", nm);
        out(k, "%08lx", (unsigned long)info.processMode);
        sprintf(k, "%s.size", nm);
        out(k, "%lu", (unsigned long)info.processSize);
        sprintf(k, "%s.launcher", nm);
        out(k, "%s", name_of(procs, n, &info.processLauncher));
        memset(&pb, 0, sizeof pb);
        folder[0] = 0;
        pb.dirInfo.ioNamePtr = folder;
        pb.dirInfo.ioVRefNum = spec.vRefNum;
        pb.dirInfo.ioDrDirID = spec.parID;
        pb.dirInfo.ioFDirIndex = -1;
        PBGetCatInfoSync(&pb);
        cname(line, folder);
        sprintf(k, "%s.folder", nm);
        out(k, "%s", line);
    }
}

/* The Control Strip's private calls (PrivateInterfaceLib): ActiveWindow
   (LayerDispatch -2) against FrontWindow, NewFloatLayer (OSDispatch $48),
   DisableSwitching/EnableSwitching (OSDispatch $19/$1A). */
static const char *which(WindowPtr w, WindowPtr mine, WindowPtr front)
{
    if(!w)
        return "nil";
    if(w == mine)
        return "mine";
    if(w == front)
        return "front";
    return "other";
}

static void layers(void)
{
    typedef WindowPtr (*Active_t)(void);
    typedef OSErr (*NewFloat_t)(WindowPtr *);
    typedef void (*Switch_t)(void);
    typedef Boolean (*IsLayer_t)(WindowPtr);
    typedef WindowPtr (*GetLayer_t)(WindowPtr);
    Active_t active = (Active_t)probe_bind("PrivateInterfaceLib", "ActiveWindow");
    NewFloat_t new_float = (NewFloat_t)probe_bind("PrivateInterfaceLib", "NewFloatLayer");
    Switch_t disable = (Switch_t)probe_bind("PrivateInterfaceLib", "DisableSwitching");
    Switch_t enable = (Switch_t)probe_bind("PrivateInterfaceLib", "EnableSwitching");
    IsLayer_t is_layer = (IsLayer_t)probe_bind("PrivateInterfaceLib", "IsLayer");
    GetLayer_t window_layer = (GetLayer_t)probe_bind("PrivateInterfaceLib", "GetWindowLayer");
    Rect r = { 60, 60, 160, 260 };
    WindowPtr w, hidden;
    probe_bind("PrivateInterfaceLib", "FragRegisterMemLib");
    if(active)
        out("ActiveWindow.none", "%s", which(active(), NULL, FrontWindow()));
    w = NewCWindow(NULL, &r, "\pproc", true, documentProc, (WindowPtr)-1, false, 0);
    if(active)
        out("ActiveWindow.shown", "%s", which(active(), w, FrontWindow()));
    hidden = NewCWindow(NULL, &r, "\phidden", false, documentProc, (WindowPtr)-1, false, 0);
    if(active)
        out("ActiveWindow.hidden_new", "%s", which(active(), w, FrontWindow()));
    DisposeWindow(hidden);
    if(active)
    {
        EventRecord e;
        int i;
        for(i = 0; i < 20; i++)
            WaitNextEvent(everyEvent & ~(mDownMask | keyDownMask), &e, 1, NULL);
        out("ActiveWindow.after_events", "%s", which(active(), w, FrontWindow()));
    }
    if(disable && enable)
    {
        disable();
        enable();
        out("DisableSwitching", "returned");
    }
    if(new_float)
    {
        WindowPtr layer = (WindowPtr)0xA5A5A5A5;
        OSErr err = new_float(&layer);
        out("NewFloatLayer", "err %d %s", err, layer == (WindowPtr)0xA5A5A5A5 ? "unset" : layer ? "layer" : "nil");
        if(!err && layer && is_layer)
            out("NewFloatLayer.IsLayer", "%d", is_layer(layer));
        if(!err && layer && window_layer)
            out("NewFloatLayer.parent", "%s", window_layer(layer) == window_layer(w) ? "process's parent" : window_layer(layer) == w ? "window" : "other");
        if(!err && layer)
            DisposeWindow(layer);
    }
    DisposeWindow(w);
}

int main(void)
{
    probe_open("proc");
    if(SECTION("list"))
        list();
    if(SECTION("layers"))
        layers();
    if(SECTION("gestalt"))
    {
        static const OSType sels[] = { 'sdev', 'csvr' };
        int i;
        for(i = 0; i < 2; i++)
        {
            char k[24];
            long r = 0;
            OSErr err = Gestalt(sels[i], &r);
            sprintf(k, "Gestalt.%.4s", (const char *)&sels[i]);
            out(k, "err %d %08lx", err, err ? 0UL : (unsigned long)r);
        }
    }
    probe_close();
    return 0;
}
