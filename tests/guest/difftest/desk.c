/* desk: the desktop picture calls of WindowsLib (8.5, private: the 8.6
   Appearance control panel's Desktop tab and its preview) and QuickDraw's
   mapping calls on empty rectangles, differential (tools/difftest/run.py
   desk). The panel maps the screen's rectangle (from a monitor desktop
   record) onto its preview with MapPt/MapRect: when Executor lacked
   GetMonitorDesktopRecordFromList the rectangle stayed empty and MapPt
   divided by zero. Signatures from the panel's call sites (tools/macdecode/
   pef.py --dis); the records' layout is what this probe prints. Also
   AppearanceLib's IterateThemes and GetSystemFont, and the other calls the
   panel's session logged as unimplemented. */
#include "probe.h"
#include <Appearance.h>
#include <Collections.h>
#include <MixedMode.h>
#include <Processes.h>
#include <Controls.h>
#include <Dialogs.h>
#include <Lists.h>

#define SET(x) memset(&(x), SENTINEL, sizeof(x))

typedef void *MDList;
typedef OSStatus (*GetList_t)(MDList *);
typedef OSStatus (*DisposeList_t)(MDList);
typedef OSStatus (*Refresh_t)(MDList, MDList *);
typedef OSStatus (*Count_t)(MDList, UInt32 *);
typedef OSStatus (*GetRecord_t)(MDList, UInt32, void *);
typedef OSStatus (*GetAlign_t)(MDList, UInt32, UInt32 *);
typedef OSStatus (*SetAlign_t)(MDList, UInt32, UInt32);
typedef OSStatus (*HasPicture_t)(MDList, UInt32, Boolean *);
typedef OSStatus (*SetToFile_t)(MDList, UInt32, FSSpec *, UInt32);
typedef OSStatus (*WillTakeTime_t)(MDList, UInt32, GrafPtr, const Rect *, Boolean *);
typedef OSStatus (*DrawFromList_t)(MDList, UInt32, GrafPtr, const Rect *, RgnHandle, PixPatHandle, Point,
                                  Boolean);
typedef OSStatus (*FileContains_t)(FSSpec *, Boolean *);
typedef OSStatus (*GetFont_t)(ScriptCode, StringPtr);
typedef OSStatus (*Iterate_t)(UniversalProcPtr, void *);

static void map_cases(void)
{
    static const Rect srcs[] = {
        { 0, 0, 0, 0 },       /* empty */
        { 10, 20, 10, 20 },   /* a point */
        { 0, 0, 100, 0 },     /* no width */
        { 0, 0, 0, 100 },     /* no height */
        { 100, 100, 0, 0 },   /* inverted */
        { 0, 0, 480, 640 },   /* a screen */
        { 0, 0, 480, 640 },   /* the same, onto an empty rectangle (below) */
    };
    static const Rect dst = { 10, 10, 58, 74 }, dst_empty = { 5, 5, 5, 5 };
    static const Point pts[] = { { 0, 0 }, { 7, 5 }, { 200, -3 }, { -40, 1000 }, { -5, -15 }, { 15, 5 } };
    char k[64];
    unsigned s, p;
    for(s = 0; s < sizeof srcs / sizeof srcs[0]; s++)
    {
        const Rect *d = s == 6 ? &dst_empty : &dst;
        for(p = 0; p < sizeof pts / sizeof pts[0]; p++)
        {
            Point pt = pts[p];
            MapPt(&pt, &srcs[s], &dst);
            sprintf(k, "MapPt.%u.%u", s, p);
            out(k, "%d,%d", pt.v, pt.h);
            pt = pts[p];
            MapPt(&pt, &srcs[s], &dst_empty);
            sprintf(k, "MapPt.%u.%u.toempty", s, p);
            out(k, "%d,%d", pt.v, pt.h);
        }
        {
            Rect r = { 3, 4, 30, 40 };
            MapRect(&r, &srcs[s], d);
            sprintf(k, "MapRect.%u", s);
            out_rect(k, &r);
        }
        {
            RgnHandle rgn = NewRgn();
            Rect r = { 3, 4, 30, 40 }, r2 = { 20, 30, 60, 90 };
            RgnHandle rgn2 = NewRgn();
            RectRgn(rgn, &r);
            MapRgn(rgn, &srcs[s], d);
            sprintf(k, "MapRgn.rect.%u", s);
            out_rgn(k, rgn);
            RectRgn(rgn, &r);
            RectRgn(rgn2, &r2);
            UnionRgn(rgn, rgn2, rgn);
            MapRgn(rgn, &srcs[s], d);
            sprintf(k, "MapRgn.complex.%u", s);
            out_rgn(k, rgn);
            DisposeRgn(rgn);
            DisposeRgn(rgn2);
        }
        {
            PolyHandle poly = OpenPoly();
            MoveTo(3, 4);
            LineTo(30, 4);
            LineTo(17, 40);
            LineTo(3, 4);
            ClosePoly();
            MapPoly(poly, &srcs[s], d);
            sprintf(k, "MapPoly.%u", s);
            out(k, "bbox %d,%d,%d,%d crc %08lx", (**poly).polyBBox.top, (**poly).polyBBox.left,
                (**poly).polyBBox.bottom, (**poly).polyBBox.right,
                probe_crc32((unsigned char *)*poly, GetHandleSize((Handle)poly), 0));
            KillPoly(poly);
        }
    }
}

