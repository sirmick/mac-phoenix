/* MacPhoenix: the Window Manager of Mac OS 8 (MacWindows.h; Window Manager
 * 2.0, WindowsLib, 8.5, and the window calls of AppearanceLib), for 8.x
 * Systems' Finder and window definitions (docs/executor/Appearance.md).
 * What a window has beyond its WindowRecord (properties, proxy, modified
 * state) is kept here, by window, until CloseWindow. */

#include <base/common.h>
#include <WindowMgr.h>
#include <WindowMgr8.h>
#include <ResourceMgr.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <map>
#include <vector>

using namespace Executor;

namespace
{
struct WindowExtras
{
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> properties;
    bool modified = false;
    bool hasProxy = false;
    FSSpec proxyFile;
    IconRef proxyIcon = nullptr;
};

/* The creator of the system properties (Get/SetWindowSystemProperty): no
   application's, a property of the window definitions'. */
constexpr uint32_t systemCreator = 0;

std::map<WindowPtr, WindowExtras>& extrasTable()
{
    static std::map<WindowPtr, WindowExtras> table;
    return table;
}

WindowExtras& extras(WindowPtr w)
{
    return extrasTable()[w];
}

WindowExtras *findExtras(WindowPtr w)
{
    auto it = extrasTable().find(w);
    return it == extrasTable().end() ? nullptr : &it->second;
}

OSStatus getProperty(WindowPtr w, uint32_t creator, uint32_t tag,
                     UInt32 size, GUEST<UInt32> *actual, void *buffer)
{
    if(!w)
        return errInvalidWindowPtr;
    WindowExtras *x = findExtras(w);
    if(!x)
        return errWindowPropertyNotFound;
    auto it = x->properties.find({ creator, tag });
    if(it == x->properties.end())
        return errWindowPropertyNotFound;
    if(actual)
        *actual = it->second.size();
    if(buffer)
        memcpy(buffer, it->second.data(), std::min<size_t>(size, it->second.size()));
    return noErr;
}

OSStatus setProperty(WindowPtr w, uint32_t creator, uint32_t tag,
                     UInt32 size, const void *buffer)
{
    if(!w)
        return errInvalidWindowPtr;
    auto& v = extras(w).properties[{ creator, tag }];
    v.assign((const uint8_t *)buffer, (const uint8_t *)buffer + (buffer ? size : 0));
    return noErr;
}

OSStatus removeProperty(WindowPtr w, uint32_t creator, uint32_t tag)
{
    if(!w)
        return errInvalidWindowPtr;
    WindowExtras *x = findExtras(w);
    if(!x || !x->properties.erase({ creator, tag }))
        return errWindowPropertyNotFound;
    return noErr;
}

/* The window's 'WDEF' ID and variant: procID = ID * 16 + variant. */
int16_t windowDefID(WindowPtr w)
{
    GUEST<INTEGER> id;
    GUEST<ResType> type;
    Str255 name;
    GetResInfo(WINDOW_DEF_PROC(w), &id, &type, name);
    return ResError() == noErr ? (int16_t)id : 0;
}

/* The features and attributes of a window made from a procID (NewWindow,
   GetNewWindow): MacWindows.h's procIDs, the variant naming what the
   frame has. */
void procIDTraits(WindowPtr w, UInt32 *features, WindowAttributes *attributes)
{
    int16_t id = windowDefID(w);
    int16_t var = GetWVariant(w);
    UInt32 f = 0;
    WindowAttributes a = WINDOW_GO_AWAY_FLAG(w) ? kWindowCloseBoxAttribute : 0;
    switch(id)
    {
        case 0: /* System 7's: documentProc, dBoxProc, ..., zoomDocProc */
        case 1: /* rDocProc */
            if(id == 0 && (var == documentProc || var == zoomDocProc))
                f |= kWindowCanGrow;
            if(id == 0 && (var & ZOOMBIT) && var != dBoxProc + ZOOMBIT)
                f |= kWindowCanZoom;
            if(id == 0 && (var == dBoxProc || var == movableDBoxProc))
                f |= kWindowIsModal;
            if(id == 1 || var == documentProc || var == noGrowDocProc
               || var == zoomDocProc || var == zoomNoGrow || var == movableDBoxProc)
                f |= kWindowHasTitleBar;
            if(id == 0 && var != dBoxProc && var != plainDBox && var != altDBoxProc
               && var != movableDBoxProc)
                f |= kWindowCanCollapse;
            break;
        case 64: /* kWindowDocumentProc + grow 1, vertical zoom 2, horizontal 4 */
            f |= kWindowHasTitleBar | kWindowCanCollapse | kWindowCanGetWindowRegion;
            if(var & 1)
                f |= kWindowCanGrow;
            if(var & 6)
                f |= kWindowCanZoom;
            if(var & 2)
                a |= kWindowVerticalZoomAttribute;
            if(var & 4)
                a |= kWindowHorizontalZoomAttribute;
            break;
        case 65: /* dialogs: plain, dialog, movable dialog, alert, movable alert, ... */
            f |= kWindowIsModal | kWindowCanGetWindowRegion;
            if(var == 2 || var == 4 || var == 5)
                f |= kWindowHasTitleBar;
            if(var == 3 || var == 4)
                f |= kWindowIsAlert;
            if(var == 5)
                f |= kWindowCanGrow;
            break;
        case 66: /* floating: + grow 2, vertical zoom 4, horizontal 8 */
        case 67: /* side floating */
            f |= kWindowHasTitleBar | kWindowCanCollapse | kWindowCanGetWindowRegion;
            if(var & 2)
                f |= kWindowCanGrow;
            if(var & 12)
                f |= kWindowCanZoom;
            if(var & 4)
                a |= kWindowVerticalZoomAttribute;
            if(var & 8)
                a |= kWindowHorizontalZoomAttribute;
            if(id == 67)
                a |= kWindowSideTitlebarAttribute;
            break;
        default:
            f |= kWindowHasTitleBar;
            break;
    }
    if(id < 64 && (f & kWindowCanZoom))
        a |= kWindowFullZoomAttribute;
    if(f & kWindowCanGrow)
        a |= kWindowResizableAttribute;
    if(f & kWindowCanCollapse)
        a |= kWindowCollapseBoxAttribute;
    if(features)
        *features = f;
    if(attributes)
        *attributes = a;
}

/* The window's content rectangle, global. */
Rect globalContent(WindowPtr w)
{
    Rect r = PORT_RECT(w);
    OffsetRect(&r, -PORT_BOUNDS(w).left, -PORT_BOUNDS(w).top);
    return r;
}
}

