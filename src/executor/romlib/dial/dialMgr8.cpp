/* MacPhoenix: the Dialog Manager of Mac OS 8 (Dialogs.h): AppearanceLib's
 * calls (StandardAlert, NewFeaturesDialog, dialog items as controls, moving
 * and sizing items) and the dialogs library's (8.5: item lists by resource,
 * timeouts, modal event masks). docs/executor/Appearance.md. */

#include <base/common.h>
#include <DialogMgr.h>
#include <DialogMgr8.h>
#include <ControlMgr.h>
#include <ControlMgr8.h>
#include <ResourceMgr.h>
#include <MemoryMgr.h>
#include <QuickDraw.h>
#include <FontMgr.h>
#include <ToolboxUtil.h>
#include <EventMgr.h>
#include <ToolboxEvent.h>
#include <Appearance.h>
#include <dial/itm.h>
#include <dial/dial.h>
#include <quickdraw/cquick.h>
#include <base/functions.impl.h>
#include <map>
#include <vector>

using namespace Executor;

namespace
{
struct DialogExtras
{
    int16_t timeoutButton = 0;
    uint32_t timeoutSeconds = 0, timeoutStart = 0;
    bool hasMask = false;
    EventMask mask = 0;
};

std::map<DialogPtr, DialogExtras>& extrasTable()
{
    static std::map<DialogPtr, DialogExtras> table;
    return table;
}

/* A DITL built in memory: items appended one by one. */
class ItemList
{
    std::vector<uint8_t> bytes;
    int count = 0;

public:
    void add(uint8_t type, Rect r, const uint8_t *data, uint8_t len)
    {
        uint8_t head[14] = {};
        head[4] = r.top >> 8, head[5] = r.top;
        head[6] = r.left >> 8, head[7] = r.left;
        head[8] = r.bottom >> 8, head[9] = r.bottom;
        head[10] = r.right >> 8, head[11] = r.right;
        head[12] = type;
        head[13] = len;
        bytes.insert(bytes.end(), head, head + 14);
        bytes.insert(bytes.end(), data, data + len);
        if(len & 1)
            bytes.push_back(0);
        count++;
    }
    void add(uint8_t type, Rect r, ConstStringPtr text)
    {
        add(type, r, text ? text + 1 : nullptr, text ? text[0] : 0);
    }
    void addIcon(Rect r, int16_t id)
    {
        uint8_t data[2] = { (uint8_t)(id >> 8), (uint8_t)id };
        add(iconItem | itemDisable, r, data, 2);
    }
    Handle handle() const
    {
        Handle h = NewHandle(2 + bytes.size());
        uint8_t *p = (uint8_t *)*h;
        p[0] = (count - 1) >> 8;
        p[1] = count - 1;
        memcpy(p + 2, bytes.data(), bytes.size());
        return h;
    }
};

/* Lines a string takes wrapped to width in the current font. */
int wrappedLines(ConstStringPtr s, int width)
{
    if(!s || !s[0])
        return 0;
    int lines = 1, x = 0, wordStart = 1;
    for(int i = 1; i <= s[0] + 1; i++)
    {
        if(i > s[0] || s[i] == ' ' || s[i] == '\r')
        {
            int w = TextWidth((Ptr)s, wordStart, i - wordStart);
            if(x + w > width && x > 0)
            {
                lines++;
                x = 0;
            }
            x += w + CharWidth(' ');
            if(i <= s[0] && s[i] == '\r')
            {
                lines++;
                x = 0;
            }
            wordStart = i + 1;
        }
    }
    return lines;
}

/* The button texts: the param record's, nil for none, -1 for the default. */
ConstStringPtr buttonText(ConstStringPtr text, ConstStringPtr standard)
{
    if(text == (ConstStringPtr)-1)
        return standard;
    return text;
}
}

void Executor::ROMlib_dialog8_forget(DialogPtr dialog)
{
    extrasTable().erase(dialog);
}

