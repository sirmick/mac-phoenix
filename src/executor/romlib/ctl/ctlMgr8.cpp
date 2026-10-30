/* MacPhoenix: the Control Manager of Mac OS 8 (Controls.h; AppearanceLib's
 * calls, _ControlDispatch, and ControlsLib's, 8.5), for 8.x Systems'
 * Finder and control definitions (docs/executor/Appearance.md): embedding,
 * the 8.0 messages (features, control data, backgrounds, tracking, focus),
 * 32-bit values, view sizes and properties.
 *
 * What a control has beyond its ControlRecord is kept here, by control,
 * until it is disposed of (or its window closed). The 32-bit values stay in
 * step with the record's 16-bit ones: a 32-bit value counts while the
 * record still holds what it was pinned to; anything that sets the 16-bit
 * one (SetControlValue, a CDEF writing contrlValue) takes over. */

#include <base/common.h>
#include <WindowMgr.h>
#include <ControlMgr.h>
#include <ControlMgr8.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <ctl/ctl.h>
#include <wind/wind.h>
#include <quickdraw/cquick.h>
#include <base/functions.impl.h>
#include <algorithm>
#include <map>
#include <vector>

using namespace Executor;

namespace
{
/* ControlDefinitions.h's: the user pane, CDEF 16, the root control's kind. */
constexpr INTEGER kControlUserPaneProc = 256;

struct Value32
{
    int32_t value = 0;
    int16_t pinned = 0; /* what the record's field was set to */
    bool set = false;
};

struct ControlExtras
{
    Value32 value, minimum, maximum;
    int32_t viewSize = 0;
    ControlHandle super = nullptr;
    std::vector<ControlHandle> subs;
    bool latentVisible = false; /* shown again with its embedder */
    bool hasFont = false;
    ControlFontStyleRec font;
    bool featuresKnown = false;
    UInt32 features = 0;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> properties;
};

std::map<ControlHandle, ControlExtras>& extrasTable()
{
    static std::map<ControlHandle, ControlExtras> table;
    return table;
}

std::map<WindowPtr, ControlHandle>& roots()
{
    static std::map<WindowPtr, ControlHandle> table;
    return table;
}

std::map<WindowPtr, ControlHandle>& focus()
{
    static std::map<WindowPtr, ControlHandle> table;
    return table;
}

ControlExtras& extras(ControlHandle c)
{
    return extrasTable()[c];
}

ControlExtras *findExtras(ControlHandle c)
{
    auto it = extrasTable().find(c);
    return it == extrasTable().end() ? nullptr : &it->second;
}

ControlHandle rootOf(WindowPtr w)
{
    auto it = roots().find(w);
    return it == roots().end() ? nullptr : it->second;
}

int16_t pin16(int32_t v)
{
    return std::clamp<int32_t>(v, -32768, 32767);
}

int32_t get32(const Value32& v, int16_t field)
{
    return v.set && v.pinned == field ? v.value : field;
}

void set32(Value32& v, GUEST<INTEGER>& field, int32_t value)
{
    v.value = value;
    v.pinned = pin16(value);
    v.set = true;
    field = v.pinned;
}

/* Whether the control's definition takes the 8.0 messages. Asked from
   8.0 on only: a 7.x Control Manager never sends them, and an old
   definition may not ignore what it doesn't know. */
bool newMessages(ControlHandle c)
{
    return (uint16_t)LM(SysVersion) >= 0x0800
           && (uint32_t)CTLCALL(c, kControlMsgTestNewMsgSupport, 0) == kControlSupportsNewMessages;
}

UInt32 features(ControlHandle c)
{
    ControlExtras& x = extras(c);
    if(!x.featuresKnown)
    {
        x.featuresKnown = true;
        x.features = newMessages(c) ? CTLCALL(c, kControlMsgGetFeatures, 0) : 0;
    }
    return x.features;
}

/* Sends an 8.0 message to a control whose definition knows them. */
int32_t message8(ControlHandle c, int16_t msg, int32_t param)
{
    return newMessages(c) ? CTLCALL(c, msg, param) : 0;
}

void detach(ControlHandle c)
{
    ControlExtras *x = findExtras(c);
    if(!x || !x->super)
        return;
    ControlHandle super = x->super;
    auto& subs = extras(super).subs;
    subs.erase(std::remove(subs.begin(), subs.end(), c), subs.end());
    x->super = nullptr;
    if(features(super) & kControlSupportsEmbedding)
    {
        CtlCallGuard guard(super);
        message8(super, kControlMsgSubControlRemoved, ptr_to_longint(c));
    }
}

/* The deepest visible control at p under (and including) c; with a part
   code in *part when a definition claims the point. */
ControlHandle deepestAt(ControlHandle c, Point p, bool activeOnly, int16_t *part)
{
    if(!CTL_VIS(c) || !PtInRect(p, &CTL_RECT(c)))
        return nullptr;
    ControlExtras *x = findExtras(c);
    if(x)
        for(auto it = x->subs.rbegin(); it != x->subs.rend(); ++it)
            if(ControlHandle hit = deepestAt(*it, p, activeOnly, part))
                return hit;
    if(activeOnly && CTL_HILITE(c) == INACTIVE)
        return nullptr;
    int16_t pc = TestControl(c, p);
    if(pc)
    {
        *part = pc;
        return c;
    }
    return nullptr;
}

/* Every control of the window that takes the keyboard focus, in embedding
   order. */
void focusable(ControlHandle c, std::vector<ControlHandle>& out)
{
    if(!CTL_VIS(c) || CTL_HILITE(c) == INACTIVE)
        return;
    if(features(c) & kControlSupportsFocus)
        out.push_back(c);
    if(ControlExtras *x = findExtras(c))
        for(ControlHandle s : x->subs)
            focusable(s, out);
}

std::vector<ControlHandle> focusOrder(WindowPtr w)
{
    std::vector<ControlHandle> out;
    if(ControlHandle root = rootOf(w))
        focusable(root, out);
    else
    {
        std::vector<ControlHandle> list;
        for(ControlHandle c = WINDOW_CONTROL_LIST(w); c; c = CTL_NEXT_CONTROL(c))
            list.push_back(c);
        for(auto it = list.rbegin(); it != list.rend(); ++it)
            focusable(*it, out);
    }
    return out;
}

OSErr moveFocus(WindowPtr w, int16_t part)
{
    std::vector<ControlHandle> order = focusOrder(w);
    if(order.empty())
        return errCouldntSetFocus;
    if(part == kControlFocusPrevPart)
        std::reverse(order.begin(), order.end());
    ControlHandle current = focus()[w];
    auto it = std::find(order.begin(), order.end(), current);
    if(it != order.end())
    {
        /* the focused control's next part first */
        CtlCallGuard guard(current);
        if(CTLCALL(current, kControlMsgFocus, part))
            return noErr;
        CTLCALL(current, kControlMsgFocus, kControlFocusNoPart);
        ++it;
    }
    else
        it = order.begin();
    for(size_t n = 0; n < order.size(); n++, ++it)
    {
        if(it == order.end())
            it = order.begin();
        CtlCallGuard guard(*it);
        if(CTLCALL(*it, kControlMsgFocus, part))
        {
            focus()[w] = *it;
            return noErr;
        }
    }
    focus()[w] = nullptr;
    return errCouldntSetFocus;
}

OSStatus getProperty(ControlHandle c, uint32_t creator, uint32_t tag,
                     UInt32 size, GUEST<UInt32> *actual, void *buffer)
{
    ControlExtras *x = c ? findExtras(c) : nullptr;
    if(!x)
        return controlPropertyNotFoundErr;
    auto it = x->properties.find({ creator, tag });
    if(it == x->properties.end())
        return controlPropertyNotFoundErr;
    if(actual)
        *actual = it->second.size();
    if(buffer)
        memcpy(buffer, it->second.data(), std::min<size_t>(size, it->second.size()));
    return noErr;
}

OSStatus setProperty(ControlHandle c, uint32_t creator, uint32_t tag,
                     UInt32 size, const void *buffer)
{
    if(!c)
        return paramErr;
    auto& v = extras(c).properties[{ creator, tag }];
    v.assign((const uint8_t *)buffer, (const uint8_t *)buffer + (buffer ? size : 0));
    return noErr;
}

void redrawIndicators(ControlHandle c)
{
    CtlCallGuard guard(c);
    if(CTL_VIS(c))
        CTLCALL(c, drawCntl, ALLINDICATORS);
}
}

