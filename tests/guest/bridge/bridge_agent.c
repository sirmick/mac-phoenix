/*
 * BridgeAgent - MacPhoenix automation agent
 *
 * A small application installed in System Folder:Startup Items. Finder
 * launches it at desktop time; it runs a WaitNextEvent loop at ~10Hz
 * polling the ExtFS "Host" volume for bridge commands.
 *
 * Protocol (file-based):
 *   Host writes Host:_bridge_cmd      - "LAUNCH path" or "QUIT"
 *   Agent reads, deletes, executes
 *   Agent writes Host:_bridge_result  - decimal OSErr, CR-terminated
 *
 * Liveness markers:
 *   Host:bridge_loaded       - created once at first poll
 *   Host:_bridge_heartbeat   - rewritten every ~2s with a counter
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Processes.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Script.h>
#include <AppleEvents.h>
#include <Aliases.h>
#include <Devices.h>
#include <Resources.h>
#include <ToolUtils.h>
#include <string.h>
#include <stdio.h>

/* ---------- Status window state ---------- */

static WindowPtr gStatusWin = NULL;
static int       gHeartbeat = 0;
static int       gCmdCount  = 0;
static Str255    gLastCmd;
static OSErr     gLastResult = 0;
static Boolean   gRunning   = true;

static void draw_status(void)
{
    if (!gStatusWin) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gStatusWin);

    Rect r = gStatusWin->portRect;
    EraseRect(&r);

    TextFont(3); /* geneva */
    TextSize(9);

    char buf[64];
    Str255 pbuf;

    MoveTo(8, 18);
    DrawString("\pMacPhoenix BridgeAgent");

    MoveTo(8, 36);
    snprintf(buf, sizeof(buf), "Heartbeat: %d", gHeartbeat);
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    DrawString(pbuf);

    MoveTo(8, 52);
    snprintf(buf, sizeof(buf), "Commands: %d  last err: %d",
             gCmdCount, (int)gLastResult);
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    DrawString(pbuf);

    MoveTo(8, 68);
    DrawString("\pLast cmd: ");
    DrawString(gLastCmd);

    SetPort(saved);
}

static void open_status_window(void)
{
    Rect bounds;
    SetRect(&bounds, 40, 60, 360, 160);
    gStatusWin = NewWindow(NULL, &bounds, "\pBridgeAgent",
                           true, documentProc, (WindowPtr)-1, true, 0);
}

/* ---------- Bridge command handlers ---------- */

static void bridge_step(const char *tag);

static OSErr do_launch(const unsigned char *path)
{
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, path, &spec);
    if (err != noErr) return err;

    LaunchParamBlockRec pb;
    memset(&pb, 0, sizeof(pb));
    pb.launchBlockID      = extendedBlock;
    pb.launchEPBLength    = extendedBlockLen;
    pb.launchFileFlags    = 0;
    pb.launchControlFlags = launchContinue | launchNoFileFlags;
    pb.launchAppSpec      = &spec;
    return LaunchApplication(&pb);
}

