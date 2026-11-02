/*
 * web_main.cpp - Executor in a browser tab (docs/executor/DIRECTION.md, W2).
 *
 * The module runs in a Worker (web/executor/worker.js). main() starts
 * Executor through the same executor_host contract mac-phoenix uses, on
 * this thread: there is no other. Executor's idle waits and a tick about
 * once a frame yield to the browser (rsys/hostidle.h); on the way back the
 * yield hook takes the input the page queued (Module.input) and renders
 * what changed, which arrives in on_rects and goes to Module.onRects: the
 * changed rectangles only, as RGBA, for the page's canvas. The cursor is
 * never in them: the page draws it (Module.onCursor, a CSS cursor).
 *
 * Automation (W3): the page's requests (Module.requests: [id, op, arg]) are
 * answered at the next event call of the running application
 * (executor_host::set_event_poll_hook), a safe point in a process's world:
 *
 *   app              the current application's name (CurApName)
 *   windows          its windows: title, global bounds, visible, kind; a
 *                    dialog's static text and ParamText's strings too (an
 *                    alert's message)
 *   menus            its menu bar: each menu's title and left edge
 *   launch PATH      LaunchApplication of "Volume:Folder:App"; its OSErr
 *   mount PATH       a disk image put in now (the Disks panel's insert, the
 *                    image in the module's filesystem already): mounted
 *                    through _MountVol (the System's patches tell the Finder),
 *                    a diskEvt posted, as for a floppy; the OSErr (-1: the
 *                    image can't be opened; nsvErr: no Mac volume on it)
 *   eject PATH       a disk image's volumes unmounted through _UnmountVol
 *                    (fBsyErr: in use)
 *   volumes          the volumes mounted: their names
 *
 * Disk images going away, ejected here or by the Finder (Put Away, or
 * anything else that unmounts them), are told to the page at the next event
 * call (Module.onEjected(path)): it takes them out of its Disks panel.
 *
 * The Worker answers 'read' and 'write' itself: files in the module's
 * filesystem, such as the Host volume (/host).
 */
#include "executor_host.h"
#include "phoenix.h"
#include "hfs_sideload.h"
#include <rsys/hostidle.h>

#include <base/common.h>
#include <DialogMgr.h>
#include <FileMgr.h>
#include <OSEvent.h>
#include <file/file.h>
#include <hfs/hfs.h>
#include <MemoryMgr.h>
#include <base/cpu.h>
#include <ProcessMgr.h>
#include <rsys/process.h>
#include <SegmentLdr.h>
#include <wind/wind.h>
#include <menu/menu.h>
#include <util/macstrings.h>

#include <emscripten.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

/* The page's input, oldest first: [kind, a, b] per event (kind 1: mouse to
 * a, b; 2: button down a; 3: key down a, Mac key code b). Returns how many
 * events it copied. */
EM_JS(int, js_take_input, (int32_t *out, int max), {
    const q = Module.input || [];
    const n = Math.min(q.length, max);
    for (let i = 0; i < n; i++) {
        const e = q[i];
        HEAP32[(out >> 2) + 3 * i] = e[0];
        HEAP32[(out >> 2) + 3 * i + 1] = e[1];
        HEAP32[(out >> 2) + 3 * i + 2] = e[2];
    }
    q.splice(0, n);
    return n;
});

EM_JS_DEPS(web_main, "$stringToUTF8,$lengthBytesUTF8,$UTF8ToString");

/* The oldest request: its id, and "op\0arg" in buf. 0: none. */
EM_JS(int, js_take_request, (char *buf, int max), {
    const q = Module.requests || [];
    if (!q.length)
        return 0;
    const [id, op, arg] = q.shift();
    const n = lengthBytesUTF8(op);
    stringToUTF8(op, buf, max);                          /* NUL-terminated */
    stringToUTF8(arg || '', buf + n + 1, max - n - 1);
    return id;
});

EM_JS(void, js_reply, (int id, const char *json), {
    if (Module.onReply)
        Module.onReply(id, JSON.parse(UTF8ToString(json)));
});

