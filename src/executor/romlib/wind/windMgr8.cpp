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
#include <base/functions.impl.h>
#include <algorithm>
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
    /* made by CreateNewWindow: its class and attributes as asked */
    bool created = false;
    WindowClass windowClass = 0;
    WindowAttributes attributes = 0;
    Rect idealUserState = { 0, 0, 0, 0 };
    bool collapsed = false;
    bool hasContentColor = false;
    RGBColor contentColor = { 0xFFFF, 0xFFFF, 0xFFFF };
    UInt32 ownerCount = 1;
    PixPatHandle contentPattern = nullptr; /* a copy */
    AliasHandle proxyAlias = nullptr;
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
    /* one that isn't there: noErr (8.6) */
    if(WindowExtras *x = findExtras(w))
        x->properties.erase({ creator, tag });
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

/* The window's frame on each side of its content (structure minus
   content, as GetWindowStructureWidths answers). */
Rect frameWidths(WindowPtr w)
{
    Rect s = (*WINDOW_STRUCT_REGION(w))->rgnBBox, c = (*WINDOW_CONT_REGION(w))->rgnBBox, r;
    r.top = c.top - s.top;
    r.left = c.left - s.left;
    r.bottom = s.bottom - c.bottom;
    r.right = s.right - c.right;
    return r;
}

/* Floating windows' state per process (InitFloatingWindows): by the
   process's A5 world. */
std::map<uint32_t, bool>& floatingProcesses()
{
    static std::map<uint32_t, bool> table; /* -> floating windows visible */
    return table;
}
}

/* MacPhoenix: the Appearance Manager's WDEFs 64-67 (procIDs 1024-1087,
   8.0), as Executor's definitions: 8.6's own draw their titles through
   Apple Type Services, which spins on Executor (MacOS8-9.md). The window
   keeps its WDEF and variant (GetWVariant, GetWindowFeatures answer as
   8.6's); drawing, hit testing and regions go to WDEF 0 with the variant
   meaning the same frame. */
LONGINT Executor::C_wdef64(INTEGER varcode, WindowPtr w, INTEGER message, LONGINT param)
{
    switch(windowDefID(w))
    {
        case 64: /* + grow 1, vertical zoom 2, horizontal zoom 4 */
        {
            ROMlib_wdef_grow_in_frame = true;
            LONGINT r = C_wdef0(((varcode & 6) ? zoomDocProc : documentProc) | ((varcode & 1) ? 0 : noGrowDocProc),
                                w, message, param);
            ROMlib_wdef_grow_in_frame = false;
            return r;
        }
        case 65: /* plain, shadow, modal, movable modal, alert, movable alert, movable modal grow */
        {
            static const INTEGER dialog[] = { plainDBox, altDBoxProc, dBoxProc, movableDBoxProc,
                                              dBoxProc, movableDBoxProc, movableDBoxProc };
            INTEGER classic = dialog[std::min<int>(varcode & 15, 6)];
            ROMlib_wdef_thin_dialog = classic == dBoxProc || classic == movableDBoxProc;
            LONGINT r = C_wdef0(classic, w, message, param);
            ROMlib_wdef_thin_dialog = false;
            return r;
        }
        default: /* 66, 67: floating + grow 2, vertical zoom 4, horizontal zoom 8 */
            /* (not WDEF 124: on 8.x it hands floating windows to WDEF 66)
               a document frame until Executor has a floating one */
            return C_wdef0(((varcode & 12) ? zoomDocProc : documentProc) | ((varcode & 2) ? 0 : noGrowDocProc),
                           w, message, param);
    }
}