OSErr Executor::C_StandardAlert(AlertType inAlertType, ConstStr255Param inError,
                                ConstStr255Param inExplanation,
                                const AlertStdAlertParamRec *inAlertParam,
                                GUEST<SInt16> *outItemHit)
{
    /* Without a parameter record: OK alone, the default. */
    ConstStringPtr ok = (ConstStringPtr) "\002OK", cancel = (ConstStringPtr) "\006Cancel",
                   dontSave = (ConstStringPtr) "\012Don\xd5t Save";
    ConstStringPtr texts[3] = { ok, nullptr, nullptr };
    int16_t defaultButton = kAlertStdAlertOKButton, cancelButton = 0;
    bool movable = false;
    ModalFilterUPP filter = nullptr;
    if(inAlertParam)
    {
        texts[0] = buttonText(inAlertParam->defaultText, ok);
        texts[1] = buttonText(inAlertParam->cancelText, cancel);
        texts[2] = buttonText(inAlertParam->otherText, dontSave);
        defaultButton = inAlertParam->defaultButton;
        cancelButton = inAlertParam->cancelButton;
        movable = inAlertParam->movable;
        filter = inAlertParam->filterProc;
    }

    /* Layout: the icon at the left, the message (system font) and the
       explanation beside it, the buttons along the bottom, right-aligned,
       the "other" button at the left. */
    GUEST<GrafPtr> savePort;
    GetPort(&savePort);
    SetPort(LM(WMgrPort));
    constexpr int textLeft = 74, textWidth = 300, line = 16;
    int16_t saveFont = qdGlobals().thePort->txFont, saveSize = qdGlobals().thePort->txSize;
    TextFont(0);
    TextSize(0);
    int errorLines = wrappedLines(inError, textWidth);
    int explanationLines = wrappedLines(inExplanation, textWidth);
    int buttonWidths[3];
    for(int i = 0; i < 3; i++)
        buttonWidths[i] = texts[i] ? std::max<int>(69, StringWidth(texts[i]) + 24) : 0;
    TextFont(saveFont);
    TextSize(saveSize);
    SetPort(savePort);

    int textBottom = 14 + errorLines * line + (explanationLines ? 8 + explanationLines * line : 0);
    int top = std::max(textBottom, 14 + 32) + 16;
    int width = textLeft + textWidth + 14;
    int height = top + 20 + 14;

    ItemList items;
    int right = width - 14;
    Rect buttons[3] = {};
    for(int i = 0; i < 2; i++)
        if(texts[i])
        {
            buttons[i] = { (int16_t)top, (int16_t)(right - buttonWidths[i]), (int16_t)(top + 20), (int16_t)right };
            right -= buttonWidths[i] + 12;
        }
    if(texts[2])
        buttons[2] = { (int16_t)top, 14, (int16_t)(top + 20), (int16_t)(14 + buttonWidths[2]) };
    /* items 1-3: OK, Cancel, Other, as the item hits StandardAlert returns */
    for(int i = 0; i < 3; i++)
    {
        if(texts[i])
            items.add(ctrlItem | btnCtrl, buttons[i], texts[i]);
        else
            items.add(userItem | itemDisable, Rect{ 0, 0, 0, 0 }, nullptr);
    }
    if(inAlertType != kAlertPlainAlert)
        items.addIcon(Rect{ 12, 20, 44, 52 }, inAlertType); /* 8.6: appr dialogs.*.standard */
    items.add(statText | itemDisable,
              Rect{ 14, textLeft, (int16_t)(14 + errorLines * line), (int16_t)(textLeft + textWidth) }, inError);
    if(explanationLines)
        items.add(statText | itemDisable,
                  Rect{ (int16_t)(22 + errorLines * line), textLeft, (int16_t)textBottom,
                        (int16_t)(textLeft + textWidth) }, inExplanation);

    /* centred across the main screen, a third of the way down */
    Rect screen = PIXMAP_BOUNDS(GD_PMAP(LM(MainDevice)));
    screen.top += LM(MBarHeight);
    Rect bounds;
    bounds.left = (screen.left + screen.right - width) / 2;
    bounds.top = screen.top + (screen.bottom - screen.top - height) / 3;
    bounds.right = bounds.left + width;
    bounds.bottom = bounds.top + height;

    DialogPtr dlg = NewColorDialog(nullptr, &bounds, (StringPtr) "", false,
                                   movable ? movableDBoxProc : dBoxProc,
                                   (WindowPtr)-1, false, 0, items.handle());
    if(!dlg)
        return memFullErr;
    /* on the theme's grey, as every alert (appr dialogs.*.standard) */
    SetThemeWindowBackground(dlg, kThemeBrushDialogBackgroundActive, false);
    if(defaultButton >= 1 && defaultButton <= 3 && texts[defaultButton - 1])
        SetDialogDefaultItem(dlg, defaultButton);
    if(cancelButton >= 1 && cancelButton <= 3 && texts[cancelButton - 1])
        SetDialogCancelItem(dlg, cancelButton);
    ShowWindow(dlg);
    SelectWindow(dlg);

    GUEST<INTEGER> hit;
    do
        ModalDialog(filter, &hit);
    while(hit < 1 || hit > 3);
    DisposeDialog(dlg);
    if(outItemHit)
        *outItemHit = hit;
    return noErr;
}

DialogPtr Executor::C_NewFeaturesDialog(void *inStorage, const Rect *inBoundsRect,
                                        ConstStr255Param inTitle, Boolean inIsVisible,
                                        SInt16 inProcID, WindowRef inBehind, Boolean inGoAwayFlag,
                                        SInt32 inRefCon, Handle inItemListHandle, UInt32 inFlags)
{
    DialogPtr dlg = NewColorDialog(inStorage, inBoundsRect, inTitle, false, inProcID,
                                   inBehind, inGoAwayFlag, inRefCon, inItemListHandle);
    if(!dlg)
        return nullptr;
    /* The item controls go into a root control (CreateRootControl embeds
       the window's controls). */
    if(inFlags & kDialogFlagsUseControlHierarchy)
        CreateRootControl(dlg, nullptr);
    if(inFlags & kDialogFlagsUseThemeBackground)
        SetThemeWindowBackground(dlg, kThemeBrushDialogBackgroundActive, false);
    if(inIsVisible)
        ShowWindow(dlg);
    return dlg;
}