/* The hooks the Control Manager's older calls make (ctl/*.cpp). */

void Executor::ROMlib_control8_new(ControlHandle c)
{
    WindowPtr w = CTL_OWNER(c);
    ControlHandle root = rootOf(w);
    if(root && root != c)
        C_EmbedControl(c, root);
}

void Executor::ROMlib_control8_dispose(ControlHandle c)
{
    /* Focus first: a control with no extras can have it too
       (SetKeyboardFocus on an old one), and a stale one crashed
       AdvanceKeyboardFocus (the 8.6 Appearance control panel). */
    for(auto &f : focus())
        if(f.second == c)
            f.second = nullptr;
    ControlExtras *x = findExtras(c);
    if(!x)
        return;
    while(!x->subs.empty())
    {
        DisposeControl(x->subs.back());
        x = findExtras(c);
    }
    detach(c);
    WindowPtr w = CTL_OWNER(c);
    if(rootOf(w) == c)
        roots().erase(w);
    extrasTable().erase(c);
}

void Executor::ROMlib_control8_forget_window(WindowPtr w)
{
    for(ControlHandle c = WINDOW_CONTROL_LIST(w); c; c = CTL_NEXT_CONTROL(c))
        extrasTable().erase(c);
    roots().erase(w);
    focus().erase(w);
}