/* A record as bytes, 32 a line; the device handle at +0x18 (and any
   other address) is told by what it points to, not printed. */
#define REC_SIZE 0x200
static unsigned char rec[REC_SIZE];

static void out_record(const char *key)
{
    char k[64];
    int o, end = REC_SIZE;
    while(end > 0 && rec[end - 1] == SENTINEL)
        end--;
    sprintf(k, "%s.size~", key);
    out(k, "%d", end);
    for(o = 0; o < end; o += 32)
    {
        sprintf(k, "%s.%03x~", key, o);
        out_hex(k, rec + o, end - o < 32 ? end - o : 32);
    }
}

static void draw_into(const char *key, DrawFromList_t draw, MDList list, UInt32 idx, PixPatHandle pp,
                      Boolean flag)
{
    GWorldPtr gw;
    Rect r = { 0, 0, 48, 64 };
    char k[64];
    if(NewGWorld(&gw, 32, &r, NULL, NULL, 0) == noErr)
    {
        CGrafPtr port;
        GDHandle dev;
        RgnHandle rgn = NewRgn();
        Point origin = { 0, 0 };
        OSStatus err;
        GetGWorld(&port, &dev);
        SetGWorld(gw, NULL);
        LockPixels(GetGWorldPixMap(gw));
        EraseRect(&r);
        RectRgn(rgn, &r);
        err = draw(list, idx, (GrafPtr)gw, &r, rgn, pp, origin, flag);
        SetGWorld(port, dev);
        sprintf(k, "%s.err", key);
        out(k, "%d", (int)err);
        probe_pixels(key, gw);
        DisposeRgn(rgn);
        DisposeGWorld(gw);
    }
}

/* A small PICT file on Host:, set as the screen's picture in each
   alignment, drawn into a GWorld (the list is never made current: the
   machine's desktop stays as it is). */
