/*
 * requests.cpp - automation requests (session.h request()), answered at an
 * event call of the running application, a safe point in a process's world:
 *
 *   app              the current application's name (CurApName)
 *   windows          its windows: title, global bounds, visible, kind; a
 *                    dialog's static text and ParamText's strings too (an
 *                    alert's message); with "items", its items too: type,
 *                    global rectangle, a control's title or a text item's text
 *   menus            its menu bar: each menu's title and left edge
 *   mouse            where the Mac has the mouse, and its screen
 *   drawn            the strings drawn lately (ROMlib_text_drawn)
 *   launch PATH      LaunchApplication of "Volume:Folder:App"; its OSErr
 *   mount PATH       a disk image put in now (the tab's Disks panel's
 *                    insert, the image in the module's filesystem already):
 *                    mounted through _MountVol (the System's patches tell the
 *                    Finder), a diskEvt posted, as for a floppy; the OSErr
 *                    (-1: the image can't be opened; nsvErr: no Mac volume
 *                    on it)
 *   mount-additions PATH  the same, its System Folder additions copied in
 *                    first (session.cpp)
 *   mount-cd PATH    the same, a CD: locked (ROMlib_locked_images, as --cdrom)
 *   eject PATH       a disk image's volumes unmounted through _UnmountVol
 *                    (fBsyErr: in use)
 *   volumes          the volumes mounted: their names
 *   flush            every volume's changes written out (FlushVol), as
 *                    before a shutdown: a native front end about to quit
 *                    (frontends/desktop); the count flushed
 *   touch VOL:NAME   a small file written there (made if it isn't) and the
 *                    volume flushed: a write to a disk image the front
 *                    end can check (executor/tests/desktop_finder.sh); the
 *                    OSErr
 *   resize WxH       the Mac's screen at that size, live
 *   scale N          the Mac drawn at N times its size, live
 */
#include "internal.h"

#include <rsys/displaymode.h>

#include <ControlMgr.h>
#include <DialogMgr.h>
#include <FileMgr.h>
#include <file/file.h>
#include <hfs/hfs.h>
#include <hidpi/hidpi.h>
#include <menu/menu.h>
#include <MemoryMgr.h>
#include <ProcessMgr.h>
#include <quickdraw/cquick.h>
#include <quickdraw/quick.h>
#include <SegmentLdr.h>
#include <rsys/process.h>
#include <util/macstrings.h>
#include <wind/wind.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace executor_host::session {

using namespace Executor;

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