bool Executor::ROMlib_control8_embedded(ControlHandle c)
{
    ControlExtras *x = findExtras(c);
    return x && x->super;
}

void Executor::ROMlib_control8_draw_subs(ControlHandle c)
{
    ControlExtras *x = findExtras(c);
    if(!x)
        return;
    std::vector<ControlHandle> subs = x->subs;
    for(ControlHandle s : subs)
        Draw1Control(s);
}

void Executor::ROMlib_control8_show_subs(ControlHandle c, bool show)
{
    ControlExtras *x = findExtras(c);
    if(!x)
        return;
    std::vector<ControlHandle> subs = x->subs;
    for(ControlHandle s : subs)
    {
        if(show)
        {
            if(extras(s).latentVisible)
            {
                extras(s).latentVisible = false;
                ShowControl(s);
            }
        }
        else if(CTL_VIS(s))
        {
            /* the embedder's erasing covers the subcontrols */
            extras(s).latentVisible = true;
            CTL_VIS(s) = 0;
            ROMlib_control8_show_subs(s, false);
        }
    }
}

bool Executor::ROMlib_control8_find(WindowPtr w, Point p, ControlHandle *hit, INTEGER *part)
{
    ControlHandle root = rootOf(w);
    if(!root)
        return false;
    int16_t pc = 0;
    *hit = deepestAt(root, p, true, &pc);
    *part = *hit ? pc : 0;
    return true;
}

bool Executor::ROMlib_control8_handles_tracking(ControlHandle c)
{
    return features(c) & kControlHandlesTracking;
}

INTEGER Executor::ROMlib_control8_track(ControlHandle c, Point p, ControlActionUPP a,
                                        EventModifiers modifiers)
{
    CtlCallGuard guard(c);
    if(a == (ControlActionUPP)-1)
        a = CTL_ACTION(c);
    ControlTrackingRec rec;
    rec.startPt = p;
    rec.modifiers = modifiers;
    rec.action = a;
    return CTLCALL(c, kControlMsgHandleTracking, ptr_to_longint(&rec));
}

/* Activation and visibility */

Boolean Executor::C_IsControlActive(ControlRef inControl)
{
    return inControl && CTL_HILITE(inControl) != INACTIVE;
}

Boolean Executor::C_IsControlVisible(ControlRef inControl)
{
    return inControl && CTL_VIS(inControl);
}

static void activate(ControlHandle c, bool on)
{
    if(on != (CTL_HILITE(c) != INACTIVE))
    {
        if(features(c) & kControlWantsActivate)
        {
            CtlCallGuard guard(c);
            CTL_HILITE(c) = on ? 0 : INACTIVE;
            CTLCALL(c, kControlMsgActivate, on);
            if(CTL_VIS(c))
                CTLCALL(c, drawCntl, 0);
        }
        else
            HiliteControl(c, on ? 0 : INACTIVE);
    }
    if(ControlExtras *x = findExtras(c))
    {
        std::vector<ControlHandle> subs = x->subs;
        for(ControlHandle s : subs)
            activate(s, on);
    }
}