static void picture(MDList list, SetToFile_t set_file, GetAlign_t get_align, HasPicture_t has_pict,
                    WillTakeTime_t will, DrawFromList_t draw, GetRecord_t get_rec)
{
    FSSpec spec;
    Rect frame = { 0, 0, 30, 40 }, wr = { 0, 0, 48, 64 };
    PicHandle pic;
    short ref;
    long n;
    OSStatus err;
    UInt32 a, align;
    Boolean b;
    char k[64];
    static unsigned char zero[512];

    ClipRect(&frame);
    pic = OpenPicture(&frame);
    {
        RGBColor red = { 0xFFFF, 0, 0 }, blue = { 0, 0, 0xFFFF };
        Rect r1 = { 2, 2, 20, 30 }, r2 = { 10, 15, 28, 38 };
        RGBForeColor(&red);
        PaintRect(&r1);
        RGBForeColor(&blue);
        PaintOval(&r2);
        ForeColor(blackColor);
        FrameRect(&frame);
    }
    ClosePicture();
    ClipRect(&wr);
    FSMakeFSSpec(0, 0, "\pHost:desk.pict", &spec);
    FSpDelete(&spec);
    err = FSpCreate(&spec, 'ttxt', 'PICT', 0);
    out("pict.create", "%d", (int)err);
    if(err || FSpOpenDF(&spec, fsRdWrPerm, &ref))
        return;
    n = 512;
    FSWrite(ref, &n, zero);
    n = GetHandleSize((Handle)pic);
    HLock((Handle)pic);
    FSWrite(ref, &n, *pic);
    FSClose(ref);
    KillPicture(pic);
    out("pict.size", "%ld", n);

    for(a = 0; a <= 7; a++)
    {
        err = set_file(list, 1, &spec, a);
        sprintf(k, "pict.%lu.set", (unsigned long)a);
        out(k, "%d", (int)err);
        SET(align);
        err = get_align(list, 1, &align);
        sprintf(k, "pict.%lu.alignment", (unsigned long)a);
        out(k, "err %d %08lx", (int)err, (unsigned long)align);
        SET(b);
        err = has_pict(list, 1, &b);
        sprintf(k, "pict.%lu.HasPicture", (unsigned long)a);
        out(k, "err %d %02x", (int)err, b);
        SET(b);
        err = will(list, 1, qd.thePort, &wr, &b);
        sprintf(k, "pict.%lu.WillTakeTime", (unsigned long)a);
        out(k, "err %d %02x", (int)err, b);
        SET(rec);
        get_rec(list, 1, rec);
        sprintf(k, "pict.%lu.record", (unsigned long)a);
        out_record(k);
        sprintf(k, "pict.%lu.rect", (unsigned long)a);
        out_rect(k, (Rect *)(rec + 0x24));
        sprintf(k, "pict.%lu.data", (unsigned long)a);
        if(*(Handle *)(rec + 8))
            out(k, "size %ld", GetHandleSize(*(Handle *)(rec + 8)));
        else
            out(k, "nil");
        sprintf(k, "pict.%lu.draw", (unsigned long)a);
        draw_into(k, draw, list, 1, NULL, 0);
    }
    err = set_file(list, 1, NULL, 0);
    out("pict.clear", "%d", (int)err);
    FSpDelete(&spec);
}

