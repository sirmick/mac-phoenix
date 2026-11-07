/* dmprobe: what a Mac's Display Manager does when the screen's resolution
 * changes (docs/executor/DisplayModes.md). The probe, Display Manager aware
 * (SIZE), registers a notify proc, an extended notify proc and handlers for
 * the display notice Apple Events, lists the main display's modes, then
 * switches to another resolution and back with DMSetDisplayMode, logging
 * every callback and event it gets (descriptors dumped whole), the main
 * GDevice, screenBits, GrayRgn and its own window, to Host:dmprobe.txt.
 *
 * Its window is placed near the bottom right: whether the Display Manager
 * moves the windows of an application that says it handles the notice.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <Events.h>
#include <Processes.h>
#include <AppleEvents.h>
#include <Displays.h>
#include <Video.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <stdio.h>
#include <string.h>

static FILE *out;
static WindowPtr win;
static DMNotificationUPP notifyUPP;
static DMExtendedNotificationUPP extUPP;

static void line(const char *s) { fputs(s, out); fputc('\n', out); fflush(out); }

static void type4(char *buf, OSType t)
{
    buf[0] = t >> 24; buf[1] = t >> 16; buf[2] = t >> 8; buf[3] = t; buf[4] = 0;
}

static void dump_desc(const AEDesc *d, int depth, OSType key);

static void dump_items(const AEDesc *d, int depth)
{
    long n = 0, i;
    if(AECountItems(d, &n) != noErr)
        return;
    for(i = 1; i <= n; i++)
    {
        AEKeyword k = 0;
        AEDesc item;
        if(AEGetNthDesc(d, i, typeWildCard, &k, &item) == noErr)
        {
            dump_desc(&item, depth + 1, k);
            AEDisposeDesc(&item);
        }
    }
}

static void dump_desc(const AEDesc *d, int depth, OSType key)
{
    char t[5], k[12], buf[200];
    Size size = d->dataHandle ? GetHandleSize(d->dataHandle) : 0;
    int i, n;
    type4(t, d->descriptorType);
    type4(k, key);
    if(key && !(k[0] >= ' ' && k[1] >= ' ' && k[2] >= ' ' && k[3] >= ' '))
        sprintf(k, "#%lX", (unsigned long)key);
    n = sprintf(buf, "%*s%s %s %ld", depth * 2, "", key ? k : "-", t, (long)size);
    if(d->descriptorType == typeAERecord || d->descriptorType == typeAEList
       || d->descriptorType == typeAppleEvent)
    {
        line(buf);
        dump_items(d, depth);
        return;
    }
    if(d->dataHandle)
    {
        unsigned char *p = (unsigned char *)*d->dataHandle;
        n += sprintf(buf + n, " :");
        for(i = 0; i < size && i < 32; i++)
            n += sprintf(buf + n, " %02X", p[i]);
    }
    line(buf);
}

static void dump_event(const AppleEvent *e, const char *who)
{
    char buf[100];
    sprintf(buf, "%s ticks=%ld", who, (long)TickCount());
    line(buf);
    if(e)
    {
        /* the attributes too: class, id */
        OSType v; DescType tp; Size sz;
        char c[5], i[5];
        if(AEGetAttributePtr(e, keyEventClassAttr, typeType, &tp, &v, 4, &sz) == noErr)
            type4(c, v); else strcpy(c, "?");
        if(AEGetAttributePtr(e, keyEventIDAttr, typeType, &tp, &v, 4, &sz) == noErr)
            type4(i, v); else strcpy(i, "?");
        sprintf(buf, " event %s/%s", c, i);
        line(buf);
        dump_desc(e, 1, 0);
    }
}

static void state(const char *when)
{
    char buf[200];
    GDHandle gd = GetMainDevice();
    Rect r = (**gd).gdRect, b = (**(**gd).gdPMap).bounds;
    Rect g = (**GetGrayRgn()).rgnBBox;
    Rect w = win ? (**((WindowPeek)win)->strucRgn).rgnBBox : r;
    sprintf(buf, "state %s: gdRect %d,%d,%d,%d pmBounds %d,%d,%d,%d rowBytes %d depth %d "
            "screenBits %d,%d,%d,%d gray %d,%d,%d,%d mbar %d win %d,%d,%d,%d",
            when, r.top, r.left, r.bottom, r.right, b.top, b.left, b.bottom, b.right,
            (**(**gd).gdPMap).rowBytes & 0x3FFF, (**(**gd).gdPMap).pixelSize,
            qd.screenBits.bounds.top, qd.screenBits.bounds.left,
            qd.screenBits.bounds.bottom, qd.screenBits.bounds.right,
            g.top, g.left, g.bottom, g.right, LMGetMBarHeight(),
            w.top, w.left, w.bottom, w.right);
    line(buf);
}

