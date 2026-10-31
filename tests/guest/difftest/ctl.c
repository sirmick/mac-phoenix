/* ctl: the Control Manager of Mac OS 8 and the Dialog Manager's 8.0 calls,
   differential (tools/difftest/run.py ctl): every standard control kind
   (values, features, best rectangle, data sizes by tag), embedding and its
   disposal, keyboard focus, 32-bit values, control data with short
   buffers, and an 8.0 dialog (items as controls) through AppendDITL and
   ShortenDITL with embedded items. */
#include "probe.h"
#include <Controls.h>
#include <Dialogs.h>
#include <Appearance.h>

#define SET(x) memset(&(x), SENTINEL, sizeof(x))

static WindowPtr win;
static ControlHandle root;

static const short procs[] = {
    0, 1, 2, 16, 1008, 32, 33, 34, 48, 49, 64, 80, 96, 112, 128, 129, 144, 160, 161, 162,
    164, 165, 176, 240, 256, 272, 273, 288, 304, 320, 352, 368, 384, 385, 386, 400, 416 };
static const OSType tags[] = {
    'text', 'tedh', 'font', 'mhan', 'mval', 'lhan', 'cont', 'clrn', 'prog', 'indi',
    'pass', 'kfil', 'sels', 'bvtp', 'bvwt', 'tabe', 'tabf', 'time', 'cics', 'kbbh' };

static void window(void)
{
    Rect r;
    SetRect(&r, 40, 60, 440, 360);
    win = NewCWindow(NULL, &r, "\pprobe", false, documentProc, (WindowPtr)-1, false, 0);
    SetPort(win);
    root = NULL;
}

/* The password edit text (273) shares its dialog's TextEdit record: 8.6's
   definition takes DialogRecord.textH from its owner and measures it for
   the best rectangle. In a plain window that reads past the window record,
   whatever is there (on Executor once a stale word, which it then took for
   a handle), so that kind is made in a dialog with no items. */
static DialogPtr dialog_owner(void)
{
    static DialogPtr dlg;
    if(!dlg)
    {
        Rect r;
        Handle items = NewHandleClear(2);
        **(short **)items = -1;
        SetRect(&r, 40, 60, 440, 360);
        dlg = NewDialog(NULL, &r, "\p", false, documentProc, (WindowPtr)-1, false, 0, items);
    }
    return dlg;
}

static void kinds(void)
{
    int i, t;
    char k[96];
    if(!SECTION("kind"))
        return;
    for(i = 0; i < (int)(sizeof procs / sizeof procs[0]); i++)
    {
        Rect r, best;
        SInt16 base;
        UInt32 feat;
        ControlHandle c;
        OSErr err;
        SetRect(&r, 10, 10, 110, 30);
        c = NewControl(procs[i] == 273 ? (WindowPtr)dialog_owner() : win,
                       &r, "\pTitle", false, 1, 0, 10, procs[i], 0);
        sprintf(k, "%d.new", procs[i]);
        out(k, "%s", c ? "made" : "nil");
        if(!c)
            continue;
        sprintf(k, "%d.values", procs[i]);
        out(k, "%d %d %d", GetControlValue(c), GetControlMinimum(c), GetControlMaximum(c));
        SET(feat);
        err = GetControlFeatures(c, &feat);
        sprintf(k, "%d.features", procs[i]);
        out(k, "err %d %08lx", err, (long)feat);
        SET(best); SET(base);
        err = GetBestControlRect(c, &best, &base);
        sprintf(k, "%d.best", procs[i]);
        out(k, "err %d %d,%d,%d,%d base %d", err, best.top, best.left, best.bottom, best.right, base);
        for(t = 0; t < (int)(sizeof tags / sizeof tags[0]); t++)
        {
            Size size;
            SET(size);
            err = GetControlDataSize(c, 0, tags[t], &size);
            sprintf(k, "%d.datasize.%.4s", procs[i], (char *)&tags[t]);
            out(k, "err %d %ld", err, (long)size);
        }
        DisposeControl(c);
    }
}

/* the classic definitions' best rectangles, by title (its width in the
   system font alongside) and rectangle height, and with the window's font
   (useWFont, Geneva 9) */