static void desk_list(void)
{
    GetList_t get_list = (GetList_t)probe_bind("WindowsLib", "GetCurrentMonitorDesktopList");
    DisposeList_t dispose = (DisposeList_t)probe_bind("WindowsLib", "DisposeMonitorDesktopList");
    Refresh_t refresh = (Refresh_t)probe_bind("WindowsLib", "RefreshMonitorDesktopList");
    Count_t count = (Count_t)probe_bind("WindowsLib", "CountMonitorDesktopRecordsInList");
    GetRecord_t get_rec = (GetRecord_t)probe_bind("WindowsLib", "GetMonitorDesktopRecordFromList");
    GetAlign_t get_align = (GetAlign_t)probe_bind("WindowsLib", "GetMonitorDesktopAlignment");
    SetAlign_t set_align = (SetAlign_t)probe_bind("WindowsLib", "SetMonitorDesktopAlignment");
    HasPicture_t has_pict = (HasPicture_t)probe_bind("WindowsLib", "MonitorDesktopHasPicture");
    SetToFile_t set_file = (SetToFile_t)probe_bind("WindowsLib", "SetMonitorDesktopToFile");
    WillTakeTime_t will = (WillTakeTime_t)probe_bind("WindowsLib", "DrawMonitorDesktopFromListWillTakeTime");
    DrawFromList_t draw = (DrawFromList_t)probe_bind("WindowsLib", "DrawMonitorDesktopFromList");
    FileContains_t contains = (FileContains_t)probe_bind("WindowsLib", "FileContainsPossibleDesktopPicture");
    MDList list, list2;
    UInt32 n, i, align;
    Boolean b;
    OSStatus err;
    char k[64];

    if(!get_list || !count || !get_rec)
        return;
    SET(list);
    err = get_list(&list);
    out("GetCurrent.err", "%d", (int)err);
    out("GetCurrent.list", "%s", list == (MDList)0xA5A5A5A5 ? "unset" : list ? "set" : "nil");
    if(err || !list || list == (MDList)0xA5A5A5A5)
        return;
    out("GetCurrent.list.size~", "%ld", GetPtrSize((Ptr)list));
    SET(n);
    err = count(list, &n);
    out("Count", "err %d n %lu", (int)err, (unsigned long)n);
    for(i = 0; i <= n + 1 && i < 4; i++)
    {
        SET(rec);
        err = get_rec(list, i, rec);
        sprintf(k, "GetRecord.%lu.err", (unsigned long)i);
        out(k, "%d", (int)err);
        sprintf(k, "GetRecord.%lu", (unsigned long)i);
        out_record(k);
        if(!err)
        {
            GDHandle gd = *(GDHandle *)(rec + 0x18);
            sprintf(k, "GetRecord.%lu.gd", (unsigned long)i);
            out(k, "%s", gd == GetMainDevice() ? "main" : "other");
            sprintf(k, "GetRecord.%lu.gdRect", (unsigned long)i);
            out_rect(k, &(**GetMainDevice()).gdRect);
        }
        if(get_align)
        {
            SET(align);
            err = get_align(list, i, &align);
            sprintf(k, "GetAlignment.%lu", (unsigned long)i);
            out(k, "err %d %08lx", (int)err, (unsigned long)align);
        }
        if(has_pict)
        {
            SET(b);
            err = has_pict(list, i, &b);
            sprintf(k, "HasPicture.%lu", (unsigned long)i);
            out(k, "err %d %02x", (int)err, b);
        }
        if(will)
        {
            Rect r = { 0, 0, 48, 64 };
            SET(b);
            err = will(list, i, qd.thePort, &r, &b);
            sprintf(k, "WillTakeTime.%lu", (unsigned long)i);
            out(k, "err %d %02x", (int)err, b);
        }
    }
    if(set_align && get_align)
    {
        err = set_align(list, 1, 2);
        out("SetAlignment.1.2", "%d", (int)err);
        SET(align);
        err = get_align(list, 1, &align);
        out("SetAlignment.1.2.after", "err %d %08lx", (int)err, (unsigned long)align);
        SET(rec);
        get_rec(list, 1, rec);
        out_record("SetAlignment.1.2.record");
        err = set_align(list, 9, 2);
        out("SetAlignment.9", "%d", (int)err);
    }
    if(set_file && has_pict)
    {
        err = set_file(list, 1, NULL, 5);
        out("SetToFile.nil", "%d", (int)err);
        SET(b);
        err = has_pict(list, 1, &b);
        out("SetToFile.nil.HasPicture", "err %d %02x", (int)err, b);
        SET(rec);
        get_rec(list, 1, rec);
        out_record("SetToFile.nil.record");
    }
    if(draw)
    {
        PixPatHandle pp = GetPixPat(16);
        draw_into("DrawFromList", draw, list, 1, NULL, 0);
        draw_into("DrawFromList.pixpat", draw, list, 1, pp, 0);
        draw_into("DrawFromList.pixpat.flag", draw, list, 1, pp, 1);
        draw_into("DrawFromList.flag", draw, list, 1, NULL, 1);
        draw_into("DrawFromList.0", draw, list, 0, NULL, 0);
        if(pp)
            DisposePixPat(pp);
    }
    if(set_file && get_align && has_pict && draw && will)
        picture(list, set_file, get_align, has_pict, will, draw, get_rec);
    if(contains)
    {
        FSSpec self;
        ProcessSerialNumber psn = { 0, kCurrentProcess };
        ProcessInfoRec info;
        memset(&info, 0, sizeof info);
        info.processInfoLength = sizeof info;
        info.processAppSpec = &self;
        GetProcessInformation(&psn, &info);
        SET(b);
        err = contains(&self, &b);
        out("FileContains.app", "err %d %02x", (int)err, b);
    }
    if(refresh && dispose)
    {
        SET(list2);
        err = refresh(list, &list2);
        out("Refresh.err", "%d", (int)err);
        out("Refresh.list", "%s", list2 == (MDList)0xA5A5A5A5 ? "unset" : list2 == list ? "same"
                                  : list2 ? "new" : "nil");
        if(!err && list2 && list2 != (MDList)0xA5A5A5A5)
        {
            SET(n);
            count(list2, &n);
            out("Refresh.count", "%lu", (unsigned long)n);
            SET(align);
            if(get_align)
            {
                get_align(list2, 1, &align);
                out("Refresh.alignment", "%08lx", (unsigned long)align);
            }
            if(list2 != list)
                out("Refresh.dispose.new", "%d", (int)dispose(list2));
        }
        out("Dispose", "%d", (int)dispose(list));
    }
}