OSErr Executor::C_ActivateControl(ControlRef inControl)
{
    if(!inControl)
        return errUnknownControl;
    activate(inControl, true);
    return noErr;
}

OSErr Executor::C_DeactivateControl(ControlRef inControl)
{
    if(!inControl)
        return errUnknownControl;
    activate(inControl, false);
    return noErr;
}

OSErr Executor::C_SetControlVisibility(ControlRef inControl, Boolean inIsVisible, Boolean inDoDraw)
{
    if(!inControl)
        return errUnknownControl;
    if(inDoDraw)
    {
        if(inIsVisible)
            ShowControl(inControl);
        else
            HideControl(inControl);
    }
    else
    {
        CTL_VIS(inControl) = inIsVisible ? 255 : 0;
        if(ControlExtras *x = findExtras(inControl))
            for(ControlHandle s : x->subs)
                C_SetControlVisibility(s, inIsVisible, false);
    }
    return noErr;
}

/* Drawing */

void Executor::C_DrawControlInCurrentPort(ControlRef inControl)
{
    if(inControl && CTL_VIS(inControl))
    {
        CTLCALL(inControl, drawCntl, 0);
        if(ControlExtras *x = findExtras(inControl))
        {
            std::vector<ControlHandle> subs = x->subs;
            for(ControlHandle s : subs)
                C_DrawControlInCurrentPort(s);
        }
    }
}

OSErr Executor::C_SetUpControlBackground(ControlRef inControl, SInt16 inDepth, Boolean inIsColorDevice)
{
    if(!inControl)
        return errUnknownControl;
    ControlBackgroundRec rec;
    rec.depth = inDepth;
    rec.colorDevice = inIsColorDevice;
    /* The nearest embedder with a background of its own sets it up. */
    for(ControlHandle c = inControl; c; c = findExtras(c) ? findExtras(c)->super : nullptr)
        if(c != inControl && (features(c) & kControlHasSpecialBackground))
        {
            CTLCALL(c, kControlMsgSetUpBackground, ptr_to_longint(&rec));
            return noErr;
        }
    RGBColor *colors = validate_colors_for_window(CTL_OWNER(inControl));
    RGBBackColor(&colors[wContentColor]);
    return noErr;
}

OSErr Executor::C_SetUpControlTextColor(ControlRef inControl, SInt16 inDepth, Boolean inIsColorDevice)
{
    if(!inControl)
        return errUnknownControl;
    ControlApplyTextColorRec rec;
    rec.depth = inDepth;
    rec.colorDevice = inIsColorDevice;
    rec.active = CTL_HILITE(inControl) != INACTIVE;
    for(ControlHandle c = inControl; c; c = findExtras(c) ? findExtras(c)->super : nullptr)
        if(c != inControl && (features(c) & kControlHasSpecialBackground))
        {
            CTLCALL(c, kControlMsgApplyTextColor, ptr_to_longint(&rec));
            return noErr;
        }
    RGBColor color = rec.active ? RGBColor{ 0, 0, 0 } : RGBColor{ 0x7777, 0x7777, 0x7777 };
    RGBForeColor(&color);
    return noErr;
}

OSErr Executor::C_GetBestControlRect(ControlRef inControl, Rect *outRect,
                                     GUEST<SInt16> *outBaseLineOffset)
{
    if(!inControl)
        return errUnknownControl;
    *outRect = CTL_RECT(inControl);
    *outBaseLineOffset = 0;
    if(!(features(inControl) & kControlSupportsCalcBestRect))
        return errMessageNotSupported;
    ControlCalcSizeRec rec;
    rec.height = RECT_HEIGHT(&CTL_RECT(inControl));
    rec.width = RECT_WIDTH(&CTL_RECT(inControl));
    rec.baseLine = 0;
    {
        CtlCallGuard guard(inControl);
        CTLCALL(inControl, kControlMsgCalcBestRect, ptr_to_longint(&rec));
    }
    outRect->right = outRect->left + rec.width;
    outRect->bottom = outRect->top + rec.height;
    *outBaseLineOffset = rec.baseLine;
    return noErr;
}

