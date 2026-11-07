/*
 * mvtest.c: a Retro68 application built against the Multiversal Interfaces
 * generated from executor/core/multiversal/defs (tests/test_multiversal_retro68.sh).
 * It is compiled and linked, not run: it checks that the C headers and the
 * glue library made from Executor's definitions work for a Mac program,
 * through the Universal Interfaces header names and the calls MacPhoenix
 * added to the definitions.
 */
#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Files.h>
#include <Resources.h>
#include <Gestalt.h>
#include <Processes.h>
/* Universal Interfaces names the generator writes for these definitions */
#include <Aliases.h>
#include <Balloons.h>
#include <Components.h>
#include <OSA.h>
#include <Displays.h>
#include <Folders.h>
#include <Lists.h>
#include <Notification.h>
#include <Scrap.h>
#include <Script.h>
#include <ShutDown.h>
#include <Sound.h>
#include <Threads.h>

static void layers(void)
{
    WindowPtr desktop = NULL, hit = NULL;
    Point pt = { 100, 100 };

    if (TakeDesktopLayer(&desktop) == noErr)
        (void)FindWindowInLayer(pt, &hit);
    (void)GetLayerAtPoint(pt);
}

static void displays(void)
{
    RgnHandle desk = NULL;
    GDHandle gd = DMGetFirstScreenDevice(true);

    if (gd && DMGetDeskRegion(&desk) == noErr && desk)
        InvalRgn(desk);
}

static void files(void)
{
    int16_t vRefNum;
    int32_t dirID;
    FSSpec spec;
    AliasHandle alias = NULL;
    int32_t freeBytes, maxBlock;

    if (FindFolder(kOnSystemDisk, 'pref', true, &vRefNum, &dirID) == noErr
        && FSMakeFSSpec(vRefNum, dirID, "\pmvtest", &spec) == noErr)
        (void)NewAlias(NULL, &spec, &alias);
    (void)GetTempMemInfo(&freeBytes, &maxBlock);
}

static void dialogs(DialogPtr d)
{
    EventRecord ev;
    int16_t modalClass;

    SetDialogDefaultItem(d, 1);
    SetDialogCancelItem(d, 2);
    if (GetNextEvent(everyEvent, &ev) && IsCancelEvent(&ev))
        HideWindow(d);
    (void)GetFrontWindowModalClass(&modalClass);
}

static void components(void)
{
    ComponentDescription cd = { 'osa ', 0, 0, 0, 0 };
    Component c = FindNextComponent(NULL, &cd);
    ComponentInstance ci;

    if(c && CountComponents(&cd) > 0 && OpenAComponent(c, &ci) == noErr)
    {
        AEDesc text;
        OSAID id = kOSANullScript;
        if(AECreateDesc('TEXT', "1 + 1", 5, &text) == noErr)
        {
            if(OSACompile(ci, &text, kOSAModeNull, &id) == noErr)
                OSADispose(ci, id);
            AEDisposeDesc(&text);
        }
        CloseComponent(ci);
    }
}

static void sound_output(void)
{
    ComponentDescription cd = { kSoundOutputDeviceType, 0, 0, 0, 0 };
    Component c = FindNextComponent(NULL, &cd);
    ComponentInstance ci;

    if(c && OpenAComponent(c, &ci) == noErr)
    {
        uint32_t volume;
        SoundInfoList rates;

        if(SoundComponentGetInfo(ci, NULL, siHardwareVolume, &volume) == noErr)
            (void)SoundComponentSetInfo(ci, NULL, siHardwareVolume, (void *)volume);
        if(SoundComponentGetInfo(ci, NULL, siSampleRateAvailable, &rates) == noErr)
            DisposeHandle(rates.infoHandle);
        CloseComponent(ci);
    }
}

static void misc(void)
{
    Rect r;
    int32_t ctx;
    int16_t len = 6;
    char text[] = "mvtest";
    Handle h = NewHandle(0);

    (void)GetMenuBarRect(&r);
    GetIconDrawContext(&ctx);
    (void)TruncText(40, text, &len, smTruncEnd);
    (void)NewGestaltValue('mvts', 1);
    (void)HMIdle();
    if (GetScrap(h, 'TEXT', &ctx) < 0)
        ZeroScrap();
    DisposeHandle(h);
}

int main(void)
{
    Rect bounds = { 60, 40, 260, 400 };
    WindowPtr w;
    EventRecord ev;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    w = NewWindow(NULL, &bounds, "\pMultiversal", true, documentProc,
                  (WindowPtr)-1, true, 0);
    SetPort(w);
    MoveTo(20, 30);
    DrawString("\pBuilt against Executor's definitions");

    layers();
    displays();
    files();
    dialogs((DialogPtr)w);
    components();
    sound_output();
    misc();

    while (!Button())
        (void)WaitNextEvent(everyEvent, &ev, 30, NULL);
    DisposeWindow(w);
    return 0;
}