/* One 20 ms frame of 48 kHz 16-bit big-endian stereo. */
EM_JS(void, js_audio, (const uint8_t *s16be, int frames), {
    if (Module.onAudio)
        Module.onAudio(HEAPU8.subarray(s16be, s16be + frames * 4));
});

/* The screen's size, and n rectangles (top, left, bottom, right in rects)
 * whose RGBA pixels follow one another in pixels, row by row. */
EM_JS(void, js_rects, (int width, int height, const int32_t *rects, int n, const uint8_t *pixels, int bytes), {
    if (Module.onRects)
        Module.onRects(width, height, HEAP32.subarray(rects >> 2, (rects >> 2) + 4 * n),
                       HEAPU8.subarray(pixels, pixels + bytes));
});

/* The cursor, for the page to draw (a CSS cursor; web/executor/cursor.js). */
EM_JS(void, js_cursor, (const uint8_t *data, const uint8_t *mask, int hotX, int hotY, int visible), {
    if (Module.onCursor)
        Module.onCursor(HEAPU8.slice(data, data + 32), HEAPU8.slice(mask, mask + 32), hotX, hotY, !!visible);
});

/* The 1x frame as RGBA bytes, the answer to request id ('screen1x'). */
EM_JS(void, js_reply_pixels, (int id, int width, int height, const uint8_t *rgba), {
    if (Module.onReply)
        Module.onReply(id, { width, height, data: HEAPU8.slice(rgba, rgba + width * height * 4) });
});

/* A disk image whose volumes are all gone (ejected). */
EM_JS(void, js_ejected, (const char *path), {
    if (Module.onEjected)
        Module.onEjected(UTF8ToString(path));
});

/* Time the front end spent producing frames (rendering, converting). */
EM_JS(void, js_frame_time, (double ms), {
    if (Module.stats)
        Module.stats.hostMs += ms;
});