OSErr Executor::C_AutoSizeDialog(DialogRef inDialog)
{
    /* The static text items keep the sizes they were given. */
    return inDialog ? noErr : paramErr;
}

OSErr Executor::C_GetDialogItemAsControl(DialogRef inDialog, SInt16 inItemNo,
                                         GUEST<ControlRef> *outControl)
{
    if(!inDialog || !outControl)
        return paramErr;
    GUEST<INTEGER> type;
    GUEST<Handle> h;
    Rect r;
    GetDialogItem(inDialog, inItemNo, &type, &h, &r);
    if(!((INTEGER)type & ctrlItem) || !h)
    {
        *outControl = nullptr;
        return errItemNotControl;
    }
    *outControl = (ControlHandle)h.get();
    return noErr;
}

OSErr Executor::C_MoveDialogItem(DialogRef inDialog, SInt16 inItemNo, SInt16 inHoriz, SInt16 inVert)
{
    if(!inDialog)
        return paramErr;
    GUEST<INTEGER> type;
    GUEST<Handle> h;
    Rect r;
    GetDialogItem(inDialog, inItemNo, &type, &h, &r);
    OffsetRect(&r, inHoriz - r.left, inVert - r.top);
    if(((INTEGER)type & ctrlItem) && h)
        MoveControl((ControlHandle)h.get(), inHoriz, inVert);
    SetDialogItem(inDialog, inItemNo, type, h, &r);
    return noErr;
}

OSErr Executor::C_SizeDialogItem(DialogRef inDialog, SInt16 inItemNo, SInt16 inWidth, SInt16 inHeight)
{
    if(!inDialog)
        return paramErr;
    GUEST<INTEGER> type;
    GUEST<Handle> h;
    Rect r;
    GetDialogItem(inDialog, inItemNo, &type, &h, &r);
    r.right = r.left + inWidth;
    r.bottom = r.top + inHeight;
    if(((INTEGER)type & ctrlItem) && h)
        SizeControl((ControlHandle)h.get(), inWidth, inHeight);
    SetDialogItem(inDialog, inItemNo, type, h, &r);
    return noErr;
}

OSErr Executor::C_AppendDialogItemList(DialogRef dialog, SInt16 ditlID, DITLMethod method)
{
    if(!dialog)
        return paramErr;
    Handle ditl = GetResource("DITL"_4, ditlID);
    if(!ditl)
        return resNotFound;
    AppendDITL(dialog, ditl, method);
    ReleaseResource(ditl);
    return noErr;
}

/* The timeout and the event mask are kept; Executor's ModalDialog doesn't
   press the button when the time is up, nor narrows its events yet. */
OSStatus Executor::C_SetDialogTimeout(DialogRef inDialog, SInt16 inButtonToPress,
                                      UInt32 inSecondsToWait)
{
    if(!inDialog)
        return paramErr;
    DialogExtras &x = extrasTable()[inDialog];
    x.timeoutButton = inButtonToPress;
    x.timeoutSeconds = inSecondsToWait;
    x.timeoutStart = TickCount();
    return noErr;
}

OSStatus Executor::C_GetDialogTimeout(DialogRef inDialog, GUEST<SInt16> *outButtonToPress,
                                      GUEST<UInt32> *outSecondsToWait,
                                      GUEST<UInt32> *outSecondsRemaining)
{
    if(!inDialog)
        return paramErr;
    DialogExtras &x = extrasTable()[inDialog];
    if(!x.timeoutSeconds)
        return -5640; /* dialogNoTimeoutErr (MacErrors.h) */
    uint32_t elapsed = (TickCount() - x.timeoutStart) / 60;
    if(outButtonToPress)
        *outButtonToPress = x.timeoutButton;
    if(outSecondsToWait)
        *outSecondsToWait = x.timeoutSeconds;
    if(outSecondsRemaining)
        *outSecondsRemaining = elapsed < x.timeoutSeconds ? x.timeoutSeconds - elapsed : 0;
    return noErr;
}

OSStatus Executor::C_SetModalDialogEventMask(DialogRef inDialog, EventMask inMask)
{
    if(!inDialog)
        return paramErr;
    DialogExtras &x = extrasTable()[inDialog];
    x.hasMask = true;
    x.mask = inMask;
    return noErr;
}

OSStatus Executor::C_GetModalDialogEventMask(DialogRef inDialog, GUEST<EventMask> *outMask)
{
    if(!inDialog || !outMask)
        return paramErr;
    DialogExtras &x = extrasTable()[inDialog];
    *outMask = x.hasMask ? x.mask : (EventMask)everyEvent;
    return noErr;
}