OSErr Executor::C_SetControlFontStyle(ControlRef inControl, const ControlFontStyleRec *inStyle)
{
    if(!inControl || !inStyle)
        return paramErr;
    extras(inControl).font = *inStyle;
    extras(inControl).hasFont = inStyle->flags != 0;
    return noErr;
}

/* Mouse and keyboard */

ControlRef Executor::C_FindControlUnderMouse(Point inWhere, WindowRef inWindow, GUEST<SInt16> *outPart)
{
    int16_t part = 0;
    ControlHandle hit = nullptr;
    if(ControlHandle root = rootOf(inWindow))
        hit = deepestAt(root, inWhere, false, &part);
    else
        for(ControlHandle c = WINDOW_CONTROL_LIST(inWindow); c && !hit; c = CTL_NEXT_CONTROL(c))
            hit = deepestAt(c, inWhere, false, &part);
    if(outPart)
        *outPart = part;
    return hit;
}

ControlPartCode Executor::C_HandleControlClick(ControlRef inControl, Point inWhere,
                                               EventModifiers inModifiers, ControlActionUPP inAction)
{
    if(!inControl)
        return kControlNoPart;
    if(features(inControl) & kControlGetsFocusOnClick)
        C_SetKeyboardFocus(CTL_OWNER(inControl), inControl, kControlFocusNextPart);
    if(features(inControl) & kControlHandlesTracking)
        return ROMlib_control8_track(inControl, inWhere, inAction, inModifiers);
    return TrackControl(inControl, inWhere, inAction);
}

ControlPartCode Executor::C_HandleControlKey(ControlRef inControl, SInt16 inKeyCode,
                                             SInt16 inCharCode, EventModifiers inModifiers)
{
    if(!inControl)
        return kControlNoPart;
    ControlKeyDownRec rec;
    rec.modifiers = inModifiers;
    rec.keyCode = inKeyCode;
    rec.charCode = inCharCode;
    CtlCallGuard guard(inControl);
    return message8(inControl, kControlMsgKeyDown, ptr_to_longint(&rec));
}

void Executor::C_IdleControls(WindowRef inWindow)
{
    for(ControlHandle c = WINDOW_CONTROL_LIST(inWindow); c; c = CTL_NEXT_CONTROL(c))
        if(features(c) & kControlWantsIdle)
        {
            CtlCallGuard guard(c);
            CTLCALL(c, kControlMsgIdle, 0);
        }
}

OSErr Executor::C_GetKeyboardFocus(WindowRef inWindow, GUEST<ControlRef> *outControl)
{
    *outControl = focus()[inWindow];
    return noErr;
}

OSErr Executor::C_SetKeyboardFocus(WindowRef inWindow, ControlRef inControl, ControlFocusPart inPart)
{
    ControlHandle current = focus()[inWindow];
    if(inControl && !(features(inControl) & kControlSupportsFocus))
        return errControlDoesntSupportFocus;
    if(current && (current != inControl || inPart == kControlFocusNoPart))
    {
        CtlCallGuard guard(current);
        CTLCALL(current, kControlMsgFocus, kControlFocusNoPart);
        focus()[inWindow] = nullptr;
    }
    if(!inControl || inPart == kControlFocusNoPart)
        return noErr;
    if(!CTL_VIS(inControl) || CTL_HILITE(inControl) == INACTIVE)
        return errControlHiddenOrDisabled;
    CtlCallGuard guard(inControl);
    if(!CTLCALL(inControl, kControlMsgFocus, inPart))
        return errCouldntSetFocus;
    focus()[inWindow] = inControl;
    return noErr;
}

OSErr Executor::C_AdvanceKeyboardFocus(WindowRef inWindow)
{
    return moveFocus(inWindow, kControlFocusNextPart);
}

OSErr Executor::C_ReverseKeyboardFocus(WindowRef inWindow)
{
    return moveFocus(inWindow, kControlFocusPrevPart);
}

OSErr Executor::C_ClearKeyboardFocus(WindowRef inWindow)
{
    return C_SetKeyboardFocus(inWindow, nullptr, kControlFocusNoPart);
}

