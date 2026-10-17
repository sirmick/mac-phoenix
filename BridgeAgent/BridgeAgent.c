/*
 * BridgeAgent - MacPhoenix automation agent
 *
 * A small application installed in System Folder:Startup Items. Finder
 * launches it at desktop time; it runs a WaitNextEvent loop at ~10Hz
 * polling the ExtFS "Host" volume for bridge commands.
 *
 * Protocol (file-based; all files live in a per-instance bridge dir
 * resolved at startup from :System Folder:Preferences:MacPhoenix.cfg
 * — host writes that file when it installs us. See bridge_cfg.h.
 * Resolved paths look like Host:MacPhoenix:<host_pid>:<leaf>):
 *   Host writes <bridge_dir>:_bridge_cmd     - one of:
 *       LAUNCH  <hfs-path>             — LaunchApplication
 *       OPEN    <hfs-path>             — generic 'aevt'/'odoc' to creator's app
 *       SCRIPT  <4-char creator>       — 'misc'/'dosc' with body from
 *                                        <bridge_dir>:_bridge_script,
 *                                        fire-and-forget. Works for
 *                                        McPL/LAND/MPS /ToyS/...
 *       EXEC    <4-char creator>       — same as SCRIPT but waits for
 *                                        the AE reply; captures stdout,
 *                                        stderr, and {Status} into
 *                                        <bridge_dir>:_bridge_reply.
 *                                        Canonical target: ToolServer
 *                                        (creator MPSX).
 *       COPY    <src>\r<dst folder>    — copy a file or folder (both forks,
 *                                        Finder info) into a folder
 *       QUIT / SHUTDOWN / RESTART      — Process / Shutdown manager
 *       SET_CLIPBOARD                  — see do_set_clipboard
 *   Agent reads, deletes, executes
 *   Agent writes <bridge_dir>:_bridge_result  - decimal OSErr, CR-terminated
 *
 * Liveness markers:
 *   <bridge_dir>:bridge_loaded       - created once at first poll
 *   <bridge_dir>:bridge_heartbeat    - rewritten every ~2s with a counter
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
#include <ShutDown.h>
#include <Scrap.h>
#include <Traps.h>
#include <Timer.h>
#include <Video.h>      /* VDSwitchInfoRec, cscSwitchMode */
#include <Displays.h>   /* GetMainDevice — already pulled by Quickdraw, kept explicit */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "bridge_cfg.h"
#include "bridge_paths.h"

/* Build stamp — compiler fills in at each build so we can confirm the
 * running BridgeAgent matches the latest source. Surfaced in the status
 * window, the heartbeat JSON, and /api/status. */
#define BRIDGE_AGENT_BUILD __DATE__ " " __TIME__

/* ---------- Status window state ---------- */

static WindowPtr gStatusWin = NULL;
static int       gHeartbeat = 0;
static int       gCmdCount  = 0;
static Str255    gLastCmd;
static OSErr     gLastResult = 0;
static Boolean   gRunning   = true;

/* Network info parsed from netcfg.txt on startup. The host writes
 * these fresh each launch so the values shown always match the
 * running bridge configuration. Empty string = field missing. */
static char gNetGateway[32] = "";
static char gNetGuestIp[32] = "";

/* Read one CR-terminated key=value line from `src` starting at *off.
 * Advances *off past the CR. Returns false at EOF. */
static Boolean read_line(const char *src, long srclen, long *off,
                         char *out, size_t outcap)
{
    if (*off >= srclen) return false;
    size_t w = 0;
    while (*off < srclen) {
        char c = src[(*off)++];
        if (c == '\r' || c == '\n') break;
        if (w + 1 < outcap) out[w++] = c;
    }
    out[w] = 0;
    return true;
}

/* Parse netcfg.txt (written by the host) for the gateway and guest
 * DHCP IP. Values are displayed in the status window. Called once
 * at startup; silent no-op if the file's absent (e.g. no --bridge). */
static void load_network_config(void)
{
    FSSpec spec;
    Str255 path; br_cfg_path(path, BR_FILE_NETCFG);
    if (FSMakeFSSpec(0, 0, path, &spec) != noErr) return;

    short ref;
    if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) return;

    long len = 0;
    GetEOF(ref, &len);
    if (len <= 0 || len > 4096) { FSClose(ref); return; }

    char *buf = (char *)NewPtr(len);
    if (!buf) { FSClose(ref); return; }

    long want = len;
    if (FSRead(ref, &want, buf) != noErr) {
        FSClose(ref); DisposePtr(buf); return;
    }
    FSClose(ref);

    long off = 0;
    char line[160];
    while (read_line(buf, want, &off, line, sizeof(line))) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line;
        const char *v = eq + 1;
        if (!strcmp(k, "gw")) {
            strncpy(gNetGateway, v, sizeof(gNetGateway) - 1);
        } else if (!strcmp(k, "guest")) {
            strncpy(gNetGuestIp, v, sizeof(gNetGuestIp) - 1);
        }
        /* mitm/ca_url keys still arrive in cfg from older host
         * builds — we just ignore them now that the MITM proxy is
         * gone. */
    }
    DisposePtr(buf);
}

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

    MoveTo(6, 18);
    DrawString("\pBridgeAgent");

    MoveTo(6, 34);
    snprintf(buf, sizeof(buf), "Build: %s", BRIDGE_AGENT_BUILD);
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    DrawString(pbuf);

    MoveTo(6, 52);
    snprintf(buf, sizeof(buf), "HB: %d", gHeartbeat);
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    DrawString(pbuf);

    MoveTo(6, 68);
    snprintf(buf, sizeof(buf), "Cmds: %d  err: %d",
             gCmdCount, (int)gLastResult);
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    DrawString(pbuf);

    MoveTo(6, 84);
    DrawString("\pLast: ");
    DrawString(gLastCmd);

    /* Network pane — values loaded once at startup from netcfg.txt. */
    MoveTo(6, 106);
    DrawString("\p--- Network ---");

    MoveTo(6, 122);
    snprintf(buf, sizeof(buf), "GW: %s",
             gNetGateway[0] ? gNetGateway : "(none)");
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    DrawString(pbuf);

    MoveTo(6, 138);
    snprintf(buf, sizeof(buf), "IP: %s",
             gNetGuestIp[0] ? gNetGuestIp : "(DHCP)");
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    DrawString(pbuf);

    SetPort(saved);
}

static void open_status_window(void)
{
    Rect bounds;
    /* ~40% of the prior 400-px width so we don't hog desktop real
     * estate. Height shrunk from 180 → 152 since the MITM row is
     * gone. */
    SetRect(&bounds, 40, 60, 200, 212);
    gStatusWin = NewWindow(NULL, &bounds, "\pBridgeAgent",
                           true, documentProc, (WindowPtr)-1, true, 0);
}

/* ---------- Bridge command handlers ---------- */

static void bridge_step(const char *tag);


/* LaunchApplication as the Finder does it: when the preferred partition in
 * the app's SIZE resource isn't free, launch in what is (down to the
 * minimum) instead of failing with memFullErr. */