static int theme_calls;
static pascal Boolean theme_proc(ConstStr255Param name, SInt16 id, Collection c, void *data)
{
    char k[64];
    SInt32 items = c ? CountCollectionItems(c) : -1;
    sprintf(k, "IterateThemes.%d", theme_calls);
    out_str(k, name);
    sprintf(k, "IterateThemes.%d.id", theme_calls);
    out(k, "%d items %ld data %s", id, (long)items, data == (void *)0x1234 ? "ok" : "wrong");
    if(c)
    {
        /* each item's tag, id and size */
        char line[1024];
        SInt32 i, n = 0;
        line[0] = 0;
        for(i = 1; i <= items && n < (SInt32)sizeof line - 32; i++)
        {
            CollectionTag tag = 0;
            SInt32 tid = 0, size = 0;
            GetIndexedCollectionItemInfo(c, i, &tag, &tid, &size, NULL);
            n += sprintf(line + n, "%c%c%c%c/%ld/%ld ", (char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8),
                         (char)tag, (long)tid, (long)size);
        }
        sprintf(k, "IterateThemes.%d.tags", theme_calls);
        out(k, "%s", line);
    }
    theme_calls++;
    return true;
}

static void themes(void)
{
    Iterate_t iterate = (Iterate_t)probe_bind("AppearanceLib", "IterateThemes");
    GetFont_t sysfont = (GetFont_t)probe_bind("AppearanceLib", "GetSystemFont");
    GetFont_t smallfont = (GetFont_t)probe_bind("AppearanceLib", "GetSmallSystemFont");
    Str255 name;
    OSStatus err;
    if(sysfont)
    {
        SET(name);
        err = sysfont(-1, name);
        out("GetSystemFont.err", "%d", (int)err);
        out_str("GetSystemFont", name);
        SET(name);
        err = sysfont(0, name);
        out("GetSystemFont.roman", "err %d", (int)err);
        out_str("GetSystemFont.roman.name", name);
    }
    if(smallfont)
    {
        SET(name);
        err = smallfont(-1, name);
        out("GetSmallSystemFont.err", "%d", (int)err);
        out_str("GetSmallSystemFont", name);
    }
    if(iterate)
    {
        UniversalProcPtr upp = NewRoutineDescriptor(
            (ProcPtr)theme_proc,
            kPascalStackBased | RESULT_SIZE(SIZE_CODE(sizeof(Boolean)))
                | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(ConstStr255Param)))
                | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(SInt16)))
                | STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(Collection)))
                | STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(void *))),
            kPowerPCISA);
        err = iterate(upp, (void *)0x1234);
        out("IterateThemes.err", "%d calls %d", (int)err, theme_calls);
        DisposeRoutineDescriptor(upp);
    }
}

/* The Appearance panel's tab panes: items appended into a dialog with
   kDialogFlagsUseControlHierarchy land in the user pane that holds them;
   what is visible as the panes are hidden and shown (desk.r). */
static void out_vis(const char *key, DialogPtr d, short first, short n)
{
    char buf[32], k[64];
    short i;
    for(i = 0; i < n; i++)
    {
        ControlHandle c = NULL;
        buf[i] = GetDialogItemAsControl(d, first + i, &c) ? '?' : IsControlVisible(c) ? 'V' : '-';
    }
    buf[n] = 0;
    sprintf(k, "%s.visible", key);
    out(k, "%s", buf);
}