/* Values */

SInt32 Executor::C_GetControl32BitValue(ControlRef theControl)
{
    return get32(extras(theControl).value, CTL_VALUE(theControl));
}

SInt32 Executor::C_GetControl32BitMinimum(ControlRef theControl)
{
    return get32(extras(theControl).minimum, CTL_MIN(theControl));
}

SInt32 Executor::C_GetControl32BitMaximum(ControlRef theControl)
{
    return get32(extras(theControl).maximum, CTL_MAX(theControl));
}

void Executor::C_SetControl32BitValue(ControlRef theControl, SInt32 newValue)
{
    newValue = std::clamp(newValue, C_GetControl32BitMinimum(theControl),
                          std::max(C_GetControl32BitMinimum(theControl),
                                   C_GetControl32BitMaximum(theControl)));
    set32(extras(theControl).value, (*theControl)->contrlValue, newValue);
    redrawIndicators(theControl);
}

void Executor::C_SetControl32BitMinimum(ControlRef theControl, SInt32 newMinimum)
{
    ControlExtras& x = extras(theControl);
    int32_t value = C_GetControl32BitValue(theControl);
    set32(x.minimum, (*theControl)->contrlMin, newMinimum);
    if(value < newMinimum)
        set32(x.value, (*theControl)->contrlValue, newMinimum);
    redrawIndicators(theControl);
}

void Executor::C_SetControl32BitMaximum(ControlRef theControl, SInt32 newMaximum)
{
    ControlExtras& x = extras(theControl);
    int32_t value = C_GetControl32BitValue(theControl);
    newMaximum = std::max(newMaximum, C_GetControl32BitMinimum(theControl));
    set32(x.maximum, (*theControl)->contrlMax, newMaximum);
    if(value > newMaximum)
        set32(x.value, (*theControl)->contrlValue, newMaximum);
    redrawIndicators(theControl);
}

SInt32 Executor::C_GetControlViewSize(ControlRef theControl)
{
    return extras(theControl).viewSize;
}

void Executor::C_SetControlViewSize(ControlRef theControl, SInt32 newViewSize)
{
    if(extras(theControl).viewSize == newViewSize)
        return;
    extras(theControl).viewSize = newViewSize;
    redrawIndicators(theControl);
}

Boolean Executor::C_IsValidControlHandle(ControlRef theControl)
{
    if(!theControl)
        return false;
    for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
        for(ControlHandle c = WINDOW_CONTROL_LIST(w); c; c = CTL_NEXT_CONTROL(c))
            if(c == theControl)
                return true;
    return false;
}

/* Embedding */

OSErr Executor::C_CreateRootControl(WindowRef inWindow, GUEST<ControlRef> *outControl)
{
    if(rootOf(inWindow))
        return errRootAlreadyExists;
    Rect r = { -32767, -32767, 32767, 32767 };
    ControlHandle root = NewControl(inWindow, &r, (StringPtr) "", true, 0, 0, 0,
                                    kControlUserPaneProc, 0);
    if(!root)
        return errUnknownControl;
    roots()[inWindow] = root;
    extras(root);
    /* the window's other controls go into it, oldest first */
    std::vector<ControlHandle> others;
    for(ControlHandle c = WINDOW_CONTROL_LIST(inWindow); c; c = CTL_NEXT_CONTROL(c))
        if(c != root && !ROMlib_control8_embedded(c))
            others.push_back(c);
    for(auto it = others.rbegin(); it != others.rend(); ++it)
        C_EmbedControl(*it, root);
    if(outControl)
        *outControl = root;
    return noErr;
}

OSErr Executor::C_GetRootControl(WindowRef inWindow, GUEST<ControlRef> *outControl)
{
    ControlHandle root = rootOf(inWindow);
    *outControl = root;
    return root ? noErr : errNoRootControl;
}