static void best_rects(void)
{
    static const short classic[] = { 0, 1, 2, 8, 9, 10 };
    static const char *titles[] = { "", "A", "Title", "Much Longer Title" };
    int i, t, h;
    char k[96];
    if(!SECTION("best"))
        return;
    TextFont(3); TextSize(9);
    for(i = 0; i < (int)(sizeof classic / sizeof classic[0]); i++)
        for(t = 0; t < (int)(sizeof titles / sizeof titles[0]); t++)
            for(h = 20; h <= 40; h += 20)
            {
                Str255 title;
                Rect r, best;
                SInt16 base;
                OSErr err;
                ControlHandle c;
                short w;
                title[0] = strlen(titles[t]);
                memcpy(title + 1, titles[t], title[0]);
                SetRect(&r, 10, 10, 210, 10 + h);
                c = NewControl(win, &r, title, false, 1, 0, 1, classic[i], 0);
                if(!c)
                    continue;
                TextFont(classic[i] & 8 ? 3 : 0);
                TextSize(classic[i] & 8 ? 9 : 0);
                w = StringWidth(title);
                TextFont(3); TextSize(9);
                SET(best); SET(base);
                err = GetBestControlRect(c, &best, &base);
                sprintf(k, "%d.%d.%d", classic[i], t, h);
                out(k, "err %d %d,%d,%d,%d base %d (title %d)", err, best.top, best.left,
                    best.bottom, best.right, base, w);
                DisposeControl(c);
            }
    TextFont(0); TextSize(0);
}

static int count_list(void)
{
    int n = 0;
    ControlHandle c;
    for(c = (ControlHandle)((WindowPeek)win)->controlList; c; c = (**c).nextControl)
        n++;
    return n;
}

static void embedding(void)
{
    Rect r;
    ControlHandle group, a, b, sub;
    UInt16 n;
    OSErr err;
    if(!SECTION("embed"))
        return;
    err = CreateRootControl(win, &root);
    out("CreateRootControl", "err %d", err);
    err = CreateRootControl(win, &sub);
    out("CreateRootControl.again", "err %d", err);
    SetRect(&r, 10, 10, 200, 150);
    group = NewControl(win, &r, "\pGroup", false, 0, 0, 1, 160, 0);
    SetRect(&r, 20, 30, 120, 50);
    a = NewControl(win, &r, "\pA", false, 0, 0, 1, 1, 0);
    SetRect(&r, 20, 60, 120, 80);
    b = NewControl(win, &r, "\pB", false, 0, 0, 1, 1, 0);
    SET(n); err = CountSubControls(root, &n); out("root.count", "err %d %d", err, n);
    err = EmbedControl(a, group); out("EmbedControl.a", "err %d", err);
    err = AutoEmbedControl(b, win); out("AutoEmbedControl.b", "err %d", err);
    SET(n); err = CountSubControls(group, &n); out("group.count", "err %d %d", err, n);
    SET(n); err = CountSubControls(root, &n); out("root.count.after", "err %d %d", err, n);
    SET(sub); err = GetSuperControl(a, &sub); out("a.super", "err %d %s", err, sub == group ? "group" : sub == root ? "root" : "other");
    SET(sub); err = GetIndexedSubControl(group, 1, &sub); out("group.1", "err %d %s", err, sub == a ? "a" : sub == b ? "b" : "other");
    SET(sub); err = GetIndexedSubControl(group, 3, &sub); out("group.3", "err %d", err);
    out("list.before", "%d", count_list());

    /* keyboard focus: two check boxes take it, the group doesn't */
    SET(sub); err = GetKeyboardFocus(win, &sub); out("focus.initial", "err %d %s", err, sub ? "set" : "nil");
    err = AdvanceKeyboardFocus(win); out("Advance", "err %d", err);
    SET(sub); err = GetKeyboardFocus(win, &sub); out("focus.1", "err %d %s", err, sub == a ? "a" : sub == b ? "b" : sub ? "other" : "nil");
    err = AdvanceKeyboardFocus(win); out("Advance.2", "err %d", err);
    SET(sub); err = GetKeyboardFocus(win, &sub); out("focus.2", "err %d %s", err, sub == a ? "a" : sub == b ? "b" : sub ? "other" : "nil");
    err = ReverseKeyboardFocus(win); out("Reverse", "err %d", err);
    SET(sub); err = GetKeyboardFocus(win, &sub); out("focus.3", "err %d %s", err, sub == a ? "a" : sub == b ? "b" : sub ? "other" : "nil");
    err = SetKeyboardFocus(win, group, kControlFocusNextPart); out("SetKeyboardFocus.group", "err %d", err);
    err = ClearKeyboardFocus(win); out("Clear", "err %d", err);
    SET(sub); err = GetKeyboardFocus(win, &sub); out("focus.cleared", "err %d %s", err, sub ? "set" : "nil");

    /* disposing of the embedder takes its subcontrols */
    SetKeyboardFocus(win, a, kControlFocusNextPart);
    DisposeControl(group);
    SET(n); err = CountSubControls(root, &n); out("root.count.disposed", "err %d %d", err, n);
    out("list.disposed", "%d", count_list());
    SET(sub); err = GetKeyboardFocus(win, &sub); out("focus.disposed", "err %d %s", err, sub ? "set" : "nil");
    err = AdvanceKeyboardFocus(win); out("Advance.disposed", "err %d", err);
}

