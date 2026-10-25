/*
 * BridgeAgent - MacPhoenix automation agent
 *
 * A small application the emulator launches from its bridge dir on the
 * Host volume at desktop time (bridge_launcher_install; or Startup Items,
 * where an older install put it); it takes the emulator's requests through
 * the bridge trap $ABE5 (guest_bridge.h, docs/CommandBridge.md):
 *   EXEC <creator> <seconds>\r<script>  — 'misc'/'dosc', waits for the reply:
 *                                        stdout, stderr and {Status}
 *   CMD <len>\r<line><payload>          — one of:
 *       LAUNCH  <hfs-path>             — LaunchApplication
 *       OPEN    <hfs-path>             — generic 'aevt'/'odoc' to creator's app
 *       SCRIPT  <4-char creator>       — 'misc'/'dosc' with the payload as the
 *                                        body, fire-and-forget. Works for
 *                                        McPL/LAND/MPS /ToyS/...
 *       COPY    <src>\r<dst folder>    — copy a file or folder (both forks,
 *                                        Finder info) into a folder
 *       RESIZE / QUIT / SHUTDOWN / RESTART / EXIT
 *       SET_CLIPBOARD                  — the payload; see do_set_clipboard
 * and tells it the front process (STATE). Its bridge dir (bridge_cfg.h)
 * still carries netcfg.txt.
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
 * window. */
#define BRIDGE_AGENT_BUILD __DATE__ " " __TIME__

/* ---------- Status window state ---------- */

static WindowPtr gStatusWin = NULL;
static Boolean   gV2;          /* the bridge trap answered our hello */
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
    snprintf(buf, sizeof(buf), "Bridge: %s", gV2 ? "connected" : "gone");
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

static OSErr open_in_finder(const FSSpec *spec);

static OSErr do_launch(const unsigned char *path)
{
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, path, &spec);
    if (err != noErr) return err;

    /* LaunchApplication runs applications only. A desk accessory, a control
     * panel or a folder opens as on a double-click: the Finder is asked. */
    FInfo fndrInfo;
    if (FSpGetFInfo(&spec, &fndrInfo) != noErr
        || (fndrInfo.fdType != 'APPL' && fndrInfo.fdType != 'APPC'
            && fndrInfo.fdType != 'APPD' && fndrInfo.fdType != 'appe'))
        return open_in_finder(&spec);

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

static Boolean gCopyReplace;       /* COPY ... REPLACE: merge folders, replace files */