void Executor::ROMlib_window8_forget(WindowPtr w)
{
    extrasTable().erase(w);
}

OSStatus Executor::C_TransitionWindow(WindowPtr window, WindowTransitionEffect effect,
                                      WindowTransitionAction action, const Rect *rect)
{
    /* No animation: the zoom rectangles a Mac draws from rect to the window
       are a flourish. */
    if(!window)
        return errInvalidWindowPtr;
    switch(action)
    {
        case kWindowShowTransitionAction:
            ShowWindow(window);
            return noErr;
        case kWindowHideTransitionAction:
            HideWindow(window);
            return noErr;
        default:
            return paramErr;
    }
}

OSStatus Executor::C_GetWindowFeatures(WindowPtr window, GUEST<UInt32> *outFeatures)
{
    if(!window)
        return errInvalidWindowPtr;
    /* An 8.x window definition answers kWindowMsgGetFeatures with 1 and the
       features in the parameter's long. */
    GUEST<UInt32> f = 0;
    if(WINDCALL(window, kWindowMsgGetFeatures, ptr_to_longint(&f)) == 1)
    {
        *outFeatures = f;
        return noErr;
    }
    UInt32 features;
    procIDTraits(window, &features, nullptr);
    *outFeatures = features;
    return noErr;
}

OSStatus Executor::C_GetWindowAttributes(WindowPtr window, GUEST<WindowAttributes> *outAttributes)
{
    if(!window)
        return errInvalidWindowPtr;
    WindowAttributes a;
    procIDTraits(window, nullptr, &a);
    *outAttributes = a;
    return noErr;
}