OSErr Executor::C_EmbedControl(ControlRef inControl, ControlRef inContainer)
{
    if(!inControl || !inContainer)
        return errUnknownControl;
    if(inControl == inContainer)
        return errCantEmbedIntoSelf;
    if(rootOf(CTL_OWNER(inControl)) == inControl)
        return errCantEmbedRoot;
    if(!(features(inContainer) & kControlSupportsEmbedding))
        return errControlIsNotEmbedder;
    detach(inControl);
    extras(inControl).super = inContainer;
    extras(inContainer).subs.push_back(inControl);
    CtlCallGuard guard(inContainer);
    message8(inContainer, kControlMsgSubControlAdded, ptr_to_longint(inControl));
    return noErr;
}

static ControlHandle embedderFor(ControlHandle c, ControlHandle candidate, const Rect& r)
{
    if(candidate == c || !(features(candidate) & kControlSupportsEmbedding))
        return nullptr;
    Rect sect;
    if(!SectRect(&r, &CTL_RECT(candidate), &sect) || !EqualRect(&sect, &r))
        return nullptr;
    if(ControlExtras *x = findExtras(candidate))
        for(auto it = x->subs.rbegin(); it != x->subs.rend(); ++it)
            if(ControlHandle deeper = embedderFor(c, *it, r))
                return deeper;
    return candidate;
}

OSErr Executor::C_AutoEmbedControl(ControlRef inControl, WindowRef inWindow)
{
    ControlHandle root = rootOf(inWindow);
    if(!root)
        return errNoRootControl;
    Rect r = CTL_RECT(inControl);
    ControlHandle container = embedderFor(inControl, root, r);
    return C_EmbedControl(inControl, container ? container : root);
}

OSErr Executor::C_GetSuperControl(ControlRef inControl, GUEST<ControlRef> *outParent)
{
    ControlExtras *x = inControl ? findExtras(inControl) : nullptr;
    *outParent = x ? x->super : nullptr;
    return x && x->super ? noErr : errControlIsNotEmbedder;
}

OSErr Executor::C_CountSubControls(ControlRef inControl, GUEST<UInt16> *outNumChildren)
{
    if(!inControl)
        return errUnknownControl;
    if(!(features(inControl) & kControlSupportsEmbedding))
        return errControlIsNotEmbedder;
    *outNumChildren = extras(inControl).subs.size();
    return noErr;
}

OSErr Executor::C_GetIndexedSubControl(ControlRef inControl, UInt16 inIndex,
                                       GUEST<ControlRef> *outSubControl)
{
    if(!inControl)
        return errUnknownControl;
    if(!(features(inControl) & kControlSupportsEmbedding))
        return errControlIsNotEmbedder;
    auto& subs = extras(inControl).subs;
    if(inIndex < 1 || inIndex > subs.size())
        return paramErr;
    *outSubControl = subs[inIndex - 1];
    return noErr;
}

OSErr Executor::C_SetControlSupervisor(ControlRef inControl, ControlRef inBoss)
{
    /* Radio groups take their buttons' clicks so; Executor's tracking
       doesn't pass clicks up yet. */
    return inControl ? noErr : errUnknownControl;
}

/* The 8.0 messages */

OSErr Executor::C_GetControlFeatures(ControlRef inControl, GUEST<UInt32> *outFeatures)
{
    if(!inControl)
        return errUnknownControl;
    *outFeatures = features(inControl);
    return noErr;
}

SInt32 Executor::C_SendControlMessage(ControlRef inControl, SInt16 inMessage, void *inParam)
{
    if(!inControl)
        return 0;
    CtlCallGuard guard(inControl);
    return CTLCALL(inControl, inMessage, ptr_to_longint(inParam));
}

OSErr Executor::C_SetControlData(ControlRef inControl, ControlPartCode inPart, ResType inTagName,
                                 Size inSize, const void *inData)
{
    if(!inControl)
        return errUnknownControl;
    if(inTagName == kControlFontStyleTag)
    {
        if(inSize != sizeof(ControlFontStyleRec))
            return errDataSizeMismatch;
        return C_SetControlFontStyle(inControl, (const ControlFontStyleRec *)inData);
    }
    if(!(features(inControl) & kControlSupportsDataAccess))
        return errDataNotSupported;
    ControlDataAccessRec rec;
    rec.tag = inTagName;
    rec.part = inPart;
    rec.size = inSize;
    rec.dataPtr = (Ptr)inData;
    CtlCallGuard guard(inControl);
    return CTLCALL(inControl, kControlMsgSetData, ptr_to_longint(&rec));
}

