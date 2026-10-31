/* wind: Window Manager 2.0 (Mac OS 8.5), differential
   (tools/difftest/run.py wind): CreateNewWindow for each window class and
   attribute set (class, attributes, features, bounds, structure widths,
   regions by region code), the classic window definitions through the same
   calls, bounds and positioning, properties, the modified and collapsed
   states, content colour, floating windows, proxies. */
#include "probe.h"
#include <Windows.h>

#define SET(x) memset(&(x), SENTINEL, sizeof(x))

static const WindowClass classes[] = { 1, 2, 3, 4, 5, 6, 8, 10, 13 };
static const WindowAttributes attrs[] = { 0, kWindowStandardDocumentAttributes, kWindowStandardFloatingAttributes,
                                          kWindowCloseBoxAttribute | kWindowFullZoomAttribute };
static const WindowRegionCode regions[] = { 0, 1, 2, 3, 5, 6, 7, 32, 33, 35 };

/* class, attributes, features, bounds, widths, regions of a window */
static void describe(const char *k, WindowRef w)
{
    char kk[96];
    WindowClass cls;
    WindowAttributes at;
    UInt32 feat;
    Rect r;
    OSStatus err;
    int i;
    SET(cls);
    err = GetWindowClass(w, &cls);
    sprintf(kk, "%s.class", k);
    out(kk, "err %ld %ld", (long)err, (long)cls);
    SET(at);
    err = GetWindowAttributes(w, &at);
    sprintf(kk, "%s.attributes", k);
    out(kk, "err %ld %08lx", (long)err, (unsigned long)at);
    SET(feat);
    err = GetWindowFeatures(w, &feat);
    sprintf(kk, "%s.features", k);
    out(kk, "err %ld %08lx", (long)err, (unsigned long)feat);
    SET(r);
    err = GetWindowStructureWidths(w, &r);
    sprintf(kk, "%s.widths", k);
    out(kk, "err %ld %d,%d,%d,%d", (long)err, r.top, r.left, r.bottom, r.right);
    for(i = 0; i < (int)(sizeof regions / sizeof regions[0]); i++)
    {
        RgnHandle rgn = NewRgn();
        SET(r);
        err = GetWindowBounds(w, regions[i], &r);
        sprintf(kk, "%s.bounds.%d", k, (int)regions[i]);
        out(kk, "err %ld %d,%d,%d,%d", (long)err, r.top, r.left, r.bottom, r.right);
        err = GetWindowRegion(w, regions[i], rgn);
        sprintf(kk, "%s.region.%d", k, (int)regions[i]);
        if(err)
            out(kk, "err %ld", (long)err);
        else
            out_rgn(kk, rgn);
        DisposeRgn(rgn);
    }
}

static void create(void)
{
    int c, a;
    char k[96];
    Rect b;
    if(!SECTION("create"))
        return;
    SetRect(&b, 100, 100, 300, 250);
    for(c = 0; c < (int)(sizeof classes / sizeof classes[0]); c++)
        for(a = 0; a < (int)(sizeof attrs / sizeof attrs[0]); a++)
        {
            WindowRef w;
            OSStatus err;
            SET(w);
            err = CreateNewWindow(classes[c], attrs[a], &b, &w);
            sprintf(k, "%ld.%d", (long)classes[c], a);
            /* an unimplemented call leaves the sentinel: no window */
            out(k, "err %ld %s", (long)err, err ? "" : w == (WindowRef)0xA5A5A5A5 ? "unset" : w ? "made" : "nil");
            if(err || !w || w == (WindowRef)0xA5A5A5A5)
                continue;
            describe(k, w);
            DisposeWindow(w);
        }
}

/* the classic definitions: document, dialog box, plain, alt dialog, no
   grow, zoom, movable modal, floating (WDEF 124); and the Appearance
   Manager's (WDEF 64-67): document, the dialogs, floating, side floating */
static void classic(void)
{
    static const short procs[] = { 0, 1, 2, 3, 4, 8, 12, 5, 1985, 1987,
                                   1024, 1031, 1040, 1041, 1042, 1043, 1044, 1045, 1046,
                                   1057, 1071, 1073, 1075 };
    int i, color;
    char k[96];
    Rect b;
    if(!SECTION("classic"))
        return;
    SetRect(&b, 100, 100, 300, 250);
    for(color = 0; color < 2; color++)
        for(i = 0; i < (int)(sizeof procs / sizeof procs[0]); i++)
        {
            WindowRef w = color ? NewCWindow(NULL, &b, "\pclassic", false, procs[i], (WindowRef)-1, true, 0)
                                : NewWindow(NULL, &b, "\pclassic", false, procs[i], (WindowRef)-1, true, 0);
            sprintf(k, "%s.%d", color ? "c" : "bw", procs[i]);
            out(k, "%s", w ? "made" : "nil");
            if(!w)
                continue;
            describe(k, w);
            DisposeWindow(w);
        }
}