static OSErr do_open_document(const unsigned char *path)
{
    bridge_step("OPEN:enter");
    /* Resolve the document file */
    FSSpec docSpec;
    OSErr err = FSMakeFSSpec(0, 0, path, &docSpec);
    if (err != noErr) { bridge_step("OPEN:doc-not-found"); return err; }

    /* Get the document's creator code */
    FInfo fndrInfo;
    err = FSpGetFInfo(&docSpec, &fndrInfo);
    if (err != noErr) return err;

    /* Find the application for this creator via Desktop Manager.
     * Walk all mounted volumes until we find a match. */
    FSSpec appSpec;
    Boolean found = false;
    short vIndex;
    for (vIndex = 1; !found; vIndex++) {
        HParamBlockRec vpb;
        Str255 vname;
        memset(&vpb, 0, sizeof(vpb));
        vpb.volumeParam.ioNamePtr = vname;
        vpb.volumeParam.ioVRefNum = 0;
        vpb.volumeParam.ioVolIndex = vIndex;
        err = PBHGetVInfoSync(&vpb);
        if (err != noErr) break;  /* no more volumes */

        DTPBRec dt;
        memset(&dt, 0, sizeof(dt));
        dt.ioVRefNum = vpb.volumeParam.ioVRefNum;
        err = PBDTGetPath(&dt);
        if (err != noErr) continue;

        Str255 appName;
        dt.ioNamePtr   = appName;
        dt.ioFileCreator = fndrInfo.fdCreator;
        dt.ioIndex     = 0;
        err = PBDTGetAPPLSync(&dt);
        if (err == noErr) {
            err = FSMakeFSSpec(vpb.volumeParam.ioVRefNum,
                               dt.ioAPPLParID, appName, &appSpec);
            if (err == noErr) found = true;
        }
    }

    if (!found) { bridge_step("OPEN:appl-not-found"); return fnfErr; }
    bridge_step("OPEN:appl-found");

    /* Build an 'odoc' high-level event as AppParameters for LaunchApplication.
     * This is equivalent to Finder double-clicking the document. */
    AEDesc fileDesc;
    AEDescList fileList;
    AppleEvent odocEvent;
    AEDesc targetSelf;
    ProcessSerialNumber selfPSN = {0, kCurrentProcess};

    err = AECreateDesc(typeProcessSerialNumber, &selfPSN,
                       sizeof(selfPSN), &targetSelf);
    if (err != noErr) return err;

    err = AECreateAppleEvent(kCoreEventClass, kAEOpenDocuments,
                             &targetSelf, kAutoGenerateReturnID,
                             kAnyTransactionID, &odocEvent);
    AEDisposeDesc(&targetSelf);
    if (err != noErr) return err;

    AliasHandle alias;
    err = NewAlias(NULL, &docSpec, &alias);
    if (err != noErr) { AEDisposeDesc(&odocEvent); return err; }

    err = AECreateList(NULL, 0, false, &fileList);
    if (err == noErr) {
        HLock((Handle)alias);
        AEPutPtr(&fileList, 0, typeAlias, *alias,
                 GetHandleSize((Handle)alias));
        HUnlock((Handle)alias);
        AEPutParamDesc(&odocEvent, keyDirectObject, &fileList);
        AEDisposeDesc(&fileList);
    }
    DisposeHandle((Handle)alias);

    AEDisposeDesc(&odocEvent);

    /* Launch the app, then send it an 'odoc' AE once it's running. */
    {
        LaunchParamBlockRec lpb;
        memset(&lpb, 0, sizeof(lpb));
        lpb.launchBlockID      = extendedBlock;
        lpb.launchEPBLength    = extendedBlockLen;
        lpb.launchFileFlags    = 0;
        lpb.launchControlFlags = launchContinue | launchNoFileFlags;
        lpb.launchAppSpec      = &appSpec;
        bridge_step("OPEN:pre-launch");
        err = LaunchApplication(&lpb);
        if (err != noErr) { bridge_step("OPEN:launch-failed"); return err; }
        bridge_step("OPEN:launched");

        /* Yield via Delay() instead of a WaitNextEvent loop. When MacPerl
         * becomes frontmost, BridgeAgent's nested WaitNextEvent loop does
         * not get scheduled reliably; Delay() is a Process Manager call
         * that always yields for the requested duration.
         *
         * ~3s is enough for MacPerl to process the auto-generated 'oapp'
         * AE and register its 'misc'/'dosc' handler. The dosc we send
         * afterward sits in the AE queue and MacPerl processes it on the
         * next trip through its event loop. */
        {
            unsigned long finalTicks;
            Delay(180, &finalTicks);
            bridge_step("OPEN:after-delay");

            /* Query Process Manager for the actual PSN of the launched app.
             * The PSN returned in launchProcessSN is reportedly unreliable,
             * and passing an app signature is rejected with noSessionErr
             * on 7.5. Walk the process list for the matching creator. */
            ProcessSerialNumber appPSN = {0, kNoProcess};
            ProcessInfoRec info;
            Str31 nameBuf;
            FSSpec procSpec;
            ProcessSerialNumber iter = {0, kNoProcess};
            while (GetNextProcess(&iter) == noErr) {
                info.processInfoLength = sizeof(info);
                info.processName = nameBuf;
                info.processAppSpec = &procSpec;
                if (GetProcessInformation(&iter, &info) == noErr) {
                    if (info.processSignature == fndrInfo.fdCreator) {
                        appPSN = iter;
                        break;
                    }
                }
            }
            if (appPSN.lowLongOfPSN == kNoProcess) {
                bridge_step("OPEN:psn-not-found");
                return fnfErr;
            }
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "OPEN:psn hi=%lx lo=%lx",
                         (unsigned long)appPSN.highLongOfPSN,
                         (unsigned long)appPSN.lowLongOfPSN);
                bridge_step(buf);
            }

            AEAddressDesc target;
            err = AECreateDesc(typeProcessSerialNumber, &appPSN,
                               sizeof(appPSN), &target);
            if (err != noErr) { bridge_step("OPEN:addr-create-failed"); return noErr; }

            AppleEvent evt, reply;
            err = AECreateAppleEvent('misc', 'dosc',
                                     &target, kAutoGenerateReturnID,
                                     kAnyTransactionID, &evt);
            AEDisposeDesc(&target);
            if (err != noErr) return noErr;

            /* Direct parameter: a one-line Perl bootstrap that runs the
             * actual script. MacPerl's 'dosc' handler executes typeChar
             * parameters as inline Perl. We construct the path string
             * from the FSSpec (volume:dirID:filename → Mac-style path).
             *
             * For files at the volume root, "VolumeName:filename" works.
             * docSpec gives us volume vRefNum and a Pascal filename. */
            {
                Str255 volName;
                HParamBlockRec vpb;
                memset(&vpb, 0, sizeof(vpb));
                vpb.volumeParam.ioNamePtr = volName;
                vpb.volumeParam.ioVRefNum = docSpec.vRefNum;
                vpb.volumeParam.ioVolIndex = 0;
                if (PBHGetVInfoSync(&vpb) == noErr) {
                    /* MacPerl's dosc handler expects keyDirectObject as a
                     * plain typeChar string of Perl source. We send a tiny
                     * stub that reads the real script file and evals it.
                     *
                     * Critical: MacPerl opens text files in Mac mode, so
                     * the slurped content has \r line terminators. MacPerl's
                     * own eval silently fails to parse sub definitions from
                     * \r-terminated source (eval returns undef with empty
                     * $@ — no error, just no subs defined). We convert
                     * \r -> \n before eval so the parser sees LF newlines. */
                    char src[512];
                    int plen = volName[0];
                    int dnlen = docSpec.name[0];
                    snprintf(src, sizeof(src),
                             "open(R,\"<%.*s:%.*s\")||die;"
                             "local $/;$c=<R>;close R;"
                             "$c=~tr/\\r/\\n/;"
                             "eval $c;die $@ if $@;\r",
                             plen, (char *)volName + 1,
                             dnlen, (char *)docSpec.name + 1);
                    AEPutParamPtr(&evt, keyDirectObject, typeChar,
                                  src, (long)strlen(src));
                    (void)vpb; (void)docSpec;
                }
            }

            /* Flags match MPDrop.c (MacPerl's own droplet sender).
             * kAEAlwaysInteract is required: without it AppleEvent
             * Manager refuses to deliver to apps that haven't yet
             * called AESetInteractionAllowed, which DoScript needs.
             * kAENoReply because DoScript runs the script async via
             * AESuspendTheCurrentEvent — kAEWaitReply would block. */
            bridge_step("OPEN:pre-aesend");
            err = AESend(&evt, NULL,
                         kAENoReply | kAEAlwaysInteract,
                         kAENormalPriority,
                         kAEDefaultTimeout, NULL, NULL);
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "OPEN:post-aesend err=%d", (int)err);
                bridge_step(buf);
            }
            AEDisposeDesc(&evt);
            return err;
        }
        return noErr;
    }
}