static OSErr copy_item(const FSSpec *src, short dstVRef, long dstDirID, Ptr buf)
{
    CInfoPBRec pb;
    Str63 name;
    OSErr err = get_cat_info(src, &pb, name);
    if (err != noErr) return err;

    Boolean is_dir = (pb.hFileInfo.ioFlAttrib & ioDirMask) != 0;
    Boolean merge = false;
    long newDirID = 0;
    FSSpec dst;
    err = FSMakeFSSpec(dstVRef, dstDirID, src->name, &dst);
    if (err == noErr) {
        if (!gCopyReplace) return dupFNErr;
        CInfoPBRec dpb;
        Str63 dname;
        if ((err = get_cat_info(&dst, &dpb, dname)) != noErr) return err;
        Boolean dst_dir = (dpb.hFileInfo.ioFlAttrib & ioDirMask) != 0;
        if (is_dir && dst_dir) {            /* into the folder that's there */
            merge = true;
            newDirID = dpb.dirInfo.ioDrDirID;
        } else if (!is_dir && !dst_dir) {   /* the file replaced */
            if ((err = FSpDelete(&dst)) != noErr) return err;
        } else {
            return dupFNErr;                /* a file where a folder is, or the reverse */
        }
    } else if (err != fnfErr) {
        return err;
    }

    if (is_dir) {
        if (!merge && (err = FSpDirCreate(&dst, smSystemScript, &newDirID)) != noErr)
            return err;
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
        if (merge) return noErr;            /* the folder keeps its own info */
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

/* "COPY src\rdst[\rREPLACE]": src a file or folder, dst an existing folder.
 * REPLACE: into a folder of the same name that's there, files replaced (an
 * update's "INTO ..." folders). */
static OSErr do_copy(const char *args)
{
    const char *sep = strchr(args, '\r');
    if (!sep) return paramErr;
    unsigned char srcPath[256], dstPath[256];
    const char *opt = strchr(sep + 1, '\r');
    gCopyReplace = opt && strncmp(opt + 1, "REPLACE", 7) == 0;
    int sl = (int)(sep - args), dl = opt ? (int)(opt - sep - 1) : (int)strlen(sep + 1);
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

/* The Finder's 'odoc' for an item opens it as a double-click does. */
static OSErr open_in_finder(const FSSpec *spec)
{
    ProcessSerialNumber finderPSN;
    OSErr err = psn_for_creator('MACS', &finderPSN);
    if (err != noErr) return err;
    SetFrontProcess(&finderPSN);
    AEAddressDesc target;
    err = AECreateDesc(typeProcessSerialNumber, &finderPSN, sizeof(finderPSN), &target);
    if (err != noErr) return err;
    AppleEvent evt;
    err = make_odoc(&target, spec, &evt);
    AEDisposeDesc(&target);
    if (err != noErr) return err;
    err = AESend(&evt, NULL, kAENoReply | kAEAlwaysInteract,
                 kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
    AEDisposeDesc(&evt);
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

/* A command's payload (SCRIPT's body, SET_CLIPBOARD's bytes), as v2_cmd
 * took it from the request. */
static Boolean gHavePayload;
static char   *gPayload;
static long    gPayloadLen;

static void drop_payload(void)
{
    if (gPayload) DisposePtr(gPayload);
    gPayload = NULL;
    gPayloadLen = 0;
    gHavePayload = false;
}

/* The payload, ours to free; *outBuf is NULL when empty (a no-op script). */
static OSErr load_bridge_script(char **outBuf, long *outLen)
{
    *outBuf = gPayload;
    *outLen = gPayloadLen;
    gPayload = NULL;
    gPayloadLen = 0;
    gHavePayload = false;
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
 *   Phase 1 (do_set_clipboard): payload into a handle, request SetFrontProcess,
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

    ZeroScrap();
    PutScrap(gClipLen, 'TEXT', *gClipData);
    discard_pending_clip();
}

/* Take the payload into a locked handle, stash it as pending, and ask
 * Process Manager to bring BridgeAgent to front. Actual PutScrap happens
 * later in commit_pending_clip once GetFrontProcess confirms we're foreground.
 * If we're already front, it fires on the very next tick.
 *
 * prior_front is accepted for signature parity but is unused for now (no
 * switch-back yet). */
static OSErr do_set_clipboard(const ProcessSerialNumber *prior_front)
{
    (void)prior_front;
    discard_pending_clip();
    Handle h = NewHandle(gPayloadLen);
    if (!h) { drop_payload(); return memFullErr; }
    HLock(h);
    if (gPayloadLen) BlockMoveData(gPayload, *h, gPayloadLen);
    gClipData = h;
    gClipLen = gPayloadLen;
    gClipPending = true;
    drop_payload();
    ProcessSerialNumber self = {0, kCurrentProcess};
    SetFrontProcess(&self);
    return noErr;
}

/* ---------- RESIZE ----------
 *
 * Host sends "RESIZE <w> <h> <appleID>" (a CMD). We hand the
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

/* Agents an older install left in Startup Items (they poll files that no
 * longer come, and the oldest patch _WaitNextEvent): asked to quit. Ours
 * has the bridge, so any other one is older. */
static void quit_other_agents(void)
{
    ProcessSerialNumber psn = {0, kNoProcess}, self;
    GetCurrentProcess(&self);
    while (GetNextProcess(&psn) == noErr) {
        ProcessInfoRec info;
        Boolean same = false;
        info.processInfoLength = sizeof(info);
        info.processName = NULL;
        info.processAppSpec = NULL;
        if (GetProcessInformation(&psn, &info) != noErr || info.processSignature != 'MxBr')
            continue;
        if (SameProcess(&psn, &self, &same) == noErr && !same)
            do_quit_target(&psn);
    }
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

/* One command line from a CMD request; its payload (SCRIPT,
 * SET_CLIPBOARD) is in gPayload. */
static OSErr run_command(char *cmd)
{
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
    } else if (strncmp(cmd, "SCRIPT ", 7) == 0) {
        /* "SCRIPT <4-char creator>" — fire-and-forget 'misc'/'dosc', the
         * payload as the body. Examples: SCRIPT McPL (MacPerl), SCRIPT LAND
         * (Frontier), SCRIPT MPS  (MPW Shell — trailing space!). EXEC (a
         * request of its own, v2_exec) waits for the reply. */
        const char *cp = cmd + 7;
        if ((int)strlen(cp) < 4) {
            result = paramErr;
        } else {
            OSType creator = ((OSType)(unsigned char)cp[0] << 24)
                           | ((OSType)(unsigned char)cp[1] << 16)
                           | ((OSType)(unsigned char)cp[2] << 8)
                           |  (OSType)(unsigned char)cp[3];
            result = do_script(creator);
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
    return result;
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
 * its own record, and is removed before we quit. */

enum { kBridgeTrap = 0xABE5, kBridgeMagic = 0x4D506878, kBridgeProtocol = 2,
       kSelAgentHello = 0x10, kSelNext = 0x11, kSelReply = 0x12, kSelAgentPending = 0x13,
       kSelState = 0x14 };

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

/* False if there's no bridge for us: no trap (not our emulator), or
 * another agent has it (one launched from the bridge dir and one in
 * Startup Items). */
static Boolean bridge_v2_init(void)
{
    if (GetToolboxTrapAddress(kBridgeTrap) == GetToolboxTrapAddress(_Unimplemented))
        return false;
    if (bridge_call(kSelAgentHello, NULL, kBridgeProtocol) != kBridgeMagic)
        return false;
    GetCurrentProcess(&gTask.psn);
    gTask.tm.tmAddr = (TimerUPP)agent_tm_entry;
    InsTime((QElemPtr)&gTask.tm);
    PrimeTime((QElemPtr)&gTask.tm, 50);
    gV2 = true;
    return true;
}

static void bridge_v2_stop(void)
{
    if (gV2) {
        RmvTime((QElemPtr)&gTask.tm);
        bridge_call(kSelAgentHello, NULL, 0);   /* gone: the host stops relying on us */
    }
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

/* "EXEC <creator> <seconds>\r<script>": exec_core, answered with the
 * reply as text, each section after a CR-terminated header:
 * "STDOUT <n>\r<n bytes>STDERR <n>\r<n bytes>STATUS <status>\r". */
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

/* The front process's name to the emulator (STATE), on every pass of the
 * loop: the host's /api/app and app= waits use it while it is recent (a
 * peek at CurApName sees whichever process happens to run, us included). */
static void report_front(void)
{
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    Str31 name;
    if (GetFrontProcess(&psn) != noErr)
        return;
    info.processInfoLength = sizeof(info);
    info.processName = name;
    info.processAppSpec = NULL;
    if (GetProcessInformation(&psn, &info) != noErr)
        return;
    bridge_call(kSelState, name + 1, name[0]);
}

/* "CMD <length>\r<command line><payload>": a command (its line has
 * <length> bytes; COPY's holds a CR) and its payload, run by run_command;
 * answered "OSERR <result>". SHUTDOWN and
 * RESTART don't come back to answer; the host counts them done once taken. */
static void v2_cmd(const char *req, long n)
{
    const char *cr = memchr(req, '\r', n);
    if (!cr) { v2_reply_oserr(paramErr); return; }
    long len = strtol(req + 4, NULL, 10);
    const char *line = cr + 1;
    long rest = n - (line - req);
    if (len < 0 || len > rest || len > 639) { v2_reply_oserr(paramErr); return; }
    char cmd[640];
    memcpy(cmd, line, len);
    cmd[len] = '\0';

    long plen = rest - len;
    drop_payload();
    gPayload = plen ? NewPtr(plen) : NULL;
    if (plen && !gPayload) { v2_reply_oserr(memFullErr); return; }
    if (plen) BlockMoveData(line + len, gPayload, plen);
    gPayloadLen = plen;
    gHavePayload = true;

    OSErr result = run_command(cmd);
    drop_payload();                     /* if the command didn't take it */
    gLastResult = result;
    v2_reply_oserr(result);
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
    } else if (n >= 4 && memcmp(req, "CMD ", 4) == 0) {
        v2_cmd(req, (long)n);
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
    /* No bridge for us (another agent has it, or no trap): go. */
    if (!bridge_v2_init())
        return 0;
    install_ae_handlers();
    /* The bridge dir (the trap says; else MacPhoenix.cfg in Preferences):
     * netcfg.txt is there. */
    br_cfg_load();
    build_menus();
    load_network_config();   /* Read netcfg.txt from the resolved bridge dir */
    open_status_window();
    draw_status();

    /* Out of the way: launched in the background (the emulator's launcher),
     * we leave the front as it is; started in front (Startup Items), the
     * Finder gets it back. Bridge commands self-foreground when they need
     * to. Give the Finder a tick to register in the process list. */
    {
        ProcessSerialNumber front, self, finder_psn;
        Boolean is_self = false;
        unsigned long ticks;
        Delay(6, &ticks);
        GetCurrentProcess(&self);
        if (GetFrontProcess(&front) == noErr && SameProcess(&front, &self, &is_self) == noErr
            && is_self && psn_for_creator('MACS', &finder_psn) == noErr)
            SetFrontProcess(&finder_psn);
    }

    EventRecord evt;
    while (gRunning) {
        if (WaitNextEvent(everyEvent, &evt, 6, NULL))
            handle_event(&evt);
        if (gV2) { report_front(); poll_v2(); }
        {                                   /* older agents start from Startup Items */
            static unsigned long last;      /* after us, as the Finder gets to them */
            if (TickCount() - last > 300) { last = TickCount(); quit_other_agents(); }
        }
        commit_pending_clip();
    }

    bridge_v2_stop();
    return 0;
}