namespace {

std::vector<uint32_t> rgba;
std::vector<int32_t> rect_list;

PhoenixVideoDriver *driver()
{
    return dynamic_cast<PhoenixVideoDriver *>(Executor::vdriver.get());
}

/* What changed, as RGBA (0xAARRGGBB to R, G, B, A bytes: 0xAABBGGRR on
   little-endian), the cursor drawn into the rectangles it is in. */
void on_rects(const uint32_t *argb, int width, int height,
              const std::vector<PhoenixVideoDriver::Rect> &rects)
{
    size_t total = 0;
    for(const auto &r : rects)
        total += (size_t)(r.bottom - r.top) * (r.right - r.left);
    rgba.resize(total);
    rect_list.clear();
    uint32_t *out = rgba.data();
    for(const auto &r : rects)
    {
        rect_list.insert(rect_list.end(), { r.top, r.left, r.bottom, r.right });
        uint32_t *start = out;
        for(int y = r.top; y < r.bottom; y++)
        {
            const uint32_t *row = argb + (size_t)y * width;
            for(int x = r.left; x < r.right; x++)
            {
                uint32_t p = row[x];
                *out++ = (p & 0xFF00FF00) | (p >> 16 & 0xFF) | (p & 0xFF) << 16;
            }
        }
        if(auto *v = driver())
            v->drawCursor(start, r);
    }
    js_rects(width, height, rect_list.data(), (int)rects.size(),
             (const uint8_t *)rgba.data(), (int)(total * 4));
}

using namespace Executor;

/* A Mac string (MacRoman) as a JSON string. */
std::string json_string(const std::string &s)
{
    std::string out = "\"";
    for(char32_t c : toUnicode(mac_string_view((const unsigned char *)s.data(), s.size())))
    {
        if(c == '"' || c == '\\')
            out += '\\', out += (char)c;
        else if(c < 0x20 || c >= 0x7F)
        {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", (unsigned)c);    // MacRoman is all in the BMP
            out += b;
        }
        else
            out += (char)c;
    }
    return out + "\"";
}

std::string pstring(const unsigned char *p)
{
    return p ? std::string((const char *)p + 1, p[0]) : std::string();
}

std::string windows_json()
{
    std::string out = "[";
    for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
    {
        Rect r = (*WINDOW_STRUCT_REGION(w))->rgnBBox;
        StringHandle t = WINDOW_TITLE(w);
        if(out.size() > 1)
            out += ",";
        out += "{\"title\":" + json_string(t ? pstring(*t) : "")
               + ",\"rect\":[" + std::to_string(r.top) + "," + std::to_string(r.left) + ","
               + std::to_string(r.bottom) + "," + std::to_string(r.right) + "]"
               + ",\"visible\":" + (WINDOW_VISIBLE(w) ? "true" : "false")
               + ",\"kind\":" + std::to_string((int)WINDOW_KIND(w));
        if(WINDOW_KIND(w) == dialogKind)
        {
            std::string text;
            for(int i = 1, n = CountDITL((DialogPtr)w); i <= n && text.size() < 1000; i++)
            {
                GUEST<INTEGER> type;
                GUEST<Handle> h;
                C_GetDialogItem((DialogPtr)w, i, &type, &h, nullptr);
                if((type & 0x7F) != statText || !h)
                    continue;
                Str255 s;
                C_GetDialogItemText(h, s);
                text += (text.empty() ? "" : "\n") + pstring(s);
            }
            out += ",\"text\":" + json_string(text) + ",\"params\":[";
            for(int i = 0; i < 4; i++)             // ParamText's ^0..^3
            {
                Handle h = LM(DAStrings)[i];
                out += (i ? "," : "") + json_string(h && *h ? pstring((const unsigned char *)*h) : "");
            }
            out += "]";
        }
        out += "}";
    }
    return out + "]";
}

std::string menus_json()
{
    std::string out = "[";
    if(LM(MenuList))
    {
        muelem *mp = (*MENULIST)->mulist;
        muelem *end = mp + (*MENULIST)->muoff / sizeof(muelem);
        for(; mp != end; mp++)
        {
            MenuHandle mh = mp->muhandle;
            if(!mh)
                continue;
            if(out.size() > 1)
                out += ",";
            out += "{\"title\":" + json_string(pstring((const unsigned char *)MI_DATA(mh)))
                   + ",\"left\":" + std::to_string((int)mp->muleft) + "}";
        }
    }
    return out + "]";
}

/* The page's UTF-8 as the Mac's MacRoman (file names: "ƒ", "™", ...). */
mac_string macRoman(const std::string &utf8)
{
    std::u32string u;
    for(size_t i = 0; i < utf8.size();)
    {
        unsigned char c = utf8[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        char32_t cp = n == 1 ? c : c & (0x7F >> n);
        for(int k = 1; k < n && i + k < utf8.size(); k++)
            cp = cp << 6 | (utf8[i + k] & 0x3F);
        u.push_back(cp);
        i += n;
    }
    return toMacRoman(u);
}

OSErr launch(const std::string &utf8)
{
    mac_string path = macRoman(utf8);
    Str255 name;
    if(path.size() > 255)
        return bdNamErr;
    name[0] = path.size();
    memcpy(name + 1, path.data(), path.size());
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, name, &spec);
    if(err != noErr)
        return err;
    LaunchParamBlockRec pb = {};
    pb.launchBlockID = extendedBlock;
    pb.launchEPBLength = extendedBlockLen;
    pb.launchControlFlags = launchContinue | launchNoFileFlags;
    pb.launchAppSpec = &spec;
    return LaunchApplication(&pb);
}

/* ---- disk images in and out while Executor runs ---- */
const char IMAGES[] = "/uploads/";

/* The disk images' drives (by path) and whether a volume is on each now. */
std::map<std::string, bool> image_drives()
{
    std::map<std::string, bool> out;
    for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
    {
        auto *dqp = (DrvQExtra *)((char *)dp - sizeof(LONGINT));
        const char *name = (const char *)dqp->devicename;
        if(!name || strncmp(name, IMAGES, sizeof IMAGES - 1) != 0)
            continue;
        bool &on = out[name];
        for(HVCB *v = (HVCB *)LM(VCBQHdr).qHead; v; v = (HVCB *)v->qLink)
            if(v->vcbDrvNum == dp->dQDrive)
                on = true;
    }
    return out;
}

/* A File Manager trap called as a program would, through the trap table:
   the System's patches on _MountVol and _UnmountVol are how the Finder
   hears of volumes coming and going. A0 the parameter block (in the
   system heap: the patches read it), D0 the result. */
OSErr fs_trap(uint16_t trap, INTEGER vref)
{
    static std::map<uint16_t, Ptr> stubs;
    static ParmBlkPtr pb = nullptr;
    Ptr &code = stubs[trap];
    if(!code)
    {
        code = NewPtrSys(4);
        ((GUEST<uint16_t> *)code)[0] = trap;          /* _MountVol, _UnmountVol */
        ((GUEST<uint16_t> *)code)[1] = 0x4E75;        /* RTS */
    }
    if(!pb)
        pb = (ParmBlkPtr)NewPtrSysClear(sizeof(ParamBlockRec));
    memset((void *)pb, 0, sizeof(ParamBlockRec));
    pb->volumeParam.ioVRefNum = vref;
    uint32_t d[8], a[7];
    for(int i = 0; i < 8; i++)
        d[i] = EM_DREG(i);
    for(int i = 0; i < 7; i++)
        a[i] = EM_AREG(i);
    EM_A0 = US_TO_SYN68K(pb);
    execute68K(US_TO_SYN68K(code));
    OSErr err = (int16_t)EM_D0;
    for(int i = 0; i < 8; i++)
        EM_DREG(i) = d[i];
    for(int i = 0; i < 7; i++)
        EM_AREG(i) = a[i];
    return err;
}

/* The image's drive (ROMlib_image_drive: its flags worked out, locked for
   a CD), then _MountVol on it, as GetNextEvent would for a disk put in; a
   diskEvt for the program in front. */
OSErr mount_image(const std::string &path)
{
    INTEGER drive = ROMlib_image_drive(path.c_str());
    if(!drive)
        return -1;
    OSErr err = fs_trap(0xA00F, drive);
    PPostEvent(diskEvt, ((LONGINT)err << 16) | (uint16_t)drive, nullptr);
    return err;
}

/* Through _UnmountVol, as the Finder's Put Away. */
OSErr eject_image(const std::string &path)
{
    for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
    {
        auto *dqp = (DrvQExtra *)((char *)dp - sizeof(LONGINT));
        if(!dqp->devicename || path != (const char *)dqp->devicename)
            continue;
        for(HVCB *v = (HVCB *)LM(VCBQHdr).qHead, *next; v; v = next)
        {
            next = (HVCB *)v->qLink;
            if(v->vcbDrvNum != dp->dQDrive)
                continue;
            if(OSErr err = fs_trap(0xA00E, v->vcbVRefNum))
                return err;
        }
        return noErr;
    }
    return nsvErr;
}

/* Drives that had a volume and have none now: ejected. A volume ejected
   but still known (offline: the Finder's Eject) is unmounted too, the
   image being gone from the page, so no grey icon stays on the desktop. */
std::map<std::string, bool> seen;
void watch_images()
{
    auto now = image_drives();
    for(auto &[path, on] : now)
    {
        if(seen[path] && !on)
        {
            for(DrvQEl *dp = (DrvQEl *)LM(DrvQHdr).qHead; dp; dp = (DrvQEl *)dp->qLink)
            {
                auto *dqp = (DrvQExtra *)((char *)dp - sizeof(LONGINT));
                if(!dqp->devicename || path != (const char *)dqp->devicename)
                    continue;
                for(HVCB *v = (HVCB *)LM(VCBQHdr).qHead, *next; v; v = next)
                {
                    next = (HVCB *)v->qLink;
                    if(!v->vcbDrvNum && (v->vcbDRefNum == dp->dQDrive || v->vcbDRefNum == -dp->dQDrive))
                        fs_trap(0xA00E, v->vcbVRefNum);
                }
            }
            js_ejected(path.c_str());
        }
        seen[path] = on;
    }
}

/* At an event call of the running application. */
void on_event_poll()
{
    char buf[1024];
    while(int id = js_take_request(buf, sizeof buf))
    {
        std::string op = buf, arg = buf + op.size() + 1;
        std::string reply;
        if(op == "app")
        {
            /* The front process (as mac-phoenix's /api/app): CurApName
               names whichever process makes this event call, the Finder in
               the background behind a full-screen game. */
            std::string front = ROMlib_front_process_name();
            reply = json_string(!front.empty() ? front
                                : LM(CurApName)[0] <= 31 ? pstring(LM(CurApName)) : "");
        }
        else if(op == "windows")
            reply = windows_json();
        else if(op == "menus")
            reply = menus_json();
        else if(op == "launch")
            reply = std::to_string(launch(arg));
        else if(op == "volumes")
        {
            reply = "[";
            for(HVCB *v = (HVCB *)LM(VCBQHdr).qHead; v; v = (HVCB *)v->qLink)
                reply += (reply.size() > 1 ? "," : "") + json_string(pstring((const unsigned char *)v->vcbVN));
            reply += "]";
        }
        else if(op == "mount")
            reply = std::to_string(mount_image(arg));
        else if(op == "eject")
            reply = std::to_string(eject_image(arg));
        else if(op == "screen1x")
        {
            std::vector<uint32_t> px;
            int w, h;
            if(driver() && driver()->copyFrame(px, w, h))
            {
                for(uint32_t &p : px)
                    p = (p & 0xFF00FF00) | (p >> 16 & 0xFF) | (p & 0xFF) << 16;
                js_reply_pixels(id, w, h, (const uint8_t *)px.data());
                continue;
            }
            reply = "null";
        }
        else
            reply = "{\"error\":" + json_string("unknown request " + op) + "}";
        js_reply(id, reply.c_str());
    }
    watch_images();
}

void on_yield()
{
    int32_t ev[3 * 64];
    int n;
    while((n = js_take_input(ev, 64)) > 0)
        for(int i = 0; i < n; i++)
        {
            const int32_t *e = &ev[3 * i];
            switch(e[0])
            {
                case 1:
                    executor_host::mouse_moved(e[1], e[2]);
                    break;
                case 2:
                    executor_host::mouse_button(e[1] != 0);
                    break;
                case 3:
                    executor_host::key(e[1] != 0, (uint8_t)e[2]);
                    break;
            }
        }
    if(auto *v = driver())
    {
        double t0 = emscripten_get_now();
        v->pump();
        js_frame_time(emscripten_get_now() - t0);
    }
}

}