static OSErr do_quit_front(void)
{
    ProcessSerialNumber psn;
    OSErr err = GetFrontProcess(&psn);
    if (err != noErr) return err;

    AEAddressDesc target;
    err = AECreateDesc(typeProcessSerialNumber, &psn, sizeof(psn), &target);
    if (err != noErr) return err;

    AppleEvent evt, reply;
    err = AECreateAppleEvent(kCoreEventClass, kAEQuitApplication,
                             &target, kAutoGenerateReturnID,
                             kAnyTransactionID, &evt);
    AEDisposeDesc(&target);
    if (err != noErr) return err;

    err = AESend(&evt, &reply, kAENoReply, kAENormalPriority,
                 kNoTimeOut, NULL, NULL);
    AEDisposeDesc(&evt);
    return err;
}

static void write_result(OSErr result)
{
    FSSpec spec;
    short ref;
    char buf[16];
    long len;

    FSMakeFSSpec(0, 0, "\pHost:_bridge_result", &spec);
    FSpDelete(&spec);
    FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    if (FSpOpenDF(&spec, fsWrPerm, &ref) != noErr) return;
    len = snprintf(buf, sizeof(buf), "%d\r", (int)result);
    FSWrite(ref, &len, buf);
    FSClose(ref);
    FlushVol(NULL, 0);
}