OSStatus Executor::C_GetWindowClass(WindowPtr window, GUEST<WindowClass> *outClass)
{
    if(!window)
        return errInvalidWindowPtr;
    int16_t id = windowDefID(window);
    UInt32 features;
    procIDTraits(window, &features, nullptr);
    WindowClass c = kDocumentWindowClass;
    if(id == 66 || id == 67)
        c = kFloatingWindowClass;
    else if(features & kWindowIsAlert)
        c = (features & kWindowHasTitleBar) ? kMovableAlertWindowClass : kAlertWindowClass;
    else if(features & kWindowIsModal)
        c = (features & kWindowHasTitleBar) ? kMovableModalWindowClass : kModalWindowClass;
    else if(id == 0 && GetWVariant(window) == plainDBox)
        c = kPlainWindowClass;
    *outClass = c;
    return noErr;
}

Boolean Executor::C_IsWindowInStandardState(WindowPtr window, GUEST<Point> *idealSize,
                                            Rect *idealStandardState)
{
    if(!window)
        return false;
    Rect std;
    Rect content = globalContent(window);
    if(idealSize)
    {
        /* The ideal size where the window is, kept on the screen (the main
           device below the menu bar, 3 pixels in, under the title bar). */
        Rect screen = PIXMAP_BOUNDS(GD_PMAP(LM(MainDevice)));
        screen.top += LM(MBarHeight) + 22;
        InsetRect(&screen, 3, 3);
        Point size = idealSize->get();
        std.left = content.left;
        std.top = content.top;
        std.right = std.left + std::min<int>(size.h, screen.right - screen.left);
        std.bottom = std.top + std::min<int>(size.v, screen.bottom - screen.top);
        if(std.right > screen.right)
            OffsetRect(&std, screen.right - std.right, 0);
        if(std.bottom > screen.bottom)
            OffsetRect(&std, 0, screen.bottom - std.bottom);
        if(std.left < screen.left)
            OffsetRect(&std, screen.left - std.left, 0);
        if(std.top < screen.top)
            OffsetRect(&std, 0, screen.top - std.top);
    }
    else if(WINDOW_DATA(window))
        std = (*(GUEST<WStateData *> *)WINDOW_DATA(window))->stdState;
    else
        return false;
    if(idealStandardState)
        *idealStandardState = std;
    return EqualRect(&std, &content);
}

OSStatus Executor::C_SetWindowModified(WindowPtr window, Boolean modified)
{
    if(!window)
        return errInvalidWindowPtr;
    bool was = extras(window).modified;
    extras(window).modified = modified;
    if(was != (bool)modified)
        WINDCALL(window, kWindowMsgModified, modified);
    return noErr;
}

Boolean Executor::C_IsWindowModified(WindowPtr window)
{
    WindowExtras *x = window ? findExtras(window) : nullptr;
    return x && x->modified;
}

OSStatus Executor::C_SetWindowProxyFSSpec(WindowPtr window, const FSSpec *inFile)
{
    if(!window || !inFile)
        return paramErr;
    extras(window).hasProxy = true;
    extras(window).proxyFile = *inFile;
    return noErr;
}

OSStatus Executor::C_GetWindowProxyFSSpec(WindowPtr window, FSSpec *outFile)
{
    WindowExtras *x = window ? findExtras(window) : nullptr;
    if(!x || !x->hasProxy)
        return errWindowDoesNotHaveProxy;
    *outFile = x->proxyFile;
    return noErr;
}

OSStatus Executor::C_SetWindowProxyIcon(WindowPtr window, IconRef icon)
{
    if(!window)
        return errInvalidWindowPtr;
    extras(window).proxyIcon = icon;
    return noErr;
}

OSStatus Executor::C_GetWindowProxyIcon(WindowPtr window, GUEST<IconRef> *outIcon)
{
    WindowExtras *x = window ? findExtras(window) : nullptr;
    if(!x || !x->proxyIcon)
        return errWindowDoesNotHaveProxy;
    *outIcon = x->proxyIcon;
    return noErr;
}

OSStatus Executor::C_RemoveWindowProxy(WindowPtr window)
{
    if(!window)
        return errInvalidWindowPtr;
    if(WindowExtras *x = findExtras(window))
    {
        x->hasProxy = false;
        x->proxyIcon = nullptr;
    }
    return noErr;
}