/* web_main [--size WxH] [--disk IMAGE]... [--system IMAGE] [--ppc] [--ram MB]
 * [--depth BPP] [APP]: the screen size, disk images to mount (read-only),
 * the image whose System Folder to run on (Apple's System and Finder:
 * copied into Executor's volume, and the Finder started), fat
 * applications' PowerPC code run (as --executor-ppc), the memory (default:
 * 32 MB, 64 on Apple's System) and the depth the screen starts in (as
 * --depth; default: as the System's Macs), and an application to start
 * instead of Executor's Browser or the Finder (paths in the module's
 * filesystem). The page's settings (web/executor/settings.js) say which. */
int main(int argc, char **argv)
{
    executor_host::Config c;
    std::string system_image;
    int ram_mb = 0, bpp = 0;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if(a == "--size" && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &c.width, &c.height);
        else if(a == "--scale" && i + 1 < argc)
            c.scale = atof(argv[++i]);
        else if(a == "--disk" && i + 1 < argc)
            c.disks.push_back(argv[++i]);
        else if(a == "--cdrom" && i + 1 < argc)  /* locked, as a Mac's CDs */
            c.cdroms.push_back(argv[++i]);
        else if(a == "--system" && i + 1 < argc)
            system_image = argv[++i];
        else if(a == "--ppc")
            c.prefer_ppc = true;  /* fat applications' PowerPC code too */
        else if(a == "--ram" && i + 1 < argc)
            ram_mb = atoi(argv[++i]);
        else if(a == "--depth" && i + 1 < argc)
            bpp = atoi(argv[++i]);
        else if(a == "--logtraps")  /* the page's ?logtraps= (main.js) */
            c.logtraps = true;
        else if(a == "--logtraps-filter" && i + 1 < argc)
            c.logtraps_filter = argv[++i];
        else if(a == "--logtraps-nesting" && i + 1 < argc)
            c.logtraps_nesting = atoi(argv[++i]);
        else if(a == "--debug" && i + 1 < argc)
            c.debug_options = argv[++i];
        else if(a == "--ppc-break" && i + 1 < argc)  /* ?ppcbreak= */
            c.ppc_breaks.push_back(argv[++i]);
        else
            c.app = a;
    }
    /* The depth the System's Macs start in, as natively: 8.x 32 bits, 7.x
       and Executor's own System 8 (at 8 bits 8.6's desktop pattern maps
       to one colour of the standard palette, flat lavender). */
    c.bpp = bpp;
    c.ram_mb = 32;
    c.data_dir = "/executor"; /* in memory; Executor makes its System Folder */
    /* Disk images are writable (CDs, --cdrom, are not): the page keeps
       their changes in an overlay (web/executor/overlay.js); the images
       themselves are never written. */
    c.writable_images = true;
    /* Removable, as a floppy or a CD put in: the Finder puts them away
       without asking (the System's image stays a fixed disk). */
    ROMlib_ejectable_images = true;
    ROMlib_fixed_image = system_image;
    if(!system_image.empty())
    {
        bool finder = false;
        std::string err;
        /* A System Folder already there is the one the page restored (its
           saved volume, worker.js): the user's, preferences and all. */
        std::string sys = c.data_dir + "/System Folder";
        if(std::filesystem::exists(sys + "/System"))
        {
            finder = std::filesystem::exists(sys + "/Finder");
            fprintf(stderr, "[web] the saved System Folder%s\n", finder ? ", starting the Finder" : "");
            if(finder && c.app.empty())
                c.app = sys + "/Finder";
            c.ram_mb = 64;
        }
        else if(executor_host::sideload_system(system_image, c.data_dir, &finder, &err))
        {
            fprintf(stderr, "[web] System Folder from %s%s\n", system_image.c_str(),
                    finder ? ", starting the Finder" : "");
            if(finder && c.app.empty())
                c.app = c.data_dir + "/System Folder/Finder";
            c.ram_mb = 64;
        }
        else
            fprintf(stderr, "[web] no System Folder from %s: %s\n", system_image.c_str(), err.c_str());
    }
    if(ram_mb > 0)
        c.ram_mb = ram_mb;
    /* "Host", as mac-phoenix names its first shared folder: files the page
       reads and writes (worker.js 'read', 'write'). */
    c.shared_folders.push_back("/host");
    c.open_transport = true;  /* answers with errors: no network yet */
    PhoenixVideoDriver::rectsHook = on_rects;
    c.on_cursor = [](const uint8_t data[32], const uint8_t mask[32], int hot_x, int hot_y, bool visible) {
        js_cursor(data, mask, hot_x, hot_y, visible);
    };
    /* Sound: always taken; the page's AudioWorklet keeps a short buffer
       and drops what falls behind (the Sound Manager runs on the clock). */
    c.on_audio = [](const uint8_t *s16be, uint32_t frames) {
        js_audio(s16be, (int)frames);
        return true;
    };
    Executor::hostidle::set_yield_hook(on_yield);
    executor_host::set_event_poll_hook(on_event_poll);
    fprintf(stderr, "[web] Executor %dx%d\n", c.width, c.height);
    return executor_host::run(c);
}