static void write_marker_once(void)
{
    static Boolean written = false;
    if (written) return;
    written = true;
    FSSpec mk;
    if (FSMakeFSSpec(0, 0, "\pHost:bridge_loaded", &mk) == fnfErr)
        FSpCreate(&mk, 'ttxt', 'TEXT', smSystemScript);
}

static void write_heartbeat(void)
{
    static unsigned long last_ticks = 0;
    unsigned long now = TickCount();
    if (now - last_ticks < 120) return;
    last_ticks = now;
    gHeartbeat++;

    FSSpec hb;
    short ref;
    if (FSMakeFSSpec(0, 0, "\pHost:bridge_heartbeat", &hb) == fnfErr)
        FSpCreate(&hb, 'ttxt', 'TEXT', smSystemScript);
    if (FSpOpenDF(&hb, fsWrPerm, &ref) == noErr) {
        char buf[16];
        long len = snprintf(buf, sizeof(buf), "%d\r", gHeartbeat);
        SetEOF(ref, 0);
        FSWrite(ref, &len, buf);
        FSClose(ref);
        FlushVol(NULL, 0);
    }
    draw_status();
}

/* Debug breadcrumb: leaves a single-step trail file on Host: so we can
 * see which point in do_open_document we reached if the agent hangs. */
static void bridge_step(const char *tag)
{
    FSSpec sp;
    short ref;
    if (FSMakeFSSpec(0, 0, "\pHost:bridge_step", &sp) == fnfErr)
        FSpCreate(&sp, 'ttxt', 'TEXT', smSystemScript);
    if (FSpOpenDF(&sp, fsWrPerm, &ref) != noErr) return;
    long len = (long)strlen(tag);
    SetEOF(ref, 0);
    FSWrite(ref, &len, tag);
    FSClose(ref);
    FlushVol(NULL, 0);
}

static void poll_bridge(void)
{
    write_marker_once();
    write_heartbeat();

    FSSpec cmd_spec;
    if (FSMakeFSSpec(0, 0, "\pHost:_bridge_cmd", &cmd_spec) != noErr)
        return;

    short refNum;
    if (FSpOpenDF(&cmd_spec, fsRdPerm, &refNum) != noErr)
        return;

    char cmd[256];
    long count = sizeof(cmd) - 1;
    memset(cmd, 0, sizeof(cmd));
    OSErr err = FSRead(refNum, &count, cmd);
    FSClose(refNum);
    FSpDelete(&cmd_spec);

    if (err != noErr && err != eofErr) return;
    cmd[count] = '\0';

    /* Stash for the status window (trim to 80 chars). */
    int vlen = (int)strlen(cmd);
    while (vlen > 0 && (cmd[vlen-1] == '\n' || cmd[vlen-1] == '\r'))
        vlen--;
    if (vlen > 80) vlen = 80;
    gLastCmd[0] = (unsigned char)vlen;
    memcpy(gLastCmd + 1, cmd, vlen);

    OSErr result = -1;
    if (strncmp(cmd, "LAUNCH ", 7) == 0) {
        int len = (int)strlen(cmd + 7);
        while (len > 0 && (cmd[7+len-1] == '\n' || cmd[7+len-1] == '\r'
                        || cmd[7+len-1] == ' '))
            len--;
        if (len > 255) len = 255;
        unsigned char path[256];
        path[0] = (unsigned char)len;
        memcpy(path + 1, cmd + 7, len);
        result = do_launch(path);
    } else if (strncmp(cmd, "OPEN ", 5) == 0) {
        int len = (int)strlen(cmd + 5);
        while (len > 0 && (cmd[5+len-1] == '\n' || cmd[5+len-1] == '\r'
                        || cmd[5+len-1] == ' '))
            len--;
        if (len > 255) len = 255;
        unsigned char path[256];
        path[0] = (unsigned char)len;
        memcpy(path + 1, cmd + 5, len);
        result = do_open_document(path);
    } else if (strncmp(cmd, "QUIT", 4) == 0) {
        result = do_quit_front();
    }

    gCmdCount++;
    gLastResult = result;
    write_result(result);
    draw_status();
}