OSStatus Executor::C_GetWindowProperty(WindowPtr window, PropertyCreator propertyCreator,
                                       PropertyTag propertyTag, UInt32 bufferSize,
                                       GUEST<UInt32> *actualSize, void *propertyBuffer)
{
    return getProperty(window, propertyCreator, propertyTag, bufferSize,
                       actualSize, propertyBuffer);
}

OSStatus Executor::C_GetWindowPropertySize(WindowPtr window, PropertyCreator creator,
                                           PropertyTag tag, GUEST<UInt32> *size)
{
    return getProperty(window, creator, tag, 0, size, nullptr);
}

OSStatus Executor::C_SetWindowProperty(WindowPtr window, PropertyCreator propertyCreator,
                                       PropertyTag propertyTag, UInt32 propertySize,
                                       void *propertyBuffer)
{
    return setProperty(window, propertyCreator, propertyTag, propertySize, propertyBuffer);
}

OSStatus Executor::C_RemoveWindowProperty(WindowPtr window, PropertyCreator propertyCreator,
                                          PropertyTag propertyTag)
{
    return removeProperty(window, propertyCreator, propertyTag);
}

OSStatus Executor::C_GetWindowSystemProperty(WindowPtr window, PropertyTag tag,
                                             UInt32 bufferSize, GUEST<UInt32> *actualSize,
                                             void *buffer)
{
    return getProperty(window, systemCreator, tag, bufferSize, actualSize, buffer);
}

OSStatus Executor::C_SetWindowSystemProperty(WindowPtr window, PropertyTag tag,
                                             UInt32 size, const void *buffer)
{
    return setProperty(window, systemCreator, tag, size, buffer);
}

OSStatus Executor::C_RemoveWindowSystemProperty(WindowPtr window, PropertyTag tag)
{
    return removeProperty(window, systemCreator, tag);
}

Boolean Executor::C_WindowHasUnicodeTitle(WindowPtr window)
{
    return false;
}

OSStatus Executor::C_GetWindowTitleLength(WindowPtr window, GUEST<UInt32> *outLength)
{
    if(!window)
        return errInvalidWindowPtr;
    StringHandle title = WINDOW_TITLE(window);
    *outLength = title ? (*title)[0] : 0;
    return noErr;
}

OSStatus Executor::C_GetWindowGadgetDropThreshold(WindowPtr window, Boolean *outPassed)
{
    *outPassed = false;
    return noErr;
}

OSStatus Executor::C_GetWindowWidgetHilite(WindowPtr inWindow, GUEST<WindowDefPartCode> *outHilite)
{
    /* Executor tracks a box with the window definition's own wDraw
       messages; between them nothing is hilited. */
    *outHilite = 0;
    return noErr;
}

OSStatus Executor::C_EraseWindowRegion(WindowPtr window, RgnHandle region)
{
    if(!window)
        return errInvalidWindowPtr;
    RGBColor *colors = validate_colors_for_window(window);
    RGBColor saved;
    GetBackColor(&saved);
    RGBBackColor(&colors[wContentColor]);
    EraseRgn(region);
    RGBBackColor(&saved);
    return noErr;
}

OSStatus Executor::C_PostDelayedWindowReshape(WindowPtr window)
{
    /* The window definition asks for its regions to be recalculated
       later (its title changed width); Executor's are recalculated when
       the window is next drawn or moved. */
    return noErr;
}

Boolean Executor::C_IsValidWindowPtr(WindowPtr possibleWindow)
{
    for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
        if((WindowPtr)w == possibleWindow)
            return true;
    return false;
}

WindowPtr Executor::C_FrontNonFloatingWindow()
{
    for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
    {
        int16_t id = windowDefID((WindowPtr)w);
        if(WINDOW_VISIBLE(w) && id != 66 && id != 67)
            return (WindowPtr)w;
    }
    return nullptr;
}

OSStatus Executor::C_GetWindowBounds(WindowPtr window, WindowRegionCode regionCode,
                                     Rect *globalBounds)
{
    if(!window)
        return errInvalidWindowPtr;
    RgnHandle rgn = NewRgn();
    OSStatus err = C_GetWindowRegion(window, regionCode, rgn);
    if(err == noErr)
        *globalBounds = (*rgn)->rgnBBox;
    DisposeRgn(rgn);
    return err;
}