static OSErr launch_like_finder(LaunchParamBlockRec *pb)
{
    OSErr err = LaunchApplication(pb);
    if (err == memFullErr) {
        pb->launchControlFlags |= launchUseMinimum;
        err = LaunchApplication(pb);
    }
    return err;
}

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
    return launch_like_finder(&pb);
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

/* Depth-limited walk of one folder for an application with this creator
 * (PBGetCatInfo works everywhere, PBCatSearch not under Executor). */
static Boolean walk_for_app(short vref, long dirID, OSType creator, int depth, FSSpec *appSpec)
{
    short index;
    for (index = 1; ; index++) {
        Str63 name;
        CInfoPBRec pb;
        memset(&pb, 0, sizeof(pb));
        pb.hFileInfo.ioNamePtr = name;
        pb.hFileInfo.ioVRefNum = vref;
        pb.hFileInfo.ioDirID = dirID;
        pb.hFileInfo.ioFDirIndex = index;
        if (PBGetCatInfoSync(&pb) != noErr) return false;
        if (pb.hFileInfo.ioFlAttrib & 0x10) {
            if (depth > 0 && walk_for_app(vref, pb.dirInfo.ioDrDirID, creator, depth - 1, appSpec))
                return true;
        } else if (pb.hFileInfo.ioFlFndrInfo.fdType == 'APPL' &&
                   pb.hFileInfo.ioFlFndrInfo.fdCreator == creator) {
            return FSMakeFSSpec(vref, dirID, name, appSpec) == noErr;
        }
    }
}

/* An application with this creator, by a catalog search of every volume:
 * for apps the desktop database doesn't know yet (copied onto a disk
 * outside the Finder: a System Folder built from snapshots starts with an
 * empty desktop database, and the Finder only registers apps it meets). */