namespace {

std::string windows_json(bool items)
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
            if(!items)
            {
                out += "}";
                continue;
            }
            /* Its items, on the screen: a control's title, a text item's
               text (what a test clicks by name). */
            out += ",\"items\":[";
            Rect &bounds = PORT_BOUNDS((GrafPtr)w);
            for(int i = 1, n = CountDITL((DialogPtr)w); i <= n; i++)
            {
                GUEST<INTEGER> type;
                GUEST<Handle> h;
                Rect ir;
                C_GetDialogItem((DialogPtr)w, i, &type, &h, &ir);
                int kind = type & 0x7F;
                Str255 s = { 0 };
                if(h && kind >= ctrlItem && kind < statText)   /* buttons, boxes, radios, resource controls */
                    C_GetControlTitle((ControlHandle)(Handle)h, s);
                else if(h && (kind == statText || kind == editText))
                    C_GetDialogItemText(h, s);
                out += (i > 1 ? "," : "") + std::string("{\"type\":") + std::to_string(kind)
                       + ",\"rect\":[" + std::to_string(ir.top - bounds.top) + "," + std::to_string(ir.left - bounds.left)
                       + "," + std::to_string(ir.bottom - bounds.top) + "," + std::to_string(ir.right - bounds.left)
                       + "],\"title\":" + json_string(pstring(s)) + "}";
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

/* The front end's UTF-8 as the Mac's MacRoman (file names: "ƒ", "™", ...). */
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

/* The strings drawn lately, oldest first: "drawn"
   (executor/tests/web/tab_m1.mjs: a program's output in its window) */
std::deque<std::string> drawn;

}

void text_drawn(const char *text, int len)
{
    drawn.emplace_back(text, len);
    if(drawn.size() > 512)
        drawn.pop_front();
}

std::optional<std::string> request(const std::string &op, const std::string &arg)
{
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
        reply = windows_json(arg == "items");
    else if(op == "menus")
        reply = menus_json();
    else if(op == "mouse")
    {
        /* Where the Mac has the mouse, and its screen: what a click
           from the tab lands on (executor/tests/web/tab_input.mjs). */
        Point m = LM(MouseLocation);
        reply = "{\"x\":" + std::to_string(m.h) + ",\"y\":" + std::to_string(m.v)
                + ",\"width\":" + std::to_string(vdriver->width())
                + ",\"height\":" + std::to_string(vdriver->height()) + "}";
    }
    else if(op == "drawn")
    {
        reply = "[";
        for(const std::string &d : drawn)
            reply += (reply.size() > 1 ? "," : "") + json_string(d);
        reply += "]";
    }
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
    else if(op == "mount-additions")
    {
        add_to_system(arg, true);
        reply = std::to_string(mount_image(arg));
    }
    else if(op == "mount-cd")
    {
        ROMlib_locked_images.insert(arg);
        reply = std::to_string(mount_image(arg));
    }
    else if(op == "eject")
        reply = std::to_string(eject_image(arg));
    else if(op == "flush")
    {
        int n = 0;
        for(HVCB *v = (HVCB *)LM(VCBQHdr).qHead; v; v = (HVCB *)v->qLink)
            if(v->vcbDrvNum && FlushVol(nullptr, v->vcbVRefNum) == noErr)
                n++;
        reply = std::to_string(n);
    }
    else if(op == "touch")
    {
        /* names and data in the system heap: guest-addressable */
        StringPtr name = (StringPtr)NewPtrSysClear(256);
        const char text[] = "written by a touch request\r";
        Ptr data = name ? NewPtrSys(sizeof text - 1) : nullptr;
        if(!name || !data)
        {
            if(name)
                DisposePtr((Ptr)name);
            return std::to_string(memFullErr);
        }
        name[0] = (uint8_t)std::min<size_t>(arg.size(), 255);
        memcpy(name + 1, arg.data(), name[0]);
        memcpy(data, text, sizeof text - 1);
        OSErr err = HCreate(0, 0, name, "ttxt"_4, "TEXT"_4);
        GUEST<INTEGER> ref;
        if(err == noErr || err == dupFNErr)
            err = HOpenDF(0, 0, name, fsRdWrPerm, &ref);
        if(err == noErr)
        {
            GUEST<LONGINT> count = sizeof text - 1;
            err = FSWrite(ref, &count, data);
            OSErr closed = FSClose(ref);
            if(err == noErr)
                err = closed;
        }
        /* the volume: the name up to its first colon */
        size_t colon = arg.find(':');
        if(err == noErr && colon != std::string::npos)
        {
            name[0] = (uint8_t)(colon + 1);
            err = FlushVol(name, 0);
        }
        DisposePtr((Ptr)name);
        DisposePtr(data);
        reply = std::to_string(err);
    }
    else if(op == "resize")
    {
        /* "WxH": the Mac's screen at that size, live (the tab's Settings,
           the browser window): the Display Manager takes it at the front
           process's event call (quickdraw/displays.cpp); the front end
           hears the new size with the next rects. Answers the size it
           has now and whether the one asked for is in range. */
        int w = 0, h = 0;
        bool ok = sscanf(arg.c_str(), "%dx%d", &w, &h) == 2 && ROMlib_dm_size_acceptable(w, h);
        if(ok)
            vdriver->requestSize(w, h);
        reply = "{\"ok\":" + std::string(ok ? "true" : "false")
                + ",\"width\":" + std::to_string(vdriver->width())
                + ",\"height\":" + std::to_string(vdriver->height()) + "}";
    }
    else if(op == "scale")
    {
        /* "N": draw the Mac at N times its size (the HiDPI box,
           hidpi.h), live: taken at the front process's event call as a
           size is (displays.cpp), the screen painted again; the front
           end's canvas follows the frames' size. Without Skia: 1 only. */
        double s = atof(arg.c_str());
#if EXECUTOR_HIDPI
        bool ok = s >= 1.0 && s <= 4.0;
#else
        bool ok = s == 1.0;
#endif
        if(ok)
            vdriver->requestScale(s);
        char buf[64];
        snprintf(buf, sizeof buf, "{\"ok\":%s,\"scale\":%g}", ok ? "true" : "false", hidpi::scale());
        reply = buf;
    }
    else
        return std::nullopt;
    return reply;
}

}  // namespace executor_host::session