static void bounds(void)
{
    WindowRef w = NULL;
    Rect b, r;
    Point ideal;
    OSStatus err;
    Boolean std;
    if(!SECTION("bounds"))
        return;
    SetRect(&b, 100, 100, 300, 250);
    CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes, &b, &w);
    if(!w)
        return;
    SetRect(&r, 50, 60, 250, 200);
    err = SetWindowBounds(w, kWindowContentRgn, &r);
    out("SetWindowBounds.content", "err %ld", (long)err);
    SET(r); GetWindowBounds(w, kWindowContentRgn, &r); out_rect("content", &r);
    SET(r); GetWindowBounds(w, kWindowStructureRgn, &r); out_rect("structure", &r);
    SetRect(&r, 40, 40, 260, 230);
    err = SetWindowBounds(w, kWindowStructureRgn, &r);
    out("SetWindowBounds.structure", "err %ld", (long)err);
    SET(r); GetWindowBounds(w, kWindowContentRgn, &r); out_rect("content.2", &r);
    err = MoveWindowStructure(w, 20, 30);
    out("MoveWindowStructure", "err %ld", (long)err);
    SET(r); GetWindowBounds(w, kWindowStructureRgn, &r); out_rect("structure.moved", &r);
    err = RepositionWindow(w, NULL, kWindowCenterOnMainScreen);
    out("RepositionWindow.center", "err %ld", (long)err);
    SET(r); GetWindowBounds(w, kWindowStructureRgn, &r); out_rect("structure.centered", &r);
    err = RepositionWindow(w, NULL, 3 /* kWindowAlertPositionOnMainScreen */);
    out("RepositionWindow.alert", "err %ld", (long)err);
    SET(r); GetWindowBounds(w, kWindowStructureRgn, &r); out_rect("structure.alert", &r);
    ideal.h = 120; ideal.v = 80;
    SET(r);
    std = IsWindowInStandardState(w, &ideal, &r);
    out("IsWindowInStandardState", "%d", (int)std);
    out_rect("IsWindowInStandardState.state", &r);
    SET(r);
    err = GetWindowIdealUserState(w, &r);
    out("GetWindowIdealUserState", "err %ld", (long)err);
    out_rect("GetWindowIdealUserState.rect", &r);
    SetRect(&r, 70, 80, 170, 180);
    err = SetWindowIdealUserState(w, &r);
    out("SetWindowIdealUserState", "err %ld", (long)err);
    SET(r); GetWindowIdealUserState(w, &r); out_rect("GetWindowIdealUserState.after", &r);
    out("IsWindowCollapsed", "%d", (int)IsWindowCollapsed(w));
    out("IsWindowCollapsable", "%d", (int)IsWindowCollapsable(w));
    err = CollapseWindow(w, true);
    out("CollapseWindow", "err %ld %d", (long)err, (int)IsWindowCollapsed(w));
    /* collapsed: the title bar alone */
    describe("collapsed", w);
    err = CollapseWindow(w, false);
    out("CollapseWindow.back", "err %ld %d", (long)err, (int)IsWindowCollapsed(w));
    /* SetWindowBounds by the structure, the window shown */
    ShowWindow(w);
    SetRect(&r, 40, 40, 260, 230);
    err = SetWindowBounds(w, kWindowStructureRgn, &r);
    out("SetWindowBounds.structure.visible", "err %ld", (long)err);
    SET(r); GetWindowBounds(w, kWindowContentRgn, &r); out_rect("content.visible", &r);
    DisposeWindow(w);
}