static OSErr getData(ControlHandle c, ControlPartCode part, ResType tag, Size bufferSize,
                     void *buffer, GUEST<Size> *actualSize)
{
    if(!c)
        return errUnknownControl;
    if(tag == kControlFontStyleTag)
    {
        ControlExtras& x = extras(c);
        if(!x.hasFont)
            memset(&x.font, 0, sizeof x.font);
        if(buffer)
            memcpy(buffer, &x.font, std::min<size_t>(bufferSize, sizeof x.font));
        if(actualSize)
            *actualSize = sizeof x.font;
        return noErr;
    }
    if(!(features(c) & kControlSupportsDataAccess))
        return errDataNotSupported;
    ControlDataAccessRec rec;
    rec.tag = tag;
    rec.part = part;
    rec.size = bufferSize;
    rec.dataPtr = (Ptr)buffer;
    OSErr err;
    {
        CtlCallGuard guard(c);
        err = CTLCALL(c, kControlMsgGetData, ptr_to_longint(&rec));
    }
    if(actualSize)
        *actualSize = rec.size;
    return err;
}

OSErr Executor::C_GetControlData(ControlRef inControl, ControlPartCode inPart, ResType inTagName,
                                 Size inBufferSize, void *inBuffer, GUEST<Size> *outActualSize)
{
    return getData(inControl, inPart, inTagName, inBufferSize, inBuffer, outActualSize);
}

OSErr Executor::C_GetControlDataSize(ControlRef inControl, ControlPartCode inPart,
                                     ResType inTagName, GUEST<Size> *outMaxSize)
{
    return getData(inControl, inPart, inTagName, 0, nullptr, outMaxSize);
}

OSStatus Executor::C_GetControlRegion(ControlRef inControl, ControlPartCode inPart, RgnHandle outRegion)
{
    if(!inControl)
        return errUnknownControl;
    CtlCallGuard guard(inControl);
    if(features(inControl) & kControlSupportsGetRegion)
    {
        ControlGetRegionRec rec;
        rec.region = outRegion;
        rec.part = inPart;
        return CTLCALL(inControl, kControlMsgGetRegion, ptr_to_longint(&rec));
    }
    if(inPart == kControlStructureMetaPart || inPart == kControlContentMetaPart)
    {
        CTLCALL(inControl, calcCntlRgn, ptr_to_longint(outRegion));
        return noErr;
    }
    return errInvalidPartCode;
}

/* Properties */

OSStatus Executor::C_GetControlProperty(ControlRef control, OSType propertyCreator, OSType propertyTag,
                                        UInt32 bufferSize, GUEST<UInt32> *actualSize, void *propertyBuffer)
{
    return getProperty(control, propertyCreator, propertyTag, bufferSize, actualSize, propertyBuffer);
}

OSStatus Executor::C_GetControlPropertySize(ControlRef control, OSType propertyCreator,
                                            OSType propertyTag, GUEST<UInt32> *size)
{
    return getProperty(control, propertyCreator, propertyTag, 0, size, nullptr);
}

OSStatus Executor::C_SetControlProperty(ControlRef control, OSType propertyCreator, OSType propertyTag,
                                        UInt32 propertySize, void *propertyData)
{
    return setProperty(control, propertyCreator, propertyTag, propertySize, propertyData);
}

OSStatus Executor::C_RemoveControlProperty(ControlRef control, OSType propertyCreator, OSType propertyTag)
{
    ControlExtras *x = control ? findExtras(control) : nullptr;
    if(!x || !x->properties.erase({ propertyCreator, propertyTag }))
        return controlPropertyNotFoundErr;
    return noErr;
}

/* The definitions' own (ControlsLib's private calls): a creator of 0. */

OSStatus Executor::C_GetControlSystemProperty(ControlRef control, PropertyTag tag, UInt32 bufferSize,
                                              GUEST<UInt32> *actualSize, void *buffer)
{
    return getProperty(control, 0, tag, bufferSize, actualSize, buffer);
}

OSStatus Executor::C_SetControlSystemProperty(ControlRef control, PropertyTag tag, UInt32 size,
                                              const void *buffer)
{
    return setProperty(control, 0, tag, size, buffer);
}
