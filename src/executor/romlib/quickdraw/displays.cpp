#include <Displays.h>
#include <QuickDraw.h>
#include <WindowMgr.h>
#include <error/error.h>

using namespace Executor;

/* MacPhoenix: the parts of Display Manager 2.0 (Gestalt 'dplv' $00020006 on
   a real 7.5.5 boot) that Finder calls. One screen, no configuration
   changes, so no notifications are ever sent. */

OSErr Executor::C_DMRegisterNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn)
{
    return noErr;
}

OSErr Executor::C_DMRemoveNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn)
{
    return noErr;
}

GDHandle Executor::C_DMGetFirstScreenDevice(Boolean activeOnly)
{
    return GetDeviceList();
}

GDHandle Executor::C_DMGetNextScreenDevice(GDHandle theDevice, Boolean activeOnly)
{
    return theDevice ? GetNextDevice(theDevice) : nullptr;
}

void Executor::C_DMDrawDesktopRegion(RgnHandle globalRgn)
{
    PaintOne(nullptr, globalRgn);
}

void Executor::C_DMDrawDesktopRect(Rect *globalRect)
{
    RgnHandle rgn = NewRgn();
    RectRgn(rgn, globalRect);
    PaintOne(nullptr, rgn);
    DisposeRgn(rgn);
}

OSErr Executor::C_DMGetDeskRegion(GUEST<RgnHandle> *desktopRegion)
{
    *desktopRegion = LM(GrayRgn);
    return noErr;
}

OSErr Executor::C_DMKeepWindowOnScreen(WindowPtr window, RgnHandle rgn)
{
    return noErr;
}

Boolean Executor::C_DMPlacesWindow(WindowPtr window)
{
    return false;
}

void Executor::C_DMDesktopIconRgnChanged()
{
}