static void panes(void)
{
    DialogPtr d = GetNewDialog(400, NULL, (WindowPtr)-1);
    ControlHandle pane = NULL, pane2 = NULL, check = NULL, root = NULL, super = NULL, extra;
    Rect r = { 120, 20, 136, 200 };
    short base, i;
    OSErr err;
    char k[64];
    if(!d)
    {
        out("dialog", "nil");
        return;
    }
    SetPort(d);
    base = CountDITL(d);
    AppendDialogItemList(d, 401, overlayDITL);
    out("count", "%d", CountDITL(d) - base);
    GetDialogItemAsControl(d, base + 1, &pane);
    GetDialogItemAsControl(d, base + 2, &check);
    GetRootControl(d, &root);
    for(i = 2; i <= 4; i++)
    {
        ControlHandle c = NULL;
        GetDialogItemAsControl(d, base + i, &c);
        super = NULL;
        err = GetSuperControl(c, &super);
        sprintf(k, "item%d.super", i);
        out(k, "err %d %s", err, super == pane ? "pane" : super == root ? "root" : super ? "other" : "nil");
    }
    out_vis("appended", d, base + 1, 4);
    ShowControl(pane);
    out_vis("ShowControl", d, base + 1, 4);
    HideControl(check);
    HideControl(pane);
    out_vis("HideControl", d, base + 1, 4);
    ShowControl(pane);
    out_vis("ShowControl.again", d, base + 1, 4);
    ShowControl(check);
    err = SetControlVisibility(pane, false, false);
    out("SetControlVisibility.hide", "%d", err);
    out_vis("SetControlVisibility.hide", d, base + 1, 4);
    err = SetControlVisibility(pane, true, false);
    out_vis("SetControlVisibility.show", d, base + 1, 4);
    SetControlVisibility(check, false, false);
    SetControlVisibility(pane, false, false);
    SetControlVisibility(pane, true, false);
    out_vis("SetControlVisibility.child", d, base + 1, 4);
    SetControlVisibility(check, true, false);
    HideControl(pane);
    /* a visible control put into the hidden pane */
    extra = NewControl(d, &r, "\pNew", true, 0, 0, 1, checkBoxProc, 0);
    err = EmbedControl(extra, pane);
    out("EmbedControl.hidden", "err %d visible %d", err, IsControlVisible(extra));
    ShowControl(pane);
    out("EmbedControl.shown", "visible %d", IsControlVisible(extra));
    HideControl(pane);
    DisposeControl(extra);
    extra = NewControl(d, &r, "\pNew", true, 0, 0, 1, checkBoxProc, 0);
    err = AutoEmbedControl(extra, d);
    super = NULL;
    GetSuperControl(extra, &super);
    out("AutoEmbedControl.hidden", "err %d visible %d super %s", err, IsControlVisible(extra),
        super == pane ? "pane" : super == root ? "root" : "other");
    DisposeControl(extra);
    /* which controls AutoEmbedControl puts into the hidden pane: the
       pane's own rectangle, one pixel in, half outside; an embedder too */
    {
        static const Rect rs[] = { { 10, 10, 200, 450 }, { 11, 11, 199, 449 }, { 150, 400, 220, 470 } };
        static const char *names[] = { "same", "inset", "across" };
        for(i = 0; i < 3; i++)
        {
            short proc;
            for(proc = 0; proc < 2; proc++)
            {
                extra = NewControl(d, &rs[i], "\p", true, proc ? 2 : 0, 0, proc ? 0 : 1,
                                   proc ? 256 /* kControlUserPaneProc */ : checkBoxProc, 0);
                err = AutoEmbedControl(extra, d);
                super = NULL;
                GetSuperControl(extra, &super);
                sprintf(k, "AutoEmbed.%s.%s", names[i], proc ? "pane" : "check");
                out(k, "err %d visible %d super %s", err, IsControlVisible(extra),
                    super == pane ? "pane" : super == root ? "root" : "other");
                DisposeControl(extra);
            }
        }
    }
    /* ShowControl of a control the hidden pane holds */
    HideControl(check);
    ShowControl(check);
    out("ShowControl.inhidden", "visible %d", IsControlVisible(check));
    ShowControl(pane);
    out("ShowControl.inhidden.pane", "visible %d", IsControlVisible(check));
    HideControl(pane);
    /* HideDialogItem/ShowDialogItem on an item in the hidden pane */
    HideDialogItem(d, base + 2);
    ShowDialogItem(d, base + 2);
    out_vis("ShowDialogItem", d, base + 1, 4);
    ShowControl(pane);
    out_vis("ShowDialogItem.pane", d, base + 1, 4);
    HideControl(pane);

    /* a second tab's pane, visible from its resource */
    base = CountDITL(d);
    AppendDialogItemList(d, 402, overlayDITL);
    GetDialogItemAsControl(d, base + 1, &pane2);
    {
        ControlHandle c = NULL;
        GetDialogItemAsControl(d, base + 2, &c);
        super = NULL;
        GetSuperControl(c, &super);
        out("pane2.item2.super", "%s", super == pane2 ? "pane2" : super == pane ? "pane" : super == root ? "root" : "other");
    }
    out_vis("pane2", d, base + 1, 4);
    super = NULL;
    GetSuperControl(pane2, &super);
    out("pane2.super", "%s", super == pane ? "pane" : super == root ? "root" : "other");
    out_vis("pane1.after", d, base - 3, 4);

    /* a list box in a hidden pane: where its scroll bars are */
    base = CountDITL(d);
    AppendDialogItemList(d, 403, overlayDITL);
    {
        ControlHandle pane3 = NULL, lb = NULL;
        ListHandle list = NULL;
        Size got = 0;
        GetDialogItemAsControl(d, base + 1, &pane3);
        GetDialogItemAsControl(d, base + 2, &lb);
        err = GetControlData(lb, 0, 'lhan', sizeof list, (Ptr)&list, &got);
        out("listbox.data", "err %d %s", err, list ? "list" : "nil");
        if(!err && list)
        {
            ControlHandle bars[2];
            bars[0] = (**list).vScroll;
            bars[1] = (**list).hScroll;
            for(i = 0; i < 2; i++)
            {
                sprintf(k, "listbox.%s", i ? "hScroll" : "vScroll");
                if(!bars[i])
                {
                    out(k, "nil");
                    continue;
                }
                super = NULL;
                GetSuperControl(bars[i], &super);
                out(k, "super %s visible %d", super == lb ? "listbox" : super == pane3 ? "pane" : super == root ? "root" : "other",
                    IsControlVisible(bars[i]));
            }
            out("listbox.visible", "%d", IsControlVisible(lb));
            {
                /* embedding into controls that don't claim to embed */
                UInt32 f = 0;
                ControlHandle ok = NULL, x;
                Rect xr = { 30, 30, 46, 120 };
                err = GetControlFeatures(lb, &f);
                out("listbox.features", "err %d %08lx", err, (unsigned long)f);
                GetDialogItemAsControl(d, 1, &ok);
                x = NewControl(d, &xr, "\pX", true, 0, 0, 1, checkBoxProc, 0);
                err = EmbedControl(x, lb);
                super = NULL;
                GetSuperControl(x, &super);
                out("EmbedControl.listbox", "err %d super %s", err, super == lb ? "listbox" : super == root ? "root" : "other");
                err = EmbedControl(x, ok);
                super = NULL;
                GetSuperControl(x, &super);
                out("EmbedControl.button", "err %d super %s", err, super == ok ? "button" : super == lb ? "listbox" : super == root ? "root" : "other");
                DisposeControl(x);
            }
            ShowControl(pane3);
            out("listbox.shown", "%d v %d h %d", IsControlVisible(lb),
                bars[0] ? IsControlVisible(bars[0]) : -1, bars[1] ? IsControlVisible(bars[1]) : -1);
        }
    }
    DisposeDialog(d);
}