static void values(void)
{
    Rect r;
    ControlHandle c;
    if(!SECTION("values"))
        return;
    SetRect(&r, 10, 10, 26, 200);
    c = NewControl(win, &r, "\p", false, 0, 0, 100, 16, 0);
    SetControl32BitMaximum(c, 100000);
    SetControl32BitValue(c, 70000);
    out("32bit", "%ld %ld %ld", (long)GetControl32BitValue(c), (long)GetControl32BitMinimum(c), (long)GetControl32BitMaximum(c));
    out("16bit", "%d %d %d", GetControlValue(c), GetControlMinimum(c), GetControlMaximum(c));
    SetControlMaximum(c, 500);
    out("after16", "%ld %ld", (long)GetControl32BitValue(c), (long)GetControl32BitMaximum(c));
    SetControl32BitValue(c, -5);
    out("clamped.low", "%ld", (long)GetControl32BitValue(c));
    SetControlViewSize(c, 50);
    out("viewsize", "%ld", (long)GetControlViewSize(c));
    DisposeControl(c);
}

static void data(void)
{
    Rect r;
    ControlHandle c;
    OSErr err;
    char buf[64];
    Size actual;
    if(!SECTION("data"))
        return;
    SetRect(&r, 10, 10, 200, 26);
    c = NewControl(win, &r, "\p", false, 0, 0, 0, 288, 0);   /* static text */
    err = SetControlData(c, 0, 'text', 5, (Ptr)"hello");
    out("static.set", "err %d", err);
    SET(actual); memset(buf, SENTINEL, sizeof buf);
    err = GetControlData(c, 0, 'text', 3, buf, &actual);
    out("static.get.short", "err %d actual %ld", err, (long)actual);
    out_hex("static.get.short.bytes", buf, 8);
    SET(actual); memset(buf, SENTINEL, sizeof buf);
    err = GetControlData(c, 0, 'text', sizeof buf, buf, &actual);
    out("static.get", "err %d actual %ld", err, (long)actual);
    out_hex("static.get.bytes", buf, 8);
    err = GetControlData(c, 0, 'xxxx', sizeof buf, buf, &actual);
    out("static.get.badtag", "err %d", err);
    DisposeControl(c);
    c = NewControl(win, &r, "\p", false, 0, 0, 0, 272, 0);    /* edit text */
    err = SetControlData(c, 0, 'text', 5, (Ptr)"world");
    out("edit.set", "err %d", err);
    SET(actual); memset(buf, SENTINEL, sizeof buf);
    err = GetControlData(c, 0, 'text', sizeof buf, buf, &actual);
    out("edit.get", "err %d actual %ld", err, (long)actual);
    out_hex("edit.get.bytes", buf, 8);
    {
        TEHandle te;
        SET(te);
        err = GetControlData(c, 0, 'tedh', sizeof te, (Ptr)&te, &actual);
        out("edit.tedh", "err %d actual %ld %s", err, (long)actual, te == (TEHandle)0xA5A5A5A5 ? "unset" : te ? "set" : "nil");
    }
    DisposeControl(c);
}