static pascal void notify(AppleEvent *e)
{
    dump_event(e, "notify");
}

static pascal void ext_notify(void *user, short message, void *data)
{
    char buf[100];
    sprintf(buf, "extnotify message=%d data=%08lX ticks=%ld", message, (long)data, (long)TickCount());
    line(buf);
    if(data && (message == kDMNotifyEvent || message == kDMNotifyExtendEvent))
        dump_desc((AEDesc *)data, 1, 0);
}

static pascal OSErr ae_handler(const AppleEvent *e, AppleEvent *reply, long refcon)
{
    dump_event(e, "handler");
    state("in handler");
    return noErr;
}

/* The modes: each resolution's ID, size, and its depth modes. */
static unsigned long res_ids[32];
static short res_w[32], res_h[32];
static int nres;

static pascal void mode_iter(void *user, DMListIndexType i, DMDisplayModeListEntryPtr e)
{
    char buf[200];
    VDResolutionInfoPtr r = e->displayModeResolutionInfo;
    VDSwitchInfoPtr s = e->displayModeSwitchInfo;
    int n = sprintf(buf, "mode %lu: id %lu %ldx%ld flags %08lX switch mode %d data %lu maxdepth %d name %.*s depths",
            (unsigned long)i, (unsigned long)r->csDisplayModeID, (long)r->csHorizontalPixels,
            (long)r->csVerticalLines, (unsigned long)e->displayModeFlags,
            s ? s->csMode : -1, s ? (unsigned long)s->csData : 0, r->csMaxDepthMode,
            e->displayModeName ? e->displayModeName[0] : 0,
            e->displayModeName ? (char *)e->displayModeName + 1 : "");
    if(e->displayModeDepthBlockInfo)
    {
        DMDepthInfoBlockPtr db = e->displayModeDepthBlockInfo;
        unsigned long j;
        for(j = 0; j < db->depthBlockCount && n < 180; j++)
        {
            VPBlockPtr vp = (VPBlockPtr)db->depthVPBlock[j].depthVPBlock;
            VDSwitchInfoPtr ds = db->depthVPBlock[j].depthSwitchInfo;
            n += sprintf(buf + n, " %d:%d", ds ? ds->csMode : -1, vp ? vp->vpPixelSize : -1);
        }
    }
    line(buf);
    if(nres < 32)
    {
        res_ids[nres] = r->csDisplayModeID;
        res_w[nres] = r->csHorizontalPixels;
        res_h[nres] = r->csVerticalLines;
        nres++;
    }
}

static void pump(long ticks)
{
    long until = TickCount() + ticks;
    EventRecord ev;
    while(TickCount() < until)
        if(WaitNextEvent(everyEvent, &ev, 2, NULL))
        {
            if(ev.what == kHighLevelEvent)
                AEProcessAppleEvent(&ev);
            else if(ev.what == updateEvt)
            {
                BeginUpdate((WindowPtr)ev.message);
                EraseRect(&((WindowPtr)ev.message)->portRect);
                EndUpdate((WindowPtr)ev.message);
            }
        }
}

static Handle config_state;

static void switch_to(unsigned long id, const char *what)
{
    char buf[100];
    unsigned long depth = 0;
    OSErr err;
    GDHandle gd = GetMainDevice();
    VDSwitchInfoRec sw;
    if(DMGetDisplayMode(gd, &sw) == noErr)
        depth = sw.csMode;
    sprintf(buf, "switch %s to %lu depthMode %lu ticks=%ld", what, id, depth, (long)TickCount());
    line(buf);
    err = DMSetDisplayMode(gd, id, &depth, 0, config_state);
    sprintf(buf, "DMSetDisplayMode -> %d depthMode %lu ticks=%ld", err, depth, (long)TickCount());
    line(buf);
    state("after DMSetDisplayMode");
    pump(120);
    state("after events");
}

