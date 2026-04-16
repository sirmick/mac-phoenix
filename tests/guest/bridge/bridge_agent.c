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

/* Locate the application that owns a given creator code by walking the
 * Desktop Manager database of every mounted volume. Returns noErr and
 * fills appSpec, or fnfErr if no app is registered for the creator. */
static OSErr find_app_for_creator(OSType creator, FSSpec *appSpec)
{
    short vIndex;
    for (vIndex = 1; ; vIndex++) {
        HParamBlockRec vpb;
        Str255 vname;
        memset(&vpb, 0, sizeof(vpb));
        vpb.volumeParam.ioNamePtr = vname;
        vpb.volumeParam.ioVolIndex = vIndex;
        if (PBHGetVInfoSync(&vpb) != noErr) return fnfErr;

        DTPBRec dt;
        memset(&dt, 0, sizeof(dt));
        dt.ioVRefNum = vpb.volumeParam.ioVRefNum;
        if (PBDTGetPath(&dt) != noErr) continue;

        Str255 appName;
        dt.ioNamePtr     = appName;
        dt.ioFileCreator = creator;
        dt.ioIndex       = 0;
        if (PBDTGetAPPLSync(&dt) == noErr &&
            FSMakeFSSpec(vpb.volumeParam.ioVRefNum, dt.ioAPPLParID,
                         appName, appSpec) == noErr) {
            return noErr;
        }
    }
}

/* Look up the Process Manager PSN for a process by creator signature.
 * Used after LaunchApplication because the PSN field returned in the
 * LaunchParamBlock is reportedly unreliable, and AECreateDesc with a
 * signature target gets noSessionErr on 7.5. */
static OSErr psn_for_creator(OSType creator, ProcessSerialNumber *outPSN)
{
    ProcessSerialNumber iter = {0, kNoProcess};
    Str31 nameBuf;
    FSSpec spec;
    while (GetNextProcess(&iter) == noErr) {
        ProcessInfoRec info;
        info.processInfoLength = sizeof(info);
        info.processName       = nameBuf;
        info.processAppSpec    = &spec;
        if (GetProcessInformation(&iter, &info) == noErr &&
            info.processSignature == creator) {
            *outPSN = iter;
            return noErr;
        }
    }
    return procNotFound;
}

static OSErr do_open_document(const unsigned char *path)
{
    FSSpec docSpec;
    OSErr err = FSMakeFSSpec(0, 0, path, &docSpec);
    if (err != noErr) return err;

    FInfo fndrInfo;
    err = FSpGetFInfo(&docSpec, &fndrInfo);
    if (err != noErr) return err;

    FSSpec appSpec;
    err = find_app_for_creator(fndrInfo.fdCreator, &appSpec);
    if (err != noErr) return err;

    /* Launch the app with no AppParameters. We send the script via a
     * follow-up 'misc'/'dosc' AppleEvent rather than packaging it as
     * an 'odoc' alias on launch, because MacPerl handles dosc as the
     * canonical "execute Perl source" entry point. */
    LaunchParamBlockRec lpb;
    memset(&lpb, 0, sizeof(lpb));
    lpb.launchBlockID      = extendedBlock;
    lpb.launchEPBLength    = extendedBlockLen;
    lpb.launchControlFlags = launchContinue | launchNoFileFlags;
    lpb.launchAppSpec      = &appSpec;
    err = LaunchApplication(&lpb);
    if (err != noErr) return err;

    /* Yield via Delay() rather than a nested WaitNextEvent loop.
     * Once MacPerl is frontmost, BridgeAgent's WNE doesn't always get
     * scheduled; Delay() is a Process Manager call that always yields.
     * ~3s gives MacPerl time to process its auto-generated 'oapp' AE
     * and register the 'misc'/'dosc' handler. */
    unsigned long finalTicks;
    Delay(180, &finalTicks);

    ProcessSerialNumber appPSN;
    if (psn_for_creator(fndrInfo.fdCreator, &appPSN) != noErr) return fnfErr;

    AEAddressDesc target;
    err = AECreateDesc(typeProcessSerialNumber, &appPSN,
                       sizeof(appPSN), &target);
    if (err != noErr) return err;

    AppleEvent evt;
    err = AECreateAppleEvent('misc', 'dosc',
                             &target, kAutoGenerateReturnID,
                             kAnyTransactionID, &evt);
    AEDisposeDesc(&target);
    if (err != noErr) return err;

    /* Direct parameter is a tiny Perl stub that reads the real script
     * and evals it. We can't just inline the script body — MacPerl
     * truncates large dosc payloads. */
    Str255 volName;
    HParamBlockRec vpb;
    memset(&vpb, 0, sizeof(vpb));
    vpb.volumeParam.ioNamePtr = volName;
    vpb.volumeParam.ioVRefNum = docSpec.vRefNum;
    if (PBHGetVInfoSync(&vpb) != noErr) {
        AEDisposeDesc(&evt);
        return ioErr;
    }

    /* MacPerl opens text files in Mac mode so the slurped content has
     * \r line terminators, and MacPerl's eval silently fails to install
     * sub definitions from \r-terminated source (eval returns undef
     * with empty $@ — no error, just no subs defined). The tr/// fixes
     * that by converting to \n before eval. */
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

    /* Flags match MPDrop.c (MacPerl's own droplet sender).
     * kAEAlwaysInteract: AE Manager otherwise refuses to deliver to
     * apps that haven't called AESetInteractionAllowed.
     * kAENoReply: DoScript runs async via AESuspendTheCurrentEvent. */
    err = AESend(&evt, NULL,
                 kAENoReply | kAEAlwaysInteract,
                 kAENormalPriority,
                 kAEDefaultTimeout, NULL, NULL);
    AEDisposeDesc(&evt);
    return err;
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

/* Send kAEShutDown ('shut') or kAERestart ('rest') to Finder. Finder quits
 * open apps first, then invokes the Shutdown Manager — same path as
 * Special → Shut Down / Restart in the menu. */
static OSErr do_system_event(AEEventID eventID)
{
    ProcessSerialNumber finderPSN = {0, kNoProcess};
    ProcessSerialNumber iter = {0, kNoProcess};
    ProcessInfoRec info;
    Str31 nameBuf;
    FSSpec procSpec;
    while (GetNextProcess(&iter) == noErr) {
        info.processInfoLength = sizeof(info);
        info.processName = nameBuf;
        info.processAppSpec = &procSpec;
        if (GetProcessInformation(&iter, &info) == noErr) {
            if (info.processSignature == 'MACS') {
                finderPSN = iter;
                break;
            }
        }
    }
    if (finderPSN.lowLongOfPSN == kNoProcess) return procNotFound;

    AEAddressDesc target;
    OSErr err = AECreateDesc(typeProcessSerialNumber, &finderPSN,
                             sizeof(finderPSN), &target);
    if (err != noErr) return err;

    AppleEvent evt;
    err = AECreateAppleEvent(kCoreEventClass, eventID,
                             &target, kAutoGenerateReturnID,
                             kAnyTransactionID, &evt);
    AEDisposeDesc(&target);
    if (err != noErr) return err;

    err = AESend(&evt, NULL, kAENoReply | kAEAlwaysInteract,
                 kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
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
    } else if (strncmp(cmd, "SHUTDOWN", 8) == 0) {
        result = do_system_event(kAEShutDown);
    } else if (strncmp(cmd, "RESTART", 7) == 0) {
        result = do_system_event(kAERestart);
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