/* The Finder's other calls the session logged as unimplemented: the icon
   device, the pattern origin, the current layer. */
static void finder_calls(void)
{
    typedef void (*SetDev_t)(GDHandle);
    typedef void (*GetDev_t)(GDHandle *);
    typedef void (*SetOrigin_t)(Point);
    typedef void (*GetOrigin_t)(Point *);
    typedef WindowPtr (*Layer_t)(void);
    SetDev_t set_dev = (SetDev_t)probe_bind("PrivateInterfaceLib", "SetIconDevice");
    GetDev_t get_dev = (GetDev_t)probe_bind("PrivateInterfaceLib", "GetIconDevice");
    SetOrigin_t set_origin = (SetOrigin_t)probe_bind("NQD", "QDSetPatternOrigin");
    GetOrigin_t get_origin = (GetOrigin_t)probe_bind("NQD", "QDGetPatternOrigin");
    Layer_t lm_layer = (Layer_t)probe_bind("PrivateInterfaceLib", "LMGetCurLayer");
    Layer_t get_layer = (Layer_t)probe_bind("PrivateInterfaceLib", "GetCurLayer");
    GDHandle gd;
    if(get_dev)
    {
        SET(gd);
        get_dev(&gd);
        out("GetIconDevice", "%s", gd == (GDHandle)0xA5A5A5A5 ? "unset" : gd == GetMainDevice() ? "main" : gd ? "other" : "nil");
    }
    if(set_dev && get_dev)
    {
        set_dev(GetMainDevice());
        SET(gd);
        get_dev(&gd);
        out("SetIconDevice.main", "%s", gd == GetMainDevice() ? "main" : gd ? "other" : "nil");
        set_dev(NULL);
        SET(gd);
        get_dev(&gd);
        out("SetIconDevice.nil", "%s", gd == (GDHandle)0xA5A5A5A5 ? "unset" : gd == GetMainDevice() ? "main" : gd ? "other" : "nil");
    }
    if(get_origin && set_origin)
    {
        Point p, q = { 3, 5 };
        SET(p);
        get_origin(&p);
        out("QDGetPatternOrigin", "%d,%d", p.v, p.h);
        set_origin(q);
        SET(p);
        get_origin(&p);
        out("QDSetPatternOrigin", "%d,%d", p.v, p.h);
        q.v = q.h = 0;
        set_origin(q);
    }
    if(lm_layer && get_layer)
        out("LMGetCurLayer", "%s", lm_layer() == get_layer() ? "GetCurLayer" : lm_layer() ? "other" : "nil");
}

