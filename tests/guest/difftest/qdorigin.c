/* qdorigin: QuickDraw's pattern origin (QDGetPatternOrigin,
   QDSetPatternOrigin; NQD, the ROM's QuickDraw), differential
   (tools/difftest/run.py qdorigin): what it is at the start, whether it
   belongs to the port or to QuickDraw, and how it moves a pattern fill.
   Every 9.0.4 session calls both at startup (Executor had neither: the
   Point asked for was left as it was). */
#include "probe.h"

typedef pascal void (*GetOrigin)(Point *);
typedef pascal void (*SetOrigin_)(Point);

static GetOrigin get_origin;
static SetOrigin_ set_origin;

static void show(const char *key)
{
    Point p;
    memset(&p, SENTINEL, sizeof p);
    get_origin(&p);
    out(key, "%d,%d", p.v, p.h);
}

static void set(short v, short h)
{
    Point p;
    p.v = v;
    p.h = h;
    set_origin(p);
}

int main(void)
{
    static const Pattern diag = { { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 } };
    GWorldPtr gw = NULL, gw2 = NULL;
    CGrafPtr save_port;
    GDHandle save_dev;
    Rect r;

    probe_open("qdorigin");
    /* 9.0.4: NQD's, a G3 ROM library (the System's ToolboxUtils imports them from
       it); InterfaceLib has neither */
    get_origin = (GetOrigin)probe_bind("NQD", "QDGetPatternOrigin");
    set_origin = (SetOrigin_)probe_bind("NQD", "QDSetPatternOrigin");
    if(!get_origin || !set_origin)
    {
        probe_close();
        return 0;
    }
    SetRect(&r, 0, 0, 16, 16);
    NewGWorld(&gw, 8, &r, NULL, NULL, 0);
    NewGWorld(&gw2, 8, &r, NULL, NULL, 0);
    GetGWorld(&save_port, &save_dev);

    if(SECTION("value"))
    {
        show("start");
        set(3, 5);
        show("set.3.5");
        set(-2, 40);
        show("set.-2.40");
        set(0, 0);
        show("set.0.0");
    }
    if(SECTION("port") && gw && gw2)
    {
        /* set in one port, asked in another */
        SetGWorld(gw, NULL);
        set(3, 5);
        show("gw.set.3.5");
        SetGWorld(gw2, NULL);
        show("gw2.after");
        SetGWorld(save_port, save_dev);
        show("window.after");
        set(0, 0);
    }
    if(SECTION("fill") && gw)
    {
        SetGWorld(gw, NULL);
        LockPixels(GetGWorldPixMap(gw));
        set(0, 0);
        FillRect(&r, &diag);
        probe_pixels("origin.0.0", gw);
        EraseRect(&r);
        set(0, 3);
        FillRect(&r, &diag);
        probe_pixels("origin.0.3", gw);
        EraseRect(&r);
        set(2, 0);
        FillRect(&r, &diag);
        probe_pixels("origin.2.0", gw);
        set(0, 0);
        SetGWorld(save_port, save_dev);
    }
    probe_close();
    return 0;
}