static OSErr search_app_for_creator(OSType creator, FSSpec *appSpec)
{
    static char buffer[16384];
    short vIndex;
    for (vIndex = 1; ; vIndex++) {
        HParamBlockRec vpb;
        memset(&vpb, 0, sizeof(vpb));
        vpb.volumeParam.ioVolIndex = vIndex;
        if (PBHGetVInfoSync(&vpb) != noErr) return fnfErr;

        CInfoPBRec spec1, spec2;
        memset(&spec1, 0, sizeof(spec1));
        memset(&spec2, 0, sizeof(spec2));
        spec1.hFileInfo.ioFlFndrInfo.fdType    = 'APPL';
        spec1.hFileInfo.ioFlFndrInfo.fdCreator = creator;
        spec2.hFileInfo.ioFlFndrInfo.fdType    = (OSType)0xFFFFFFFF;
        spec2.hFileInfo.ioFlFndrInfo.fdCreator = (OSType)0xFFFFFFFF;
        spec2.hFileInfo.ioFlAttrib             = 0x10;      /* files only */

        CSParam csp;
        memset(&csp, 0, sizeof(csp));
        csp.ioVRefNum       = vpb.volumeParam.ioVRefNum;
        csp.ioMatchPtr      = appSpec;
        csp.ioReqMatchCount = 1;
        csp.ioSearchBits    = fsSBFlFndrInfo | fsSBFlAttrib;
        csp.ioSearchInfo1   = &spec1;
        csp.ioSearchInfo2   = &spec2;
        csp.ioSearchTime    = 0;
        csp.ioOptBuffer     = buffer;
        csp.ioOptBufSize    = sizeof(buffer);
        OSErr err = PBCatSearchSync(&csp);
        if ((err == noErr || err == eofErr) && csp.ioActMatchCount > 0)
            return noErr;
        if (walk_for_app(vpb.volumeParam.ioVRefNum, 2 /* root */, creator, 3, appSpec))   /* no catalog
                                                    search (Executor), or it missed: walk the folders */
            return noErr;
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

/* Launch the app for a creator (via Desktop Database lookup), wait for it
 * to register AE handlers, return its PSN. Used by both OPEN (generic
 * 'odoc') and SCRIPT (generic 'misc'/'dosc'). LaunchApplication brings an
 * already-running app to front, so we don't pre-check; the 3s Delay is the
 * tax in either case but is what classic Mac apps need to install
 * 'misc'/'dosc' / 'aevt'/'odoc' handlers after their auto 'oapp' AE.
 *
 * Yield via Delay() rather than a nested WaitNextEvent loop — once the
 * target is frontmost, BridgeAgent's own WNE doesn't always get scheduled,
 * but Delay() is a Process Manager call that always yields. */
static OSErr launch_app_for_creator(OSType creator, ProcessSerialNumber *outPSN)
{
    FSSpec appSpec;
    OSErr err;

    /* Already running: that process, not whichever copy the lookup finds
     * (two MPW Shells, Apple's and CodeWarrior's, share a creator; the one
     * a caller launched by path is the one it means). */
    if (psn_for_creator(creator, outPSN) == noErr) {
        SetFrontProcess(outPSN);
        return noErr;
    }

    err = find_app_for_creator(creator, &appSpec);
    if (err != noErr) err = search_app_for_creator(creator, &appSpec);
    if (err != noErr) return err;

    LaunchParamBlockRec lpb;
    memset(&lpb, 0, sizeof(lpb));
    lpb.launchBlockID      = extendedBlock;
    lpb.launchEPBLength    = extendedBlockLen;
    lpb.launchControlFlags = launchContinue | launchNoFileFlags;
    lpb.launchAppSpec      = &appSpec;
    err = launch_like_finder(&lpb);
    if (err != noErr) return err;

    unsigned long finalTicks;
    Delay(180, &finalTicks);

    return psn_for_creator(creator, outPSN);
}

/* Send a one-direct-parameter AppleEvent to a process by PSN.
 * Flags match MPDrop.c (MacPerl's droplet sender) and work for any
 * "do script"-class app: kAEAlwaysInteract (AE Manager otherwise refuses
 * to deliver to apps that haven't called AESetInteractionAllowed),
 * kAENoReply (target may run the script async via AESuspendTheCurrentEvent). */
static OSErr send_aevt_psn(const ProcessSerialNumber *psn,
                           AEEventClass cls, AEEventID id,
                           DescType paramType, const void *paramData,
                           long paramLen)
{
    AEAddressDesc target;
    OSErr err = AECreateDesc(typeProcessSerialNumber, psn, sizeof(*psn), &target);
    if (err != noErr) return err;

    AppleEvent evt;
    err = AECreateAppleEvent(cls, id, &target, kAutoGenerateReturnID,
                             kAnyTransactionID, &evt);
    AEDisposeDesc(&target);
    if (err != noErr) return err;

    err = AEPutParamPtr(&evt, keyDirectObject, paramType, paramData, paramLen);
    if (err == noErr) {
        err = AESend(&evt, NULL,
                     kAENoReply | kAEAlwaysInteract,
                     kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
    }
    AEDisposeDesc(&evt);
    return err;
}

/* ---------- COPY: a file or folder, both forks and Finder info ----------
 *
 * COPY <source path>\r<destination folder path>: copies the item into the
 * folder under its own name, recursively for a folder. Used to place
 * software from a mounted disk or CD image without the Finder (and without
 * screen coordinates). An item already there is an error (dupFNErr). */

#define COPY_BUF_SIZE (32L * 1024)

static OSErr copy_fork(const FSSpec *src, const FSSpec *dst, Boolean rsrc, Ptr buf)
{
    short in, out;
    OSErr err = rsrc ? FSpOpenRF(src, fsRdPerm, &in) : FSpOpenDF(src, fsRdPerm, &in);
    if (err != noErr) return err;
    err = rsrc ? FSpOpenRF(dst, fsWrPerm, &out) : FSpOpenDF(dst, fsWrPerm, &out);
    if (err != noErr) { FSClose(in); return err; }
    for (;;) {
        long n = COPY_BUF_SIZE;
        OSErr rerr = FSRead(in, &n, buf);
        if (n > 0) {
            long w = n;
            err = FSWrite(out, &w, buf);
            if (err != noErr) break;
        }
        if (rerr == eofErr || (rerr == noErr && n == 0)) { err = noErr; break; }
        if (rerr != noErr) { err = rerr; break; }
    }
    FSClose(in);
    FSClose(out);
    return err;
}

/* Catalog info of spec into pb (with its own name buffer). */
static OSErr get_cat_info(const FSSpec *spec, CInfoPBRec *pb, Str63 name)
{
    memcpy(name, spec->name, spec->name[0] + 1);
    memset(pb, 0, sizeof(*pb));
    pb->hFileInfo.ioNamePtr = name;
    pb->hFileInfo.ioVRefNum = spec->vRefNum;
    pb->hFileInfo.ioDirID = spec->parID;
    pb->hFileInfo.ioFDirIndex = 0;
    return PBGetCatInfoSync(pb);
}

static OSErr copy_item(const FSSpec *src, short dstVRef, long dstDirID, Ptr buf)
{
    CInfoPBRec pb;
    Str63 name;
    OSErr err = get_cat_info(src, &pb, name);
    if (err != noErr) return err;

    FSSpec dst;
    err = FSMakeFSSpec(dstVRef, dstDirID, src->name, &dst);
    if (err == noErr) return dupFNErr;
    if (err != fnfErr) return err;

    Boolean is_dir = (pb.hFileInfo.ioFlAttrib & ioDirMask) != 0;
    if (is_dir) {
        long newDirID;
        err = FSpDirCreate(&dst, smSystemScript, &newDirID);
        if (err != noErr) return err;
        /* Children, by index, before the folder's own info (dates) is set. */
        long srcDirID = pb.dirInfo.ioDrDirID;
        for (short i = 1; ; i++) {
            CInfoPBRec cpb;
            Str63 cname;
            memset(&cpb, 0, sizeof(cpb));
            cname[0] = 0;
            cpb.hFileInfo.ioNamePtr = cname;
            cpb.hFileInfo.ioVRefNum = src->vRefNum;
            cpb.hFileInfo.ioDirID = srcDirID;
            cpb.hFileInfo.ioFDirIndex = i;
            if (PBGetCatInfoSync(&cpb) != noErr) break;
            FSSpec child;
            err = FSMakeFSSpec(src->vRefNum, srcDirID, cname, &child);
            if (err != noErr) return err;
            err = copy_item(&child, dstVRef, newDirID, buf);
            if (err != noErr) return err;
        }
        CInfoPBRec dpb;
        Str63 dname;
        err = get_cat_info(&dst, &dpb, dname);
        if (err != noErr) return err;
        dpb.dirInfo.ioDrUsrWds = pb.dirInfo.ioDrUsrWds;
        dpb.dirInfo.ioDrFndrInfo = pb.dirInfo.ioDrFndrInfo;
        dpb.dirInfo.ioDrCrDat = pb.dirInfo.ioDrCrDat;
        dpb.dirInfo.ioDrMdDat = pb.dirInfo.ioDrMdDat;
        dpb.dirInfo.ioDrDirID = dst.parID;
        dpb.dirInfo.ioNamePtr = dname;
        return PBSetCatInfoSync(&dpb);
    }

    err = FSpCreate(&dst, pb.hFileInfo.ioFlFndrInfo.fdCreator,
                    pb.hFileInfo.ioFlFndrInfo.fdType, smSystemScript);
    if (err != noErr) return err;
    if (pb.hFileInfo.ioFlLgLen > 0 && (err = copy_fork(src, &dst, false, buf)) != noErr) return err;
    if (pb.hFileInfo.ioFlRLgLen > 0 && (err = copy_fork(src, &dst, true, buf)) != noErr) return err;
    CInfoPBRec fpb;
    Str63 fname;
    err = get_cat_info(&dst, &fpb, fname);
    if (err != noErr) return err;
    fpb.hFileInfo.ioFlFndrInfo = pb.hFileInfo.ioFlFndrInfo;
    fpb.hFileInfo.ioFlFndrInfo.fdFlags &= ~kHasBeenInited;
    fpb.hFileInfo.ioFlXFndrInfo = pb.hFileInfo.ioFlXFndrInfo;
    fpb.hFileInfo.ioFlCrDat = pb.hFileInfo.ioFlCrDat;
    fpb.hFileInfo.ioFlMdDat = pb.hFileInfo.ioFlMdDat;
    fpb.hFileInfo.ioDirID = dst.parID;
    fpb.hFileInfo.ioNamePtr = fname;
    return PBSetCatInfoSync(&fpb);
}

/* "COPY src\rdst": src a file or folder, dst an existing folder. */
static OSErr do_copy(const char *args)
{
    const char *sep = strchr(args, '\r');
    if (!sep) return paramErr;
    unsigned char srcPath[256], dstPath[256];
    int sl = (int)(sep - args), dl = (int)strlen(sep + 1);
    while (dl > 0 && (sep[dl] == '\r' || sep[dl] == '\n')) dl--;
    if (sl > 255 || dl > 255) return bdNamErr;
    srcPath[0] = (unsigned char)sl; memcpy(srcPath + 1, args, sl);
    dstPath[0] = (unsigned char)dl; memcpy(dstPath + 1, sep + 1, dl);

    FSSpec src, dst;
    OSErr err = FSMakeFSSpec(0, 0, srcPath, &src);
    if (err != noErr) return err;
    err = FSMakeFSSpec(0, 0, dstPath, &dst);
    if (err != noErr) return err;
    CInfoPBRec pb;
    Str63 name;
    err = get_cat_info(&dst, &pb, name);
    if (err != noErr) return err;
    if (!(pb.hFileInfo.ioFlAttrib & ioDirMask)) return dirNFErr;

    Ptr buf = NewPtr(COPY_BUF_SIZE);
    if (!buf) return memFullErr;
    err = copy_item(&src, dst.vRefNum, pb.dirInfo.ioDrDirID, buf);
    DisposePtr(buf);
    FlushVol(NULL, dst.vRefNum);
    return err;
}

/* An 'aevt'/'odoc' for one document (a list of one alias), to `target`. */
static OSErr make_odoc(const AEAddressDesc *target, const FSSpec *docSpec, AppleEvent *evt)
{
    AliasHandle alias = NULL;
    OSErr err = NewAlias(NULL, docSpec, &alias);
    if (err != noErr || alias == NULL) return err ? err : memFullErr;

    err = AECreateAppleEvent(kCoreEventClass, kAEOpenDocuments, target,
                             kAutoGenerateReturnID, kAnyTransactionID, evt);
    if (err != noErr) { DisposeHandle((Handle)alias); return err; }

    AEDescList docList;
    err = AECreateList(NULL, 0, false, &docList);
    if (err == noErr) {
        HLock((Handle)alias);
        err = AEPutPtr(&docList, 1, typeAlias, *alias, GetHandleSize((Handle)alias));
        HUnlock((Handle)alias);
        if (err == noErr)
            err = AEPutParamDesc(evt, keyDirectObject, &docList);
        AEDisposeDesc(&docList);
    }
    DisposeHandle((Handle)alias);
    if (err != noErr) AEDisposeDesc(evt);
    return err;
}

/* Open a document as the Finder does on a double-click. An app that isn't
 * running is launched with the 'odoc' in its launch parameters: the Process
 * Manager hands it over as an Apple event, or, to an app that doesn't take
 * high-level events (Apple's Installer), as classic startup documents. A
 * running app is sent the 'odoc'. Generic over any creator: MacPerl,
 * Frontier, ResEdit, Disk Copy, the Installer ... */
static OSErr do_open_document(const unsigned char *path)
{
    FSSpec docSpec;
    OSErr err = FSMakeFSSpec(0, 0, path, &docSpec);
    if (err != noErr) return err;

    FInfo fndrInfo;
    err = FSpGetFInfo(&docSpec, &fndrInfo);
    if (err != noErr) return err;

    ProcessSerialNumber appPSN;
    AEAddressDesc target;
    AppleEvent evt;
    if (psn_for_creator(fndrInfo.fdCreator, &appPSN) != noErr) {
        FSSpec appSpec;
        err = find_app_for_creator(fndrInfo.fdCreator, &appSpec);
        if (err != noErr) err = search_app_for_creator(fndrInfo.fdCreator, &appSpec);
        if (err != noErr) return err;
        OSType sig = fndrInfo.fdCreator;
        err = AECreateDesc(typeApplSignature, &sig, sizeof(sig), &target);
        if (err != noErr) return err;
        err = make_odoc(&target, &docSpec, &evt);
        AEDisposeDesc(&target);
        if (err != noErr) return err;

        AEDesc params;
        err = AECoerceDesc(&evt, typeAppParameters, &params);
        AEDisposeDesc(&evt);
        if (err != noErr) return err;
        /* The parameters go in a block of their own in the system heap,
         * never freed here: the Process Manager holds on to them, and
         * disposes of them when the launched app quits. Handed a pointer
         * into our own heap (the coerced descriptor's data), that dispose
         * corrupted this heap: BridgeAgent died (system errors 3, 10) as
         * an Installer it had launched quit. */
        long size = GetHandleSize(params.dataHandle);
        Ptr block = NewPtrSys(size);
        if (block == NULL) { AEDisposeDesc(&params); return memFullErr; }
        BlockMoveData(*params.dataHandle, block, size);
        AEDisposeDesc(&params);
        LaunchParamBlockRec lpb;
        memset(&lpb, 0, sizeof(lpb));
        lpb.launchBlockID       = extendedBlock;
        lpb.launchEPBLength     = extendedBlockLen;
        lpb.launchControlFlags  = launchContinue | launchNoFileFlags;
        lpb.launchAppSpec       = &appSpec;
        lpb.launchAppParameters = (AppParametersPtr)block;
        return launch_like_finder(&lpb);
    }

    err = AECreateDesc(typeProcessSerialNumber, &appPSN, sizeof(appPSN), &target);
    if (err != noErr) return err;
    err = make_odoc(&target, &docSpec, &evt);
    AEDisposeDesc(&target);
    if (err != noErr) return err;
    err = AESend(&evt, NULL, kAENoReply | kAEAlwaysInteract,
                 kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
    AEDisposeDesc(&evt);
    return err;
}

/* Read Host:_bridge_script into a freshly NewPtr'd buffer. Caller frees.
 * Returns noErr + (*outBuf, *outLen) on success; *outBuf is NULL on empty.
 * The file is consumed (deleted) on success — single-shot like _bridge_cmd. */
static OSErr load_bridge_script(char **outBuf, long *outLen)
{
    *outBuf = NULL;
    *outLen = 0;

    FSSpec spec;
    Str255 path; br_cfg_path(path, BR_FILE_SCRIPT);
    OSErr err = FSMakeFSSpec(0, 0, path, &spec);
    if (err != noErr) return err;

    short ref;
    err = FSpOpenDF(&spec, fsRdPerm, &ref);
    if (err != noErr) return err;

    long size = 0;
    err = GetEOF(ref, &size);
    if (err != noErr || size < 0) { FSClose(ref); return err ? err : paramErr; }

    if (size == 0) {
        FSClose(ref);
        FSpDelete(&spec);
        return noErr;  /* empty script is legal, lets host send a no-op */
    }

    char *buf = (char *)NewPtr(size);
    if (!buf) { FSClose(ref); return memFullErr; }

    long rlen = size;
    err = FSRead(ref, &rlen, buf);
    FSClose(ref);
    if (err != noErr && err != eofErr) { DisposePtr(buf); return err; }

    FSpDelete(&spec);
    *outBuf = buf;
    *outLen = rlen;
    return noErr;
}

/* Send a script source verbatim to the app for `creator` via 'misc'/'dosc'
 * ("do script"). Generic — works for MacPerl (creator 'McPL'), Frontier
 * UserLand 5 ('LAND'), AppleScript editor ('ToyS'), MPW Toolserver ('MPS '),
 * or anything else registered for 'misc'/'dosc'. The host is responsible
 * for any language-specific escaping or workarounds (e.g. MacPerl needs
 * \n, not \r, in eval bodies — that's a host concern, not ours). */
static OSErr do_script(OSType creator)
{
    char *src = NULL;
    long src_len = 0;
    OSErr err = load_bridge_script(&src, &src_len);
    if (err != noErr) return err;

    ProcessSerialNumber appPSN;
    err = launch_app_for_creator(creator, &appPSN);
    if (err != noErr) { if (src) DisposePtr(src); return err; }

    if (src && src_len > 0) {
        err = send_aevt_psn(&appPSN, 'misc', 'dosc',
                            typeChar, src, src_len);
    }
    if (src) DisposePtr(src);
    return err;
}

/* Write a single AE reply payload file. Format is text with three sections,
 * each preceded by a CR-terminated header line:
 *
 *   STDOUT <decimal-byte-count>\r
 *   <stdout bytes>
 *   STDERR <decimal-byte-count>\r
 *   <stderr bytes>
 *   STATUS <decimal>\r
 *
 * Sections that aren't present are written with count 0. The host parses
 * the header lines and slices out the byte ranges. We chose this over
 * AppleSingle / typeAEList serialisation because the host doesn't link
 * against any AE library; this format is a few lines of C on each side. */
static OSErr write_bridge_reply(const char *stdout_buf, long stdout_len,
                                const char *stderr_buf, long stderr_len,
                                long status)
{
    Str255 path; br_cfg_path(path, BR_FILE_REPLY);
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, path, &spec);
    if (err == fnfErr) {
        err = FSpCreate(&spec, 'MxBr', 'TEXT', smSystemScript);
        if (err != noErr) return err;
    } else if (err != noErr) {
        return err;
    } else {
        FSpDelete(&spec);
        err = FSpCreate(&spec, 'MxBr', 'TEXT', smSystemScript);
        if (err != noErr) return err;
    }

    short ref;
    err = FSpOpenDF(&spec, fsWrPerm, &ref);
    if (err != noErr) return err;

    char hdr[64];
    long n;

    n = snprintf(hdr, sizeof(hdr), "STDOUT %ld\r", stdout_len);
    FSWrite(ref, &n, hdr);
    if (stdout_len > 0 && stdout_buf) {
        long len = stdout_len;
        FSWrite(ref, &len, stdout_buf);
    }

    n = snprintf(hdr, sizeof(hdr), "STDERR %ld\r", stderr_len);
    FSWrite(ref, &n, hdr);
    if (stderr_len > 0 && stderr_buf) {
        long len = stderr_len;
        FSWrite(ref, &len, stderr_buf);
    }

    n = snprintf(hdr, sizeof(hdr), "STATUS %ld\r", status);
    FSWrite(ref, &n, hdr);

    FSClose(ref);
    return noErr;
}

/* Pull a typeChar parameter out of an AE descriptor, returning a freshly
 * NewPtr'd buffer. Caller frees. Returns NULL + 0 if the parameter isn't
 * present or has zero size. */
static void extract_text_param(const AppleEvent *ae, AEKeyword key,
                               char **outBuf, long *outLen)
{
    *outBuf = NULL;
    *outLen = 0;

    DescType actualType;
    Size actualSize;
    OSErr err = AESizeOfParam(ae, key, &actualType, &actualSize);
    if (err != noErr || actualSize <= 0) return;

    char *buf = (char *)NewPtr(actualSize);
    if (!buf) return;

    Size got = 0;
    err = AEGetParamPtr(ae, key, typeChar, &actualType,
                        buf, actualSize, &got);
    if (err != noErr) { DisposePtr(buf); return; }

    *outBuf = buf;
    *outLen = got;
}

/* Pull keyErrorNumber as a signed long. Returns 0 if absent. */
static long extract_status_param(const AppleEvent *ae)
{
    DescType actualType;
    Size actualSize;
    long status = 0;
    Size got = 0;
    OSErr err = AEGetParamPtr(ae, keyErrorNumber, typeLongInteger,
                              &actualType, &status, sizeof(status), &got);
    if (err == noErr && got >= (Size)sizeof(status)) return status;

    short s16 = 0;
    err = AEGetParamPtr(ae, keyErrorNumber, typeShortInteger,
                        &actualType, &s16, sizeof(s16), &got);
    if (err == noErr && got >= (Size)sizeof(s16)) return (long)s16;

    return 0;
}

/* Run src through creator's do-script ('misc'/'dosc') and wait for the
 * reply, timeout ticks at most: stdout (keyDirectObject), stderr
 * (keyErrorString) and {Status} (keyErrorNumber), as ToolServer ('MPSX')
 * fills them. The MPW Shell doesn't return useful reply payloads; use
 * do_script for that. Buffers are NewPtr'd (caller frees); an OSErr if the
 * app couldn't be reached. */
static OSErr exec_core(OSType creator, const char *src, long src_len, long timeout,
                       char **out, long *out_len, char **errs, long *errs_len, long *status)
{
    *out = *errs = NULL;
    *out_len = *errs_len = *status = 0;

    ProcessSerialNumber appPSN;
    OSErr err = launch_app_for_creator(creator, &appPSN);
    if (err != noErr) return err;

    AEAddressDesc target;
    err = AECreateDesc(typeProcessSerialNumber, &appPSN, sizeof(appPSN), &target);
    if (err != noErr) return err;

    AppleEvent evt;
    err = AECreateAppleEvent('misc', 'dosc', &target,
                             kAutoGenerateReturnID, kAnyTransactionID, &evt);
    AEDisposeDesc(&target);
    if (err != noErr) return err;

    if (src && src_len > 0)
        err = AEPutParamPtr(&evt, keyDirectObject, typeChar, src, src_len);
    if (err != noErr) { AEDisposeDesc(&evt); return err; }

    /* Zero-init: an AEDesc with descriptorType=typeNull (0) and
     * dataHandle=NULL is the documented "empty" state — AEDisposeDesc on
     * it is a no-op. AEInitializeDesc is Carbon-only / not in the Retro68
     * universal headers we build against. */
    AppleEvent reply;
    memset(&reply, 0, sizeof(reply));
    err = AESend(&evt, &reply, kAEWaitReply | kAEAlwaysInteract,
                 kAENormalPriority, timeout, NULL, NULL);
    AEDisposeDesc(&evt);
    if (err != noErr) { AEDisposeDesc(&reply); return err; }

    extract_text_param(&reply, keyDirectObject, out, out_len);
    extract_text_param(&reply, keyErrorString,  errs, errs_len);
    *status = extract_status_param(&reply);
    AEDisposeDesc(&reply);
    return noErr;
}

/* EXEC <creator> — like SCRIPT but waits for the AE reply and captures
 * stdout / stderr / exit status into _bridge_reply (exec_core). A minute
 * at most; the trap's EXEC (bridge v2) takes a timeout. */
static OSErr do_exec(OSType creator)
{
    char *src = NULL;
    long src_len = 0;
    OSErr err = load_bridge_script(&src, &src_len);
    if (err != noErr) return err;

    char *out, *errs;
    long out_len, errs_len, status;
    err = exec_core(creator, src, src_len, 60 * 60, &out, &out_len, &errs, &errs_len, &status);
    if (src) DisposePtr(src);
    if (err != noErr) {
        write_bridge_reply(NULL, 0, NULL, 0, (long)err);
        return err;
    }
    OSErr werr = write_bridge_reply(out, out_len, errs, errs_len, status);
    if (out) DisposePtr(out);
    if (errs) DisposePtr(errs);
    return werr;
}

/* Send kAEQuitApplication to an arbitrary PSN. The caller supplies the
 * target because we self-foreground before dispatching bridge commands, so
 * GetFrontProcess at send-time would return us (the bridge agent), not the
 * user's original frontmost app. */
/* Pending-clipboard state machine.
 *
 * SetFrontProcess is asynchronous: it schedules a front-switch that actually
 * happens when BridgeAgent next yields through the Event Manager AND the
 * outgoing app also yields. PutScrap run inline right after SetFrontProcess
 * executes while BridgeAgent is still background from Scrap Manager's view —
 * the write lands in the desk scrap but target apps won't sync it because no
 * activate/deactivate cycle has occurred yet.
 *
 * So we split into two phases:
 *   Phase 1 (do_set_clipboard): read file into handle, request SetFrontProcess,
 *     stash pending state, return noErr immediately.
 *   Phase 2 (commit_pending_clip, called each tick from main loop):
 *     GetFrontProcess == self? If yes, we've actually reached foreground.
 *     Now ZeroScrap + PutScrap; target apps that saw our activate event will
 *     refresh their private TE scrap on next activate cycle.
 *
 * Switch-back is intentionally NOT done — leaves BridgeAgent frontmost so the
 * user can verify the clip landed. We can add swap-back once transfer works. */
static Boolean gClipPending = false;
static Handle  gClipData    = NULL;  /* HLock'd while pending */
static long    gClipLen     = 0;

static void discard_pending_clip(void)
{
    if (gClipData) {
        HUnlock(gClipData);
        DisposeHandle(gClipData);
        gClipData = NULL;
    }
    gClipLen = 0;
    gClipPending = false;
}

/* Called each main-loop tick. If there's a pending SET_CLIPBOARD and
 * BridgeAgent is genuinely the front process now, commit the PutScrap. */
static void commit_pending_clip(void)
{
    if (!gClipPending || !gClipData) return;

    ProcessSerialNumber front, self = {0, kCurrentProcess};
    if (GetFrontProcess(&front) != noErr) return;
    Boolean same = false;
    if (SameProcess(&front, &self, &same) != noErr || !same) {
        return; /* not front yet — try again next tick */
    }

    char dbg[96];
    long size_before  = *(volatile long  *)0x0960;
    short count_before = *(volatile short *)0x0968;

    long zret    = ZeroScrap();
    long put_err = PutScrap(gClipLen, 'TEXT', *gClipData);

    long size_after  = *(volatile long  *)0x0960;
    short count_after = *(volatile short *)0x0968;

    snprintf(dbg, sizeof(dbg),
             "commit_clip: len=%ld z=%ld p=%ld sz %ld->%ld cnt %d->%d",
             gClipLen, zret, put_err,
             size_before, size_after, (int)count_before, (int)count_after);
    bridge_step(dbg);

    discard_pending_clip();
}

/* Load Host:_bridge_clipboard into a locked handle, stash as pending, and
 * ask Process Manager to bring BridgeAgent to front. Actual PutScrap happens
 * later in commit_pending_clip once GetFrontProcess confirms we're foreground.
 *
 * prior_front is accepted for signature parity but is unused for now (no
 * switch-back yet). */
static OSErr do_set_clipboard(const ProcessSerialNumber *prior_front)
{
    (void)prior_front;
    char dbg[96];
    FSSpec spec;
    Str255 cb_path; br_cfg_path(cb_path, BR_FILE_CLIPBOARD);
    OSErr err = FSMakeFSSpec(0, 0, cb_path, &spec);
    if (err != noErr) {
        snprintf(dbg, sizeof(dbg), "set_clip: FSMakeFSSpec err=%d", (int)err);
        bridge_step(dbg);
        return err;
    }

    short ref;
    err = FSpOpenDF(&spec, fsRdPerm, &ref);
    if (err != noErr) {
        snprintf(dbg, sizeof(dbg), "set_clip: FSpOpenDF err=%d", (int)err);
        bridge_step(dbg);
        return err;
    }

    long size = 0;
    err = GetEOF(ref, &size);
    if (err != noErr || size < 0) {
        FSClose(ref);
        snprintf(dbg, sizeof(dbg), "set_clip: GetEOF err=%d size=%ld", (int)err, size);
        bridge_step(dbg);
        return (err != noErr) ? err : paramErr;
    }

    /* Drop any previous pending clip before we load the new one. */
    discard_pending_clip();

    if (size == 0) {
        FSClose(ref);
        /* Empty: just request foreground, commit will ZeroScrap with zero len. */
        gClipData = NewHandle(0);
        if (!gClipData) {
            bridge_step("set_clip: NewHandle(0) failed");
            return memFullErr;
        }
        HLock(gClipData);
        gClipLen = 0;
        gClipPending = true;
    } else {
        Handle h = NewHandle(size);
        if (!h) {
            FSClose(ref);
            bridge_step("set_clip: NewHandle failed");
            return memFullErr;
        }
        HLock(h);
        long rlen = size;
        err = FSRead(ref, &rlen, *h);
        FSClose(ref);
        if (err != noErr && err != eofErr) {
            HUnlock(h);
            DisposeHandle(h);
            snprintf(dbg, sizeof(dbg), "set_clip: FSRead err=%d", (int)err);
            bridge_step(dbg);
            return err;
        }
        gClipData = h;
        gClipLen  = rlen;
        gClipPending = true;
    }

    FSpDelete(&spec); /* file is single-shot; bytes are in the pending handle */

    /* Ask Process Manager for the front slot. If we're already front this is
     * a no-op and commit_pending_clip will fire on the very next tick. */
    ProcessSerialNumber self = {0, kCurrentProcess};
    OSErr sfp_err = SetFrontProcess(&self);

    snprintf(dbg, sizeof(dbg), "set_clip: pending len=%ld sfp=%d",
             gClipLen, (int)sfp_err);
    bridge_step(dbg);

    return noErr;
}

/* ---------- RESIZE ----------
 *
 * Host writes "RESIZE <w> <h> <appleID>" into _bridge_cmd. We hand the
 * appleID to Display Manager's DMSetDisplayMode — the canonical
 * resolution-switching API on System 7.5+ and Mac OS 9.
 *
 * Why DM not raw cscSwitchMode: cscSwitchMode flips the driver's
 * internal state but does NOT update the GDevice/PixMap fields apps
 * read for layout (gdRect, gdPMap.bounds, gdPMap.rowBytes). DM is the
 * blessed wrapper that calls cscSwitchMode + cscGetVideoParameters,
 * patches the GDevice, and broadcasts a display-changed notification
 * so apps redraw. Without those steps, apps keep drawing with stale
 * rowBytes into the new framebuffer — the classic diagonal smear.
 *
 * Depth: PPC publishes only APPLE_32_BIT, so we always pass kDepthMode6
 * (0x85). w/h params are unused — DM derives the new geometry from the
 * driver's response to its internal cscGetVideoParameters call.
 */
static OSErr do_resize(int width, int height, unsigned long appleID)
{
    (void)width; (void)height;

    GDHandle gd = GetMainDevice();
    if (gd == NULL) return paramErr;

    unsigned long depthMode = 0x85;  /* kDepthMode6 = 32-bit */
    return DMSetDisplayMode(gd, (DisplayIDType)appleID,
                            &depthMode, 0, NULL);
}

static OSErr do_quit_target(const ProcessSerialNumber *psn)
{
    AEAddressDesc target;
    OSErr err = AECreateDesc(typeProcessSerialNumber, psn, sizeof(*psn), &target);
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

/* Quit every running app (skipping ourselves) by sending kAEQuitApplication.
 * Used as a best-effort "ask apps to clean up" pass before the Shutdown
 * Manager kills everything. Returns once we've sent — no waiting; apps
 * that ignore the AE just get terminated when ShutDwnPower/Start runs. */
static void quit_all_other_apps(void)
{
    ProcessSerialNumber self;
    GetCurrentProcess(&self);

    ProcessSerialNumber iter = {0, kNoProcess};
    while (GetNextProcess(&iter) == noErr) {
        if (iter.highLongOfPSN == self.highLongOfPSN &&
            iter.lowLongOfPSN  == self.lowLongOfPSN) continue;

        AEAddressDesc target;
        if (AECreateDesc(typeProcessSerialNumber, &iter,
                         sizeof(iter), &target) != noErr) continue;
        AppleEvent evt;
        if (AECreateAppleEvent(kCoreEventClass, kAEQuitApplication,
                               &target, kAutoGenerateReturnID,
                               kAnyTransactionID, &evt) == noErr) {
            AESend(&evt, NULL, kAENoReply | kAEAlwaysInteract,
                   kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
            AEDisposeDesc(&evt);
        }
        AEDisposeDesc(&target);
    }
}

/* SHUTDOWN/RESTART: bypass Finder entirely. kAEShutDown / kAERestart are
 * events the Finder *sends*, not receives — there's no AppleEvent path to
 * trigger system shutdown. Call the Shutdown Manager directly, after a
 * best-effort pass to quit other apps so they can flush state. The
 * Shutdown Manager itself runs registered shutdown procs and closes
 * drivers; the emulator should observe the shutdown via its usual hooks. */
static OSErr do_system_event(AEEventID eventID)
{
    quit_all_other_apps();

    /* Brief yield so quit AEs get a chance to run. */
    unsigned long ticks;
    Delay(60, &ticks);  /* ~1s */

    if (eventID == kAERestart)        ShutDwnStart();
    else if (eventID == kAEShutDown)  ShutDwnPower();
    else                              return paramErr;

    /* ShutDwnStart/Power don't return on success. If we're still here,
     * something blocked the shutdown — surface as a generic error. */
    return -1;
}

static void write_result(OSErr result)
{
    FSSpec spec;
    short ref;
    char buf[16];
    long len;

    Str255 res_path; br_cfg_path(res_path, BR_FILE_RESULT);
    FSMakeFSSpec(0, 0, res_path, &spec);
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
    Str255 loaded_path; br_cfg_path(loaded_path, BR_FILE_LOADED);
    if (FSMakeFSSpec(0, 0, loaded_path, &mk) == fnfErr)
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
    Str255 hb_path; br_cfg_path(hb_path, BR_FILE_HEARTBEAT);
    if (FSMakeFSSpec(0, 0, hb_path, &hb) == fnfErr)
        FSpCreate(&hb, 'ttxt', 'TEXT', smSystemScript);
    if (FSpOpenDF(&hb, fsWrPerm, &ref) == noErr) {
        /* JSON payload so the host can tail this file for live status. */
        char last_cmd[96] = {0};
        int clen = gLastCmd[0];
        if (clen > 80) clen = 80;
        memcpy(last_cmd, gLastCmd + 1, clen);
        /* Minimal JSON-escape: drop backslashes and quotes from last_cmd. */
        for (int i = 0; i < clen; i++)
            if (last_cmd[i] == '"' || last_cmd[i] == '\\') last_cmd[i] = '_';

        char buf[384];
        long len = snprintf(buf, sizeof(buf),
            "{\"build\":\"%s\","
            "\"heartbeat\":%d,\"commands\":%d,"
            "\"last_result\":%d,\"last_cmd\":\"%s\"}\r",
            BRIDGE_AGENT_BUILD,
            gHeartbeat, gCmdCount, (int)gLastResult, last_cmd);
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
    Str255 step_path; br_cfg_path(step_path, BR_FILE_STEP);
    if (FSMakeFSSpec(0, 0, step_path, &sp) == fnfErr)
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
    Str255 cmd_path; br_cfg_path(cmd_path, BR_FILE_CMD);
    if (FSMakeFSSpec(0, 0, cmd_path, &cmd_spec) != noErr)
        return;

    short refNum;
    if (FSpOpenDF(&cmd_spec, fsRdPerm, &refNum) != noErr)
        return;

    char cmd[640];                       /* COPY carries two full paths */
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

    /* Capture the frontmost process so QUIT can target it. We do not
     * self-foreground: our bridge commands (LaunchApplication, AESend,
     * ShutDwnPower/Start) work from any scheduling context, and forcing a
     * front-process switch dirties CurApName in a way that's hard to
     * reliably restore. `canBackground` in the SIZE resource is what
     * lets us poll responsively from the background. */
    ProcessSerialNumber prior_front;
    GetFrontProcess(&prior_front);

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
    } else if (strncmp(cmd, "COPY ", 5) == 0) {
        result = do_copy(cmd + 5);
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
    } else if (strncmp(cmd, "SCRIPT ", 7) == 0 ||
               strncmp(cmd, "EXEC ",   5) == 0) {
        /* "SCRIPT <4-char creator>" — fire-and-forget 'misc'/'dosc'.
         * "EXEC   <4-char creator>" — same, but waits for the AE reply
         * and captures stdout/stderr/{Status} into _bridge_reply.
         * Body lives in BR_FILE_SCRIPT for both.
         * Examples: SCRIPT McPL (MacPerl), SCRIPT LAND (Frontier),
         * SCRIPT MPS  (MPW Shell — trailing space!), EXEC MPSX (ToolServer
         * — the canonical target for /api/exec). */
        Boolean exec_mode = (cmd[0] == 'E');
        const char *cp = cmd + (exec_mode ? 5 : 7);
        if ((int)strlen(cp) < 4) {
            result = paramErr;
        } else {
            OSType creator = ((OSType)(unsigned char)cp[0] << 24)
                           | ((OSType)(unsigned char)cp[1] << 16)
                           | ((OSType)(unsigned char)cp[2] << 8)
                           |  (OSType)(unsigned char)cp[3];
            result = exec_mode ? do_exec(creator) : do_script(creator);
        }
    } else if (strncmp(cmd, "RESIZE ", 7) == 0) {
        /* "RESIZE <width> <height> <appleID>" — host snaps to a known
         * mode from kVideoModes and passes the matching APPLE_* ID so
         * we don't have to keep a parallel table on the guest side. */
        int w = 0, h = 0;
        unsigned long ide = 0;
        if (sscanf(cmd + 7, "%d %d %lu", &w, &h, &ide) == 3) {
            result = do_resize(w, h, ide);
        } else {
            result = paramErr;
        }
    } else if (strncmp(cmd, "SHUTDOWN", 8) == 0) {
        result = do_system_event(kAEShutDown);
    } else if (strncmp(cmd, "RESTART", 7) == 0) {
        result = do_system_event(kAERestart);
    } else if (strncmp(cmd, "EXIT", 4) == 0) {
        /* The agent itself quits: out of the way while Apple's Installer
         * installs a System (it dies there otherwise: PLAN.md, P2 status).
         * It comes back with the next boot. */
        gRunning = false;
    } else if (strncmp(cmd, "QUIT", 4) == 0) {
        /* The front process can be the agent itself (it comes forward
         * to handle commands) or the Finder once an app has gone: quitting
         * either is never what "quit the front app" means. */
        ProcessSerialNumber self;
        ProcessInfoRec info;
        Str31 front_name;
        Boolean is_self = false;
        GetCurrentProcess(&self);
        SameProcess(&prior_front, &self, &is_self);
        info.processInfoLength = sizeof(info);
        info.processName = front_name;
        info.processAppSpec = NULL;
        if (is_self || (GetProcessInformation(&prior_front, &info) == noErr &&
                        info.processSignature == 'MACS'))
            result = procNotFound;
        else
            result = do_quit_target(&prior_front);
    } else if (strncmp(cmd, "SET_CLIPBOARD", 13) == 0) {
        result = do_set_clipboard(&prior_front);
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

/* ---------- Bridge v2: the bridge trap ($ABE5, guest_bridge.h) ---------
 *
 * Requests come from the emulator through the trap instead of files: a
 * Time Manager task asks it every 50 ms whether one waits (AGENT_PENDING) and
 * wakes us (WakeUpProcess); the main loop takes it (NEXT) and answers
 * (REPLY). The task runs at interrupt time on whatever A5: it touches only
 * its own record, and is removed before we quit. Commands not moved yet
 * still come as files (poll_bridge). */

enum { kBridgeTrap = 0xABE5, kBridgeMagic = 0x4D506878, kBridgeProtocol = 2,
       kSelAgentHello = 0x10, kSelNext = 0x11, kSelReply = 0x12, kSelAgentPending = 0x13 };

static long bridge_call(long selector, const void *ptr, long value)
{
    register long d0 __asm__("d0") = selector;
    register const void *a0 __asm__("a0") = ptr;
    register long d1 __asm__("d1") = value;
    __asm__ volatile(".short 0xABE5"
                     : "+d"(d0), "+a"(a0), "+d"(d1)
                     :
                     : "d2", "a1", "memory", "cc");
    return d0;
}

typedef struct {
    TMTask tm;
    ProcessSerialNumber psn;
} AgentTask;

static AgentTask gTask;
static Boolean gV2;

/* The Time Manager calls agent_tm_entry with the task in A1. */
void agent_tick(AgentTask *t);
void agent_tick(AgentTask *t)
{
    if (bridge_call(kSelAgentPending, NULL, 0) == 1)
        WakeUpProcess(&t->psn);
    PrimeTime((QElemPtr)&t->tm, 50);
}

extern void agent_tm_entry(void);
__asm__(
    "    .text\n"
    "    .globl agent_tm_entry\n"
    "agent_tm_entry:\n"
    "    movem.l %d0-%d2/%a0-%a1,-(%sp)\n"
    "    move.l %a1,-(%sp)\n"
    "    jsr agent_tick\n"
    "    addq.l #4,%sp\n"
    "    movem.l (%sp)+,%d0-%d2/%a0-%a1\n"
    "    rts\n");

static void bridge_v2_init(void)
{
    if (GetToolboxTrapAddress(kBridgeTrap) == GetToolboxTrapAddress(_Unimplemented))
        return;                                 /* an emulator without it: files only */
    if (bridge_call(kSelAgentHello, NULL, kBridgeProtocol) != kBridgeMagic)
        return;
    GetCurrentProcess(&gTask.psn);
    gTask.tm.tmAddr = (TimerUPP)agent_tm_entry;
    InsTime((QElemPtr)&gTask.tm);
    PrimeTime((QElemPtr)&gTask.tm, 50);
    gV2 = true;
}

static void bridge_v2_stop(void)
{
    if (gV2)
        RmvTime((QElemPtr)&gTask.tm);
    gV2 = false;
}

static void v2_reply(const char *data, long len)
{
    bridge_call(kSelReply, data, len);
}

static void v2_reply_oserr(OSErr err)
{
    char b[32];
    long n = snprintf(b, sizeof(b), "OSERR %d", (int)err);
    v2_reply(b, n);
}

/* "EXEC <creator> <seconds>\r<script>": exec_core, its reply as the
 * STDOUT/STDERR/STATUS text the file transport uses. */
static void v2_exec(const char *req, long n)
{
    const char *cr = memchr(req, '\r', n);
    if (!cr || n < 10) { v2_reply_oserr(paramErr); return; }
    OSType creator;
    memcpy(&creator, req + 5, 4);
    long seconds = strtol(req + 10, NULL, 10);
    if (seconds <= 0) seconds = 60;
    const char *src = cr + 1;
    long src_len = n - (src - req);

    char *out, *errs;
    long out_len, errs_len, status;
    OSErr err = exec_core(creator, src, src_len, seconds * 60, &out, &out_len, &errs, &errs_len, &status);
    if (err != noErr) { v2_reply_oserr(err); return; }

    char hdr[3][40];
    long h0 = snprintf(hdr[0], sizeof hdr[0], "STDOUT %ld\r", out_len);
    long h1 = snprintf(hdr[1], sizeof hdr[1], "STDERR %ld\r", errs_len);
    long h2 = snprintf(hdr[2], sizeof hdr[2], "STATUS %ld\r", status);
    long total = h0 + out_len + h1 + errs_len + h2;
    char *blob = NewPtr(total);
    if (!blob) { v2_reply_oserr(memFullErr); }
    else {
        char *p = blob;
        memcpy(p, hdr[0], h0); p += h0;
        if (out_len) { memcpy(p, out, out_len); p += out_len; }
        memcpy(p, hdr[1], h1); p += h1;
        if (errs_len) { memcpy(p, errs, errs_len); p += errs_len; }
        memcpy(p, hdr[2], h2);
        v2_reply(blob, total);
        DisposePtr(blob);
    }
    if (out) DisposePtr(out);
    if (errs) DisposePtr(errs);
}

/* A request waiting? Take it and answer it. */
static void poll_v2(void)
{
    static char small[256];
    unsigned long n = (unsigned long)bridge_call(kSelNext, small, sizeof small);
    if (n == 0xFFFFFFFFUL)
        return;
    char *req = small;
    if (n & 0x80000000UL) {                     /* bigger than small: room for it */
        n &= 0x7FFFFFFFUL;
        req = NewPtr(n);
        if (!req) {
            bridge_call(kSelNext, small, 0);    /* can't: answer so the host stops waiting */
            v2_reply_oserr(memFullErr);
            return;
        }
        n = (unsigned long)bridge_call(kSelNext, req, n);
    }
    gCmdCount++;
    if (n >= 5 && memcmp(req, "EXEC ", 5) == 0) {
        gLastCmd[0] = 7; memcpy(gLastCmd + 1, "EXEC v2", 7);
        v2_exec(req, (long)n);
    } else {
        v2_reply_oserr(-1);                     /* not a verb we know */
    }
    if (req != small) DisposePtr(req);
    draw_status();
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
    /* Read :System Folder:Preferences:MacPhoenix.cfg first — every
     * Host:MacPhoenix path we build below depends on the bridge dir
     * resolved from cfg (or the legacy "Host:MacPhoenix" fallback). */
    br_cfg_load();
    build_menus();
    install_ae_handlers();
    load_network_config();   /* Read netcfg.txt from the resolved bridge dir */
    open_status_window();
    bridge_v2_init();
    draw_status();

    /* Creating a visible window during startup yanked us to front. Hand
     * focus back to Finder so we run as a true background agent; bridge
     * commands will self-foreground explicitly when they arrive. Give
     * Finder a tick to register in the process list before we query. */
    {
        unsigned long ticks;
        Delay(6, &ticks);
        ProcessSerialNumber finder_psn;
        if (psn_for_creator('MACS', &finder_psn) == noErr) {
            SetFrontProcess(&finder_psn);
        }
    }

    EventRecord evt;
    while (gRunning) {
        if (WaitNextEvent(everyEvent, &evt, 6, NULL))
            handle_event(&evt);
        if (gV2) poll_v2();
        poll_bridge();
        commit_pending_clip();
    }

    bridge_v2_stop();
    return 0;
}