int main(void)
{
    ProcessSerialNumber psn;
    DisplayIDType id = 0;
    GDHandle gd;
    unsigned long count = 0, cur = 0, i, target = 0;
    DMListType list;
    VDSwitchInfoRec sw;
    Rect r;
    long dm = 0;
    char buf[200];

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    InitCursor();

    out = fopen("Host:dmprobe.txt", "w");
    if(!out)
        return 1;
    Gestalt('dplv', &dm);
    sprintf(buf, "dplv %08lX", dm);
    line(buf);

    gd = GetMainDevice();
    r = (**gd).gdRect;
    SetRect(&r, r.right - 300, r.bottom - 120, r.right - 20, r.bottom - 20);
    win = NewCWindow(NULL, &r, "\pdmprobe", true, documentProc, (WindowPtr)-1, false, 0);
    state("start");

    GetCurrentProcess(&psn);
    notifyUPP = NewDMNotificationUPP(notify);
    extUPP = NewDMExtendedNotificationUPP(ext_notify);
    sprintf(buf, "DMRegisterNotifyProc %d", DMRegisterNotifyProc(notifyUPP, &psn));
    line(buf);
    sprintf(buf, "DMRegisterExtendedNotifyProc %d",
            DMRegisterExtendedNotifyProc(extUPP, NULL, 0, &psn));
    line(buf);
    sprintf(buf, "AEInstall dspl %d cnfg %d dsum %d",
            AEInstallEventHandler(kCoreEventClass, kAEDisplayNotice, NewAEEventHandlerUPP(ae_handler), 0, false),
            AEInstallEventHandler(kCoreEventClass, kAESystemConfigNotice, NewAEEventHandlerUPP(ae_handler), 1, false),
            AEInstallEventHandler(kCoreEventClass, kAEDisplaySummary, NewAEEventHandlerUPP(ae_handler), 2, false));
    line(buf);
    pump(30);

    DMGetDisplayIDByGDevice(gd, &id, false);
    if(DMGetDisplayMode(gd, &sw) == noErr)
    {
        sprintf(buf, "current: mode %d data %lu", sw.csMode, (unsigned long)sw.csData);
        line(buf);
        cur = sw.csData;
    }
    if(DMNewDisplayModeList(id, 0, 0, &count, &list) == noErr)
    {
        sprintf(buf, "modes: %lu (display %lu)", count, (unsigned long)id);
        line(buf);
        for(i = 0; i < count; i++)
            DMGetIndexedDisplayModeFromList(list, i, 0, NewDMDisplayModeListIteratorUPP(mode_iter), NULL);
        DMDisposeList(list);
    }

    /* 640x480 (or the smallest other), and back */
    for(i = 0; i < (unsigned long)nres; i++)
        if(res_ids[i] != cur && res_w[i] == 640 && res_h[i] == 480)
            target = res_ids[i];
    for(i = 0; !target && i < (unsigned long)nres; i++)
        if(res_ids[i] != cur)
            target = res_ids[i];
    if(target)
    {
        switch_to(target, "small");
        switch_to(cur, "back");
        /* without notify procs: does the display notice come as an event? */
        sprintf(buf, "removed %d %d",
                DMRemoveNotifyProc(notifyUPP, &psn),
                DMRemoveExtendedNotifyProc(extUPP, NULL, &psn, 0));
        line(buf);
        switch_to(target, "small2");
        switch_to(cur, "back2");
        /* as the Monitors control panel does: in a configuration */
        {
            OSErr e1, e2;
            e1 = DMBeginConfigureDisplays(&config_state);
            sprintf(buf, "begin %d", e1);
            line(buf);
            switch_to(target, "small3");
            e2 = DMEndConfigureDisplays(config_state);
            config_state = NULL;
            sprintf(buf, "end %d", e2);
            line(buf);
            pump(120);
            state("after end");
            DMBeginConfigureDisplays(&config_state);
            switch_to(cur, "back3");
            DMEndConfigureDisplays(config_state);
            config_state = NULL;
            pump(120);
            state("after end");
        }
    }
    line("---");
    fclose(out);
    return 0;
}