static void props(void)
{
    WindowRef w = NULL;
    Rect b;
    OSStatus err;
    UInt32 size, value = 0x12345678, back;
    RGBColor c = { 0x1111, 0x2222, 0x3333 }, got;
    if(!SECTION("props"))
        return;
    SetRect(&b, 100, 100, 300, 250);
    CreateNewWindow(kDocumentWindowClass, 0, &b, &w);
    if(!w)
        return;
    SET(size);
    err = GetWindowPropertySize(w, 'prob', 'tag1', &size);
    out("GetWindowPropertySize.none", "err %ld", (long)err);
    err = SetWindowProperty(w, 'prob', 'tag1', sizeof value, &value);
    out("SetWindowProperty", "err %ld", (long)err);
    SET(size);
    err = GetWindowPropertySize(w, 'prob', 'tag1', &size);
    out("GetWindowPropertySize", "err %ld %lu", (long)err, (unsigned long)size);
    SET(back); SET(size);
    err = GetWindowProperty(w, 'prob', 'tag1', sizeof back, &size, &back);
    out("GetWindowProperty", "err %ld %lu %08lx", (long)err, (unsigned long)size, (unsigned long)back);
    SET(back); SET(size);
    err = GetWindowProperty(w, 'prob', 'tag1', 2, &size, &back);
    out("GetWindowProperty.short", "err %ld %lu %08lx", (long)err, (unsigned long)size, (unsigned long)back);
    err = RemoveWindowProperty(w, 'prob', 'tag1');
    out("RemoveWindowProperty", "err %ld", (long)err);
    err = RemoveWindowProperty(w, 'prob', 'tag1');
    out("RemoveWindowProperty.again", "err %ld", (long)err);
    out("IsWindowModified", "%d", (int)IsWindowModified(w));
    err = SetWindowModified(w, true);
    out("SetWindowModified", "err %ld %d", (long)err, (int)IsWindowModified(w));
    SET(got);
    err = GetWindowContentColor(w, &got);
    out("GetWindowContentColor", "err %ld %04x %04x %04x", (long)err, got.red, got.green, got.blue);
    err = SetWindowContentColor(w, &c);
    SET(got);
    GetWindowContentColor(w, &got);
    out("SetWindowContentColor", "err %ld %04x %04x %04x", (long)err, got.red, got.green, got.blue);
    DisposeWindow(w);
}

static void floating(void)
{
    WindowRef doc = NULL, flt = NULL, front;
    Rect b;
    OSStatus err;
    if(!SECTION("floating"))
        return;
    SetRect(&b, 100, 100, 300, 250);
    CreateNewWindow(kDocumentWindowClass, 0, &b, &doc);
    SetRect(&b, 320, 100, 420, 200);
    CreateNewWindow(kFloatingWindowClass, 0, &b, &flt);
    if(!doc || !flt)
        return;
    ShowWindow(doc);
    ShowWindow(flt);
    front = FrontNonFloatingWindow();
    out("FrontNonFloatingWindow", "%s", front == doc ? "doc" : front == flt ? "floating" : front ? "other" : "nil");
    front = FrontWindow();
    out("FrontWindow", "%s", front == doc ? "doc" : front == flt ? "floating" : front ? "other" : "nil");
    out("AreFloatingWindowsVisible", "%d", (int)AreFloatingWindowsVisible());
    err = HideFloatingWindows();
    out("HideFloatingWindows", "err %ld %d visible %d", (long)err, (int)AreFloatingWindowsVisible(),
        (int)((WindowPeek)flt)->visible);
    err = ShowFloatingWindows();
    out("ShowFloatingWindows", "err %ld %d visible %d", (long)err, (int)AreFloatingWindowsVisible(),
        (int)((WindowPeek)flt)->visible);
    out("IsValidWindowPtr.doc", "%d", (int)IsValidWindowPtr(doc));
    out("IsValidWindowPtr.nil", "%d", (int)IsValidWindowPtr(NULL));
    out("IsValidWindowPtr.bogus", "%d", (int)IsValidWindowPtr((WindowRef)&b));
    DisposeWindow(flt);
    DisposeWindow(doc);
    out("IsValidWindowPtr.disposed", "%d", (int)IsValidWindowPtr(doc));
}

static void proxy(void)
{
    WindowRef w = NULL;
    Rect b;
    OSStatus err;
    FSSpec spec;
    if(!SECTION("proxy"))
        return;
    SetRect(&b, 100, 100, 300, 250);
    CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes, &b, &w);
    if(!w)
        return;
    SET(spec);
    err = GetWindowProxyFSSpec(w, &spec);
    out("GetWindowProxyFSSpec.none", "err %ld", (long)err);
    err = SetWindowProxyCreatorAndType(w, 'ttxt', 'TEXT', 0);
    out("SetWindowProxyCreatorAndType", "err %ld", (long)err);
    SET(spec);
    err = GetWindowProxyFSSpec(w, &spec);
    out("GetWindowProxyFSSpec.typed", "err %ld", (long)err);
    err = RemoveWindowProxy(w);
    out("RemoveWindowProxy", "err %ld", (long)err);
    err = RemoveWindowProxy(w);
    out("RemoveWindowProxy.again", "err %ld", (long)err);
    DisposeWindow(w);
}

int main(void)
{
    probe_open("wind");
    create();
    classic();
    bounds();
    props();
    floating();
    proxy();
    probe_close();
    return 0;
}