/* ---------- AppleEvent handlers ---------- */

static pascal OSErr ae_quit(const AppleEvent *evt, AppleEvent *reply, long refcon)
{
    (void)evt; (void)reply; (void)refcon;
    gRunning = false;
    return noErr;
}

static pascal OSErr ae_ignore(const AppleEvent *evt, AppleEvent *reply, long refcon)
{
    (void)evt; (void)reply; (void)refcon;
    return noErr;
}

static void install_ae_handlers(void)
{
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                          NewAEEventHandlerUPP(ae_quit), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                          NewAEEventHandlerUPP(ae_ignore), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
                          NewAEEventHandlerUPP(ae_ignore), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
                          NewAEEventHandlerUPP(ae_ignore), 0, false);
}

/* ---------- Menus ---------- */

#define kAppleMenu  128
#define kFileMenu   129

static MenuHandle gAppleMenuH;
static MenuHandle gFileMenuH;

static void build_menus(void)
{
    gAppleMenuH = NewMenu(kAppleMenu, "\p\024"); /* 0x14 = apple glyph */
    AppendMenu(gAppleMenuH, "\pAbout BridgeAgent\311"); /* \311 = ellipsis */
    AppendMenu(gAppleMenuH, "\p(-");
    AppendResMenu(gAppleMenuH, 'DRVR');
    InsertMenu(gAppleMenuH, 0);

    gFileMenuH = NewMenu(kFileMenu, "\pFile");
    AppendMenu(gFileMenuH, "\pQuit/Q");
    InsertMenu(gFileMenuH, 0);

    DrawMenuBar();
}

static void do_about(void)
{
    ParamText("\pMacPhoenix BridgeAgent\rAutomation agent.",
              "\p", "\p", "\p");
    NoteAlert(128, NULL);
}

static void do_menu(long choice)
{
    short menuID = HiWord(choice);
    short item   = LoWord(choice);

    if (menuID == kAppleMenu) {
        if (item == 1) {
            do_about();
        } else {
            Str255 daName;
            GetMenuItemText(gAppleMenuH, item, daName);
            OpenDeskAcc(daName);
        }
    } else if (menuID == kFileMenu) {
        if (item == 1) gRunning = false;
    }
    HiliteMenu(0);
}

/* ---------- Event dispatch ---------- */

static void handle_event(EventRecord *evt)
{
    switch (evt->what) {
    case mouseDown: {
        WindowPtr win;
        short part = FindWindow(evt->where, &win);
        switch (part) {
        case inMenuBar:
            do_menu(MenuSelect(evt->where));
            break;
        case inSysWindow:
            SystemClick(evt, win);
            break;
        case inDrag:
            if (win == gStatusWin) {
                Rect bounds = (*GetGrayRgn())->rgnBBox;
                DragWindow(win, evt->where, &bounds);
            }
            break;
        case inContent:
            if (win != FrontWindow()) SelectWindow(win);
            break;
        case inGoAway:
            if (TrackGoAway(win, evt->where)) HideWindow(win);
            break;
        }
        break;
    }
    case keyDown:
    case autoKey: {
        char ch = evt->message & charCodeMask;
        if (evt->modifiers & cmdKey) {
            long choice = MenuKey(ch);
            if (HiWord(choice)) do_menu(choice);
        }
        break;
    }
    case updateEvt: {
        WindowPtr win = (WindowPtr)evt->message;
        BeginUpdate(win);
        if (win == gStatusWin) draw_status();
        EndUpdate(win);
        break;
    }
    case kHighLevelEvent:
        AEProcessAppleEvent(evt);
        break;
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
    MaxApplZone();

    gLastCmd[0] = 6; memcpy(gLastCmd + 1, "(none)", 6);
    build_menus();
    install_ae_handlers();
    open_status_window();
    draw_status();

    EventRecord evt;
    while (gRunning) {
        if (WaitNextEvent(everyEvent, &evt, 6, NULL))
            handle_event(&evt);
        poll_bridge();
    }
    return 0;
}
