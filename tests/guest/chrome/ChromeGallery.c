/* ChromeGallery: a document window and a floating window of platinum
   chrome (push buttons, check box, radio button, sliders, progress bar,
   pop-up, little arrows, disclosure triangle, tabs) that stays on the
   screen until it is told to quit. For looking at Executor's HiDPI chrome
   (--executor-scale 2, docs/executor/HiDPI.md) and the ctest cell
   hidpi_chrome_executor, which compares the 2x surface with the 1x frame
   inside the boxes below (tests/test_hidpi_chrome.sh: keep them in step). */
#include <Types.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Controls.h>
#include <Events.h>
#include <AppleEvents.h>
#include <Appearance.h>

static Boolean quit = false;

static pascal OSErr Quit(const AppleEvent *e, AppleEvent *reply, long refcon)
{
    quit = true;
    return noErr;
}

struct ctl
{
    short proc, top, left, bottom, right, value, min, max;
    const char *title;
};

/* the document window's controls, in its local coordinates */
static const struct ctl ctls[] = {
    { 368, 20, 20, 40, 90, 0, 0, 1, "\pOK" },
    { 368, 20, 110, 40, 190, 0, 0, 1, "\pCancel" },
    { 369, 50, 20, 66, 120, 1, 0, 1, "\pCheck" },
    { 370, 70, 20, 86, 120, 1, 0, 1, "\pRadio" },
    { 48, 100, 20, 124, 200, 5, 0, 10, "\p" },  /* pointing down */
    { 52, 130, 20, 154, 200, 3, 0, 10, "\p" },  /* pointing up */
    { 56, 160, 20, 184, 200, 7, 0, 10, "\p" },  /* non-directional */
    { 80, 194, 20, 208, 200, 50, 0, 100, "\p" }, /* progress bar */
    { 48, 20, 220, 200, 244, 5, 0, 10, "\p" },  /* vertical, pointing right */
    { 1008, 50, 260, 70, 380, 200, 0, 0, "\p" }, /* pop-up, menu 200 */
    { 96, 80, 260, 103, 273, 5, 0, 10, "\p" },  /* little arrows */
    { 64, 80, 290, 92, 302, 1, 0, 1, "\p" },    /* disclosure triangle, open */
    { 368, 20, 260, 40, 340, 0, 0, 1, "\pOff" }, /* inactive */
};

static void DrawGallery(WindowRef w)
{
    Rect r;
    SetPort((GrafPtr)w);
    DrawControls(w);
    /* a pane, and on it north tabs: the front one, one behind, one
       pressed */
    SetRect(&r, 260, 161, 420, 210);
    DrawThemeTabPane(&r, kThemeStateActive);
    SetRect(&r, 260, 140, 313, 164);
    DrawThemeTab(&r, kThemeTabFront, kThemeTabNorth, NULL, 0);
    SetRect(&r, 313, 140, 366, 164);
    DrawThemeTab(&r, kThemeTabNonFront, kThemeTabNorth, NULL, 0);
    SetRect(&r, 366, 140, 419, 164);
    DrawThemeTab(&r, kThemeTabNonFrontPressed, kThemeTabNorth, NULL, 0);
}

int main(void)
{
    WindowRef doc, floater;
    ControlHandle h;
    MenuHandle m;
    Rect r;
    int i;
    EventRecord e;
    Boolean shown = false;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    RegisterAppearanceClient();
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP(Quit), 0, false);

    m = NewMenu(200, "\p");
    AppendMenu(m, "\pChrome");
    InsertMenu(m, -1);

    /* the floating window first: the document window in front, active */
    SetRect(&r, 480, 80, 620, 160);
    floater = NewCWindow(NULL, &r, "\pFloat", true, kWindowFloatProc, (WindowRef)-1, true, 0);
    SetRect(&r, 20, 60, 460, 300);
    doc = NewCWindow(NULL, &r, "\pChrome Gallery", true, zoomDocProc, (WindowRef)-1, true, 0);
    SetPort((GrafPtr)doc);
    SetThemeWindowBackground(doc, kThemeBrushDialogBackgroundActive, true);
    for(i = 0; i < (int)(sizeof ctls / sizeof ctls[0]); i++)
    {
        const struct ctl *c = &ctls[i];
        SetRect(&r, c->left, c->top, c->right, c->bottom);
        h = NewControl(doc, &r, (ConstStr255Param)c->title, true, c->value, c->min, c->max, c->proc, 0);
        if(i == sizeof ctls / sizeof ctls[0] - 1)
            HiliteControl(h, 255);
    }

    while(!quit)
    {
        if(!WaitNextEvent(everyEvent, &e, 30, NULL))
        {
            /* once in front, the whole window again: on Executor 8.6 the
               part BridgeAgent's window covered stays blank otherwise */
            if(!shown)
            {
                SetPort((GrafPtr)doc);
                InvalRect(&((GrafPtr)doc)->portRect);
                shown = true;
            }
            continue;
        }
        switch(e.what)
        {
            case updateEvt:
                BeginUpdate((WindowRef)e.message);
                EraseRect(&((GrafPtr)e.message)->portRect);
                if((WindowRef)e.message == doc)
                    DrawGallery(doc);
                EndUpdate((WindowRef)e.message);
                break;
            case kHighLevelEvent:
                AEProcessAppleEvent(&e);
                break;
        }
    }
    DisposeWindow(floater);
    DisposeWindow(doc);
    return 0;
}