/* An in-memory DITL: count-1, then {handle, rect, type, length, data}. */
static Handle ditl(int n, const short *types, const char **texts)
{
    long size = 2, i;
    Handle h;
    char *p;
    for(i = 0; i < n; i++)
        size += 14 + ((strlen(texts[i]) + 1) & ~1);
    h = NewHandleClear(size);
    p = *h;
    *(short *)p = n - 1;
    p += 2;
    for(i = 0; i < n; i++)
    {
        short *rp = (short *)(p + 4);
        int len = strlen(texts[i]);
        rp[0] = 10 + 25 * i; rp[1] = 10; rp[2] = 30 + 25 * i; rp[3] = 150;
        p[12] = types[i];
        p[13] = len;
        memcpy(p + 14, texts[i], len);
        p += 14 + ((len + 1) & ~1);
    }
    return h;
}

static void dialogs(void)
{
    static const short types[] = { ctrlItem + btnCtrl, ctrlItem + chkCtrl, statText, editText, userItem };
    static const char *texts[] = { "OK", "Check", "Static", "Edit", "" };
    static const short more_types[] = { ctrlItem + radCtrl, statText };
    static const char *more_texts[] = { "Radio", "More" };
    Rect r;
    DialogPtr d;
    ControlHandle droot, item, group;
    UInt16 n;
    OSErr err;
    int i;
    char k[64];
    if(!SECTION("dialog"))
        return;
    SetRect(&r, 50, 50, 350, 350);
    d = NewFeaturesDialog(NULL, &r, "\pdlg", false, documentProc, (WindowPtr)-1, false, 0,
                          ditl(5, types, texts), kDialogFlagsUseControlHierarchy);
    out("NewFeaturesDialog", "%s", d ? "made" : "nil");
    if(!d)
        return;
    out("CountDITL", "%d", CountDITL(d));
    SET(droot); err = GetRootControl(d, &droot); out("root", "err %d %s", err, droot ? "set" : "nil");
    SET(n); err = CountSubControls(droot, &n); out("root.count", "err %d %d", err, n);
    for(i = 1; i <= 5; i++)
    {
        SET(item);
        err = GetDialogItemAsControl(d, i, &item);
        sprintf(k, "item.%d", i);
        out(k, "err %d %s", err, item == (ControlHandle)0xA5A5A5A5 ? "unset" : item ? "control" : "nil");
    }
    /* a group box as item 6, items 7 and 8 (appended) embedded in it */
    {
        static const short g_types[] = { ctrlItem + resCtrl };
        Handle extra = ditl(2, more_types, more_texts);
        SetRect(&r, 160, 10, 290, 120);
        group = NewControl(d, &r, "\pGroup", true, 0, 0, 1, 160, 0);
        (void)g_types;
        AppendDITL(d, extra, overlayDITL);
        DisposeHandle(extra);
        out("CountDITL.appended", "%d", CountDITL(d));
        for(i = 6; i <= 7; i++)
        {
            SET(item);
            err = GetDialogItemAsControl(d, i, &item);
            sprintf(k, "appended.%d", i);
            out(k, "err %d %s", err, item == (ControlHandle)0xA5A5A5A5 ? "unset" : item ? "control" : "nil");
            if(!err && item)
            {
                err = EmbedControl(item, group);
                sprintf(k, "appended.%d.embed", i);
                out(k, "err %d", err);
            }
        }
        SET(n); err = CountSubControls(group, &n); out("group.count", "err %d %d", err, n);
    }
    ShortenDITL(d, 2);
    out("CountDITL.shortened", "%d", CountDITL(d));
    SET(n); err = CountSubControls(group, &n); out("group.count.shortened", "err %d %d", err, n);
    SET(n); err = CountSubControls(droot, &n); out("root.count.shortened", "err %d %d", err, n);
    err = AdvanceKeyboardFocus(d); out("Advance.shortened", "err %d", err);
    DisposeDialog(d);
    out("disposed", "ok");
}

int main(void)
{
    probe_open("ctl");
    window();
    kinds();
    best_rects();
    embedding();
    values();
    data();
    dialogs();
    probe_close();
    return 0;
}
