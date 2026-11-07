/* Copyright 1992 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <OSUtil.h>
#include <NotifyMgr.h>
#include <rsys/hook.h>
#include <base/cpu.h>
#include <DialogMgr.h>
#include <ControlMgr.h>
#include <QuickDraw.h>
#include <SoundMgr.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>
#include <wind/wind.h>
#include <quickdraw/cquick.h>
#include <MenuMgr.h>
#include <rsys/process.h>
#include <algorithm>
#include <vector>

using namespace Executor;

/* Forward declarations in NotifyMgr.h (DO NOT DELETE THIS LINE) */

typedef void nmfunc(NMRecPtr nmptr);

/*
 * The stubs routine will take care of the trap conventions:
 *
 * A0: address of NMRec record
 * D0: result code
 */

/* MacPhoenix: requests wait here until the front process makes an event
   call (NMInstall may be called at interrupt time), as in 7.5.5's
   Process Manager: then the sound, the text in an alert and, once that is
   dismissed, the response procedure. The menu mark and the menu bar icon
   are not shown. */
static std::vector<NMRecPtr> pending;

OSErr Executor::NMRemove(NMRecPtr nmptr)
{
    if(nmptr->qType != nmType)
        return nmTypErr;
    pending.erase(std::remove(pending.begin(), pending.end(), nmptr), pending.end());
    return noErr;
}

OSErr Executor::NMInstall(NMRecPtr nmptr)
{
    if(nmptr->qType != nmType)
        return nmTypErr;
    if(StringPtr s = nmptr->nmStr)
        fprintf(stderr, "[Executor] NMInstall: %.*s\n", (int)s[0], (const char *)s + 1);
    pending.push_back(nmptr);
    return noErr;
}

static void call_response(NMRecPtr nmptr)
{
    if(guest_cast<LONGINT>(nmptr->nmResp) == -1)
    {
        NMRemove(nmptr);
        return;
    }
    LONGINT saved0, saved1, saved2, savea0, savea1;

    ROMlib_hook(notify_procnumber);

    saved0 = EM_D0;
    saved1 = EM_D1;
    saved2 = EM_D2;
    savea0 = EM_A0;
    savea1 = EM_A1;
    PUSHADDR(US_TO_SYN68K(nmptr));
    execute68K(guest_cast<uint32_t>(nmptr->nmResp));
    EM_D0 = saved0;
    EM_D1 = saved1;
    EM_D2 = saved2;
    EM_A0 = savea0;
    EM_A1 = savea1;
}

/* The notification's text in an alert as 7.5.5 shows it (measured on a
   real boot): 448 x 158, centred, a fifth of the way down; the note icon,
   the text, OK. */
static void show_alert(ConstStringPtr text)
{
    enum
    {
        kWidth = 448,
        kHeight = 158,
    };
    GUEST<GrafPtr> save_port;
    GetPort(&save_port);

    /* The item list: OK, the text, the note icon. */
    std::vector<uint8_t> ditl;
    auto word = [&](int v) { ditl.push_back(v >> 8); ditl.push_back(v & 0xFF); };
    auto item = [&](int top, int left, int bottom, int right, int type,
                    const uint8_t *data, int len) {
        for(int i = 0; i < 4; i++)
            ditl.push_back(0);
        word(top);
        word(left);
        word(bottom);
        word(right);
        ditl.push_back(type);
        ditl.push_back(len);
        ditl.insert(ditl.end(), data, data + len);
        if(len & 1)
            ditl.push_back(0);
    };
    word(3 - 1);
    item(130, 355, 150, 425, ctrlItem + btnCtrl, (const uint8_t *)"OK", 2);
    item(8, 70, 120, 433, statText | itemDisable, text + 1, text[0]);
    const uint8_t note_icon[2] = { 0, noteIcon };
    item(8, 18, 40, 50, iconItem | itemDisable, note_icon, 2);
    Handle items = NewHandle(ditl.size());
    memcpy(*items, ditl.data(), ditl.size());

    Rect screen = qdGlobals().screenBits.bounds;
    Rect r;
    r.left = (screen.left + screen.right - kWidth) / 2;
    r.top = screen.top + LM(MBarHeight) + (screen.bottom - screen.top - kHeight) / 5;
    r.right = r.left + kWidth;
    r.bottom = r.top + kHeight;

    DialogPtr dp = NewDialog(nullptr, &r, (StringPtr) "", true, dBoxProc,
                             (WindowPtr)-1, false, 0, items);
    if(dp)
    {
        SetPort(dp);
        /* Drawn now, so the update ModalDialog would take doesn't erase
           the default button's outline. */
        BeginUpdate(dp);
        DrawDialog(dp);
        EndUpdate(dp);
        GUEST<INTEGER> type;
        GUEST<Handle> h;
        Rect box;
        GetDialogItem(dp, 1, &type, &h, &box);
        InsetRect(&box, -4, -4);
        PenSize(3, 3);
        FrameRoundRect(&box, 16, 16);
        PenSize(1, 1);
        GUEST<INTEGER> hit;
        ROMlib_process_hold_front(true);
        do
            ModalDialog(nullptr, &hit);
        while(hit != 1);
        ROMlib_process_hold_front(false);
        DisposeDialog(dp);
    }
    SetPort(save_port);
}

void Executor::ROMlib_notify_poll()
{
    static bool busy;
    if(busy || pending.empty())
        return;
    busy = true;
    NMRecPtr nmptr = pending.front();
    pending.erase(pending.begin());

    Handle sound = nmptr->nmSound;
    if(guest_cast<LONGINT>(sound) == -1)
        SysBeep(5);
    else if(sound)
        SndPlay(nullptr, sound, false);
    if(StringPtr s = nmptr->nmStr)
        show_alert(s);
    if(nmptr->nmResp)
        call_response(nmptr);
    busy = false;
}