static bool floating_window_p(WindowPtr w);

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
    /* A window CreateNewWindow made: 8.6's platinum definition's features
       for its class, growing and zooming as its attributes say (measured:
       tests/guest/difftest wind) */
    if(WindowExtras *x = findExtras(window); x && x->created)
    {
        static const UInt32 base[] = { 0, 0x1a38, 0x1a78, 0x1a18, 0x1a58, 0x1a54, 0x1fd4 };
        UInt32 f = x->windowClass < 7 ? base[x->windowClass] : 0;
        if(x->attributes & kWindowResizableAttribute)
            f |= kWindowCanGrow;
        if(x->attributes & kWindowFullZoomAttribute)
            f |= kWindowCanZoom;
        *outFeatures = f;
        return noErr;
    }
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
    if(WindowExtras *x = findExtras(window); x && x->created)
    {
        *outAttributes = x->attributes;
        return noErr;
    }
    /* a window of a procID: paramErr (8.6), which keeps attributes only
       for CreateNewWindow's */
    if(int16_t id = windowDefID(window); id < 64 || id > 67)
    {
        *outAttributes = 0;
        return paramErr;
    }
    WindowAttributes a;
    procIDTraits(window, nullptr, &a);
    *outAttributes = a;
    return noErr;
}

OSStatus Executor::C_GetWindowClass(WindowPtr window, GUEST<WindowClass> *outClass)
{
    if(!window)
        return errInvalidWindowPtr;
    /* 8.6 answers kDocumentWindowClass for every class CreateNewWindow
       makes (tests/guest/difftest wind) */
    if(WindowExtras *x = findExtras(window); x && x->created)
    {
        *outClass = kDocumentWindowClass;
        return noErr;
    }
    int16_t id = windowDefID(window);
    /* a window of a classic procID: kDocumentWindowClass, whatever its
       definition (8.6) */
    if(id < 64)
    {
        *outClass = kDocumentWindowClass;
        return noErr;
    }
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
        if(WINDOW_VISIBLE(w) && id != 66 && id != 67 && !floating_window_p((WindowPtr)w))
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

/* ── Window Manager 2.0's creation, bounds and states (8.5) ─────────────── */

/* A window of a class (MacWindows.h), as 8.6 makes them: the alert and
   modal classes take no attributes (errUnsupportedWindowAttributesForClass),
   only these classes exist (errUnrecognizedWindowClass for the rest), the
   attributes kept as asked. Drawn by Executor's own definitions (document
   variants, WDEF 124's floating ones, dialog boxes): 8.6's platinum WDEFs
   64-67 draw titles through Apple Type Services, which doesn't work on
   Executor yet (Date & Time, MacOS8-9.md). */
OSStatus Executor::C_CreateNewWindow(WindowClass windowClass, WindowAttributes attributes,
                                     const Rect *contentBounds, GUEST<WindowRef> *outWindow)
{
    if(!contentBounds || !outWindow)
        return paramErr;
    bool resizable = attributes & kWindowResizableAttribute;
    bool zoom = attributes & kWindowFullZoomAttribute;
    INTEGER procID;
    switch(windowClass)
    {
        case kAlertWindowClass:
        case kModalWindowClass:
        case kMovableAlertWindowClass:
        case kMovableModalWindowClass:
            if(attributes)
                return errUnsupportedWindowAttributesForClass;
            procID = windowClass == kMovableAlertWindowClass || windowClass == kMovableModalWindowClass
                ? movableDBoxProc : dBoxProc;
            break;
        case kFloatingWindowClass:
            procID = 1984 + (resizable ? 1 : 0) + (zoom ? 2 : 0); /* WDEF 124 */
            break;
        case kDocumentWindowClass:
            procID = resizable ? (zoom ? zoomDocProc : documentProc) : (zoom ? zoomNoGrow : noGrowDocProc);
            break;
        default:
            return errUnrecognizedWindowClass;
    }
    WindowPtr w = NewCWindow(nullptr, contentBounds, (StringPtr) "", false, procID, (WindowPtr)-1,
                             (attributes & kWindowCloseBoxAttribute) != 0, 0);
    if(!w)
        return memFullErr;
    WindowExtras &x = extras(w);
    x.created = true;
    x.windowClass = windowClass;
    x.attributes = attributes;
    *outWindow = w;
    return noErr;
}

OSStatus Executor::C_GetWindowStructureWidths(WindowRef inWindow, Rect *outRect)
{
    if(!inWindow || !outRect)
        return paramErr;
    *outRect = frameWidths(inWindow);
    return noErr;
}

/* The content or the structure where the rectangle says, sized to it. */
OSStatus Executor::C_SetWindowBounds(WindowRef window, WindowRegionCode regionCode, const Rect *globalBounds)
{
    if(!window || !globalBounds)
        return paramErr;
    Rect c = *globalBounds;
    if(regionCode == kWindowStructureRgn)
    {
        Rect fw = frameWidths(window);
        c.top += fw.top;
        c.left += fw.left;
        c.bottom -= fw.bottom;
        c.right -= fw.right;
    }
    else if(regionCode != kWindowContentRgn)
        return errWindowRegionCodeInvalid;
    MoveWindow(window, c.left, c.top, false);
    SizeWindow(window, c.right - c.left, c.bottom - c.top, true);
    return noErr;
}

OSStatus Executor::C_MoveWindowStructure(WindowRef window, short hGlobal, short vGlobal)
{
    if(!window)
        return paramErr;
    Rect fw = frameWidths(window);
    MoveWindow(window, hGlobal + fw.left, vGlobal + fw.top, false);
    return noErr;
}

/* Centred (in the screen below the menu bar, or on the parent), cascaded
   or where an alert goes (a third of the way down); a method that needs
   a parent without one, paramErr (8.6). */
OSStatus Executor::C_RepositionWindow(WindowRef window, WindowRef parentWindow, WindowPositionMethod method)
{
    if(!window)
        return paramErr;
    Rect area = (*GD_PMAP(LM(MainDevice)))->bounds;
    area.top += LM(MBarHeight);
    bool on_parent = method == kWindowCenterOnParentWindow || method == kWindowCenterOnParentWindowScreen
        || method == kWindowAlertPositionOnParentWindow || method == kWindowAlertPositionOnParentWindowScreen
        || method == 5 /* kWindowCascadeOnParentWindow */ || method == 6;
    if(on_parent)
    {
        if(!parentWindow)
            return paramErr;
        if(method == kWindowCenterOnParentWindow || method == kWindowAlertPositionOnParentWindow
           || method == 5)
            area = (*WINDOW_STRUCT_REGION(parentWindow))->rgnBBox;
    }
    Rect s = (*WINDOW_STRUCT_REGION(window))->rgnBBox;
    int w = s.right - s.left, h = s.bottom - s.top;
    int left = area.left + (area.right - area.left - w) / 2, top;
    switch(method)
    {
        case kWindowCenterOnMainScreen:
        case kWindowCenterOnParentWindow:
        case kWindowCenterOnParentWindowScreen:
            top = area.top + (area.bottom - area.top - h) / 2;
            break;
        case kWindowAlertPositionOnMainScreen:
        case kWindowAlertPositionOnParentWindow:
        case kWindowAlertPositionOnParentWindowScreen:
            top = area.top + (area.bottom - area.top - h) / 3;
            break;
        case kWindowCascadeOnMainScreen:
        case 5:
        case 6:
            left = area.left + 10;
            top = area.top + 10;
            break;
        default:
            return paramErr;
    }
    return C_MoveWindowStructure(window, left, top);
}

OSStatus Executor::C_GetWindowIdealUserState(WindowRef window, Rect *userState)
{
    if(!window || !userState)
        return paramErr;
    WindowExtras *x = findExtras(window);
    *userState = x ? x->idealUserState : Rect{ 0, 0, 0, 0 };
    return noErr;
}

OSStatus Executor::C_SetWindowIdealUserState(WindowRef window, const Rect *userState)
{
    if(!window || !userState)
        return paramErr;
    extras(window).idealUserState = *userState;
    return noErr;
}

/* Kept, not drawn: Executor's definitions don't shade a window yet. */
OSStatus Executor::C_CollapseWindow(WindowRef window, Boolean collapse)
{
    if(!window)
        return errInvalidWindowPtr;
    extras(window).collapsed = collapse;
    return noErr;
}

bool Executor::ROMlib_window8_collapsed(WindowPtr w)
{
    WindowExtras *x = findExtras(w);
    return x && x->collapsed;
}

OSStatus Executor::C_SetWindowContentColor(WindowRef window, const RGBColor *color)
{
    if(!window || !color)
        return paramErr;
    WindowExtras &x = extras(window);
    x.hasContentColor = true;
    x.contentColor = *color;
    return noErr;
}

/* None set: errWindowPropertyNotFound and white (8.6) */
OSStatus Executor::C_GetWindowContentColor(WindowRef window, RGBColor *color)
{
    if(!window || !color)
        return paramErr;
    WindowExtras *x = findExtras(window);
    *color = x && x->hasContentColor ? x->contentColor : RGBColor{ 0xFFFF, 0xFFFF, 0xFFFF };
    return x && x->hasContentColor ? noErr : errWindowPropertyNotFound;
}

/* The floating window calls need InitFloatingWindows first
   (errFloatingWindowsNotInitialized, 8.6); their windows are the floating
   class's and WDEF 124's. */
static bool floating_window_p(WindowPtr w)
{
    WindowExtras *x = findExtras(w);
    return (x && x->created && x->windowClass == kFloatingWindowClass) || windowDefID(w) == 124;
}

static uint32_t process_key()
{
    return guest_cast<uint32_t>(LM(CurrentA5));
}

OSStatus Executor::C_InitFloatingWindows()
{
    floatingProcesses()[process_key()] = true;
    return noErr;
}

static OSStatus show_hide_floating(bool show)
{
    auto it = floatingProcesses().find(process_key());
    if(it == floatingProcesses().end())
        return errFloatingWindowsNotInitialized;
    it->second = show;
    for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
        if(floating_window_p((WindowPtr)w))
            ShowHide((WindowPtr)w, show);
    return noErr;
}

OSStatus Executor::C_HideFloatingWindows()
{
    return show_hide_floating(false);
}

OSStatus Executor::C_ShowFloatingWindows()
{
    return show_hide_floating(true);
}

Boolean Executor::C_AreFloatingWindowsVisible()
{
    auto it = floatingProcesses().find(process_key());
    return it == floatingProcesses().end() || it->second;
}

/* A proxy of no file yet: the window keeps none (8.6: GetWindowProxyFSSpec
   still errWindowDoesNotHaveProxy). */
OSStatus Executor::C_SetWindowProxyCreatorAndType(WindowRef window, OSType fileCreator, OSType fileType,
                                                  SInt16 vRefNum)
{
    return window ? noErr : errInvalidWindowPtr;
}

/* The parts of Executor's document frame (windDocdef.cpp's geometry: a
   19-pixel title bar, the close and zoom boxes 9 pixels in, the grow box
   the content's bottom right 16), global; false for a window without
   them. */
bool Executor::ROMlib_window8_part_region(WindowPtr w, WindowRegionCode code, RgnHandle rgn, bool boxes)
{
    if(windowDefID(w) != 0)
        return false;
    int16_t var = GetWVariant(w) & ~ZOOMBIT;
    bool title = var == documentProc || var == noGrowDocProc || var == movableDBoxProc;
    bool grow = var == documentProc;
    bool zoom = (GetWVariant(w) & ZOOMBIT) && var != dBoxProc;
    Rect c = globalContent(w), r;
    /* 8.6 answers the title bar, the title (when there is one) and the drag
       region; the boxes only inside, for the drag region */
    if(code != kWindowTitleBarRgn && code != kWindowTitleTextRgn && code != kWindowDragRgn && !boxes)
        return false;
    switch(code)
    {
        case kWindowTitleBarRgn:
        case kWindowDragRgn:
            if(!title)
                return false;
            SetRect(&r, c.left - 1, c.top - 19, c.right + 1, c.top);
            RectRgn(rgn, &r);
            if(code == kWindowDragRgn)
            {
                RgnHandle box = NewRgn();
                for(WindowRegionCode b : { kWindowCloseBoxRgn, kWindowZoomBoxRgn })
                    if(ROMlib_window8_part_region(w, b, box, true))
                        DiffRgn(rgn, box, rgn);
                DisposeRgn(box);
            }
            return true;
        case kWindowTitleTextRgn:
        {
            if(!title || !WINDOW_TITLE(w) || !(*WINDOW_TITLE(w))[0])
                return false;
            int width = WINDOW_TITLE_WIDTH(w), mid = (c.left + c.right) / 2;
            SetRect(&r, mid - width / 2, c.top - 16, mid + (width + 1) / 2, c.top - 3);
            RectRgn(rgn, &r);
            return true;
        }
        case kWindowCloseBoxRgn:
            if(!title || !WINDOW_GO_AWAY_FLAG(w))
                return false;
            SetRect(&r, c.left + 9, c.top - 15, c.left + 20, c.top - 4);
            RectRgn(rgn, &r);
            return true;
        case kWindowZoomBoxRgn:
            if(!title || !zoom)
                return false;
            SetRect(&r, c.right - 19, c.top - 15, c.right - 8, c.top - 4);
            RectRgn(rgn, &r);
            return true;
        case kWindowGrowRgn:
            if(!grow)
                return false;
            SetRect(&r, c.right - 16, c.bottom - 16, c.right, c.bottom);
            RectRgn(rgn, &r);
            return true;
        default:
            return false;
    }
}

bool Executor::ROMlib_window8_floating_class(WindowPtr w)
{
    WindowExtras *x = findExtras(w);
    return x && x->created && x->windowClass == kFloatingWindowClass;
}

/* ── the rest of WindowsLib's calls ─────────────────────────────────── */

/* CloneWindow's count (DisposeWindow doesn't consult it yet) */
OSStatus Executor::C_GetWindowOwnerCount(WindowRef window, GUEST<UInt32> *outCount)
{
    if(!window || !outCount)
        return paramErr;
    WindowExtras *x = findExtras(window);
    *outCount = x ? x->ownerCount : 1;
    return noErr;
}

OSStatus Executor::C_CloneWindow(WindowRef window)
{
    if(!window)
        return errInvalidWindowPtr;
    extras(window).ownerCount++;
    return noErr;
}

OSStatus Executor::C_SetWindowContentPattern(WindowRef window, PixPatHandle pixPat)
{
    if(!window || !pixPat)
        return paramErr;
    WindowExtras &x = extras(window);
    if(!x.contentPattern)
        x.contentPattern = NewPixPat();
    if(!x.contentPattern)
        return memFullErr;
    CopyPixPat(pixPat, x.contentPattern);
    return noErr;
}

/* None set: errWindowPropertyNotFound, as for the content colour */
OSStatus Executor::C_GetWindowContentPattern(WindowRef window, PixPatHandle outPixPat)
{
    if(!window || !outPixPat)
        return paramErr;
    WindowExtras *x = findExtras(window);
    if(!x || !x->contentPattern)
        return errWindowPropertyNotFound;
    CopyPixPat(x->contentPattern, outPixPat);
    return noErr;
}

OSStatus Executor::C_SetWindowProxyAlias(WindowRef window, AliasHandle alias)
{
    if(!window || !alias)
        return paramErr;
    WindowExtras &x = extras(window);
    GUEST<Handle> copy = (Handle)alias;
    if(HandToHand(&copy) != noErr)
        return memFullErr;
    x.proxyAlias = (AliasHandle)copy.get();
    x.hasProxy = true;
    return noErr;
}

OSStatus Executor::C_GetWindowProxyAlias(WindowRef window, GUEST<AliasHandle> *alias)
{
    if(!window || !alias)
        return paramErr;
    WindowExtras *x = findExtras(window);
    if(!x || !x->proxyAlias)
        return errWindowDoesNotHaveProxy;
    GUEST<Handle> copy = (Handle)x->proxyAlias;
    if(HandToHand(&copy) != noErr)
        return memFullErr;
    *alias = (AliasHandle)copy.get();
    return noErr;
}

static bool has_proxy(WindowRef window)
{
    WindowExtras *x = findExtras(window);
    return x && x->hasProxy;
}

/* Proxy icons aren't dragged yet: with none, errWindowDoesNotHaveProxy;
   with one, Track answers errUserWantsToDragWindow (the caller drags the
   window as usual), Begin/End paramErr. */
OSStatus Executor::C_BeginWindowProxyDrag(WindowRef window, GUEST<DragReference> *outNewDrag,
                                          RgnHandle outDragOutlineRgn)
{
    if(!window)
        return errInvalidWindowPtr;
    return has_proxy(window) ? paramErr : errWindowDoesNotHaveProxy;
}

OSStatus Executor::C_EndWindowProxyDrag(WindowRef window, DragReference theDrag)
{
    if(!window)
        return errInvalidWindowPtr;
    return has_proxy(window) ? paramErr : errWindowDoesNotHaveProxy;
}

OSStatus Executor::C_TrackWindowProxyFromExistingDrag(WindowRef window, Point startPt, DragReference drag,
                                                      RgnHandle inDragOutlineRgn)
{
    if(!window)
        return errInvalidWindowPtr;
    return has_proxy(window) ? errUserWantsToDragWindow : errWindowDoesNotHaveProxy;
}

OSStatus Executor::C_TrackWindowProxyDrag(WindowRef window, Point startPt)
{
    if(!window)
        return errInvalidWindowPtr;
    return has_proxy(window) ? errUserWantsToDragWindow : errWindowDoesNotHaveProxy;
}

/* No path pop-up menu yet: a click in the title is never one */
Boolean Executor::C_IsWindowPathSelectClick(WindowRef window, const EventRecord *event)
{
    return false;
}

OSStatus Executor::C_WindowPathSelect(WindowRef window, MenuRef menu, GUEST<SInt32> *outMenuResult)
{
    if(!window)
        return errInvalidWindowPtr;
    if(outMenuResult)
        *outMenuResult = 0;
    return has_proxy(window) ? -128 /* userCanceledErr */ : errWindowDoesNotHaveProxy;
}

/* Kept, not drawn (Executor's definitions have no drag highlight) */
OSStatus Executor::C_HiliteWindowFrameForDrag(WindowRef window, Boolean hilited)
{
    return window ? noErr : errInvalidWindowPtr;
}

/* GrowWindow and SizeWindow; the new content rectangle, global */
Boolean Executor::C_ResizeWindow(WindowRef window, Point startPoint, const Rect *sizeConstraints,
                                 Rect *newContentRect)
{
    if(!window)
        return false;
    Rect limits = { 0, 0, 32767, 32767 };
    if(sizeConstraints)
        limits = *sizeConstraints;
    LONGINT size = GrowWindow(window, startPoint, &limits);
    if(size)
        SizeWindow(window, size & 0xFFFF, (size >> 16) & 0xFFFF, true);
    if(newContentRect)
        *newContentRect = globalContent(window);
    return size != 0;
}

/* Out: to the ideal size at the content's top left (IsWindowInStandardState's
   standard state), the user state kept; in: back to the user state. */
OSStatus Executor::C_ZoomWindowIdeal(WindowRef window, WindowPartCode partCode, GUEST<Point> *ioIdealSize)
{
    if(!window || !ioIdealSize)
        return paramErr;
    WindowExtras &x = extras(window);
    Rect target;
    if(partCode == inZoomOut)
    {
        x.idealUserState = globalContent(window);
        C_IsWindowInStandardState(window, ioIdealSize, &target);
    }
    else if(partCode == inZoomIn)
    {
        target = x.idealUserState;
        if(EmptyRect(&target))
            return noErr;
    }
    else
        return paramErr;
    C_SetWindowBounds(window, kWindowContentRgn, &target);
    return noErr;
}

Handle Executor::C_GetWindowDataHandle(WindowRef window)
{
    return window ? (Handle)WINDOW_DATA(window) : nullptr;
}

void Executor::C_SetWindowDataHandle(WindowRef window, Handle data)
{
    if(window)
        WINDOW_DATA(window) = data;
}

Boolean Executor::C_GetWindowZoomFlag(WindowRef window)
{
    return window && WINDOW_SPARE_FLAG(window);
}

void Executor::C_GetWindowStructureRgn(WindowRef window, RgnHandle r)
{
    if(window && r)
        CopyRgn(WINDOW_STRUCT_REGION(window), r);
}

void Executor::C_GetWindowContentRgn(WindowRef window, RgnHandle r)
{
    if(window && r)
        CopyRgn(WINDOW_CONT_REGION(window), r);
}

void Executor::C_GetWindowUpdateRgn(WindowRef window, RgnHandle r)
{
    if(window && r)
        CopyRgn(WINDOW_UPDATE_REGION(window), r);
}

SInt16 Executor::C_GetWindowTitleWidth(WindowRef window)
{
    return window ? (SInt16)WINDOW_TITLE_WIDTH(window) : 0;
}