/* The Control Strip panel's: GetDefFontSize (with SysFontSiz 0 too), and
   the Gestalt selectors it checks before it loads. */
static void strip_calls(void)
{
    typedef short (*Size_t)(void);
    typedef void (*SetSize_t)(short);
    Size_t def_size = (Size_t)probe_bind("InterfaceLib", "GetDefFontSize");
    Size_t lm_size = (Size_t)probe_bind("InterfaceLib", "LMGetSysFontSize");
    SetSize_t lm_set = (SetSize_t)probe_bind("InterfaceLib", "LMSetSysFontSize");
    static const OSType sels[] = { 'sdev', 'csvr' };
    int i;
    if(def_size && lm_size)
        out("GetDefFontSize", "%d SysFontSiz %d", def_size(), lm_size());
    if(def_size && lm_size && lm_set)
    {
        short was = lm_size();
        lm_set(0);
        out("GetDefFontSize.0", "%d", def_size());
        lm_set(9);
        out("GetDefFontSize.9", "%d", def_size());
        lm_set(was);
    }
    for(i = 0; i < 2; i++)
    {
        char k[24];
        long r = 0;
        OSErr err = Gestalt(sels[i], &r);
        sprintf(k, "Gestalt.%.4s", (const char *)&sels[i]);
        out(k, "err %d %08lx", err, err ? 0UL : (unsigned long)r);
    }
}

int main(void)
{
    probe_open("desk");
    if(SECTION("map"))
        map_cases();
    if(SECTION("list"))
        desk_list();
    if(SECTION("themes"))
        themes();
    if(SECTION("panes"))
        panes();
    if(SECTION("finder"))
        finder_calls();
    if(SECTION("strip"))
        strip_calls();
    probe_close();
    return 0;
}
