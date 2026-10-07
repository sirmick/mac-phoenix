/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DialogMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <ResourceMgr.h>
#include <WindowMgr.h>
#include <DialogMgr.h>
#include <MemoryMgr.h>

#include <OSUtil.h>
#include <dial/itm.h>
#include <res/resource.h>
#include <quickdraw/cquick.h>
#include <mman/mman.h>
#include <prefs/options.h>
#include <dial/dial.h>

using namespace Executor;

int16_t alert_extra_icon_id = -32768;

static icon_item_template_t icon_item_template = {
    /* item count - 1 */
    0,
    nullptr,
    {
        10,
        20,
        42,
        52,
    },
    (1 << 7) | (iconItem),
    2,

    /* to be filled in later */ (short)-1,
};

INTEGER Executor::C_Alert(INTEGER id, ModalFilterUPP fp) /* IMI-418 */
{
    alth ah;
    Handle ih;
    INTEGER n, defbut;
    GUEST<INTEGER> hit;
    Handle alert_ctab_res_h;
    Handle item_ctab_res_h;

    if(id != LM(ANumber))
    {
        LM(ANumber) = id;
        LM(ACount) = 0;
    }
    ah = (alth)GetResource("ALRT"_4, id);
    if(!ah)
    {
        Beep(1);
        return -1;
    }

    LoadResource((Handle)ah);
    n = ((*ah)->altstag >> (4 * LM(ACount))) & 0xF;
    LM(ACount) = LM(ACount) + 1;
    if(LM(ACount) > 3)
        LM(ACount) = 3;
    Beep(n & 3);
    if(!(n & 4))
        return -1;

    ih = GetResource("DITL"_4, (*ah)->altiid);
    if(!ih)
        return -1;
    LoadResource(ih);
    alert_ctab_res_h = ROMlib_getrestid("actb"_4, (*ah)->altiid);
    item_ctab_res_h = ROMlib_getrestid("ictb"_4, (*ah)->altiid);
    GUEST<Handle> h = ih;
    HandToHand(&h);

    TheGDeviceGuard guard(LM(MainDevice));

    Rect adjusted_rect;
    bool color_p;
    DialogPeek dp;
    SignedByte flags;
    itmp ip;

    {
        HLockGuard hGuard(ah);
        dialog_compute_rect(&(*ah)->altr,
                            &adjusted_rect,
                            (ALERT_RES_HAS_POSITION_P(ah)
                                 ? toHost(ALERT_RES_POSITION(ah))
                                 : noAutoCenter));
    }

    color_p = ((alert_ctab_res_h
                && CTAB_SIZE((CTabHandle)alert_ctab_res_h) != -1)
               || item_ctab_res_h);
    if(color_p)
        dp = ((DialogPeek)
                  NewColorDialog(nullptr, &adjusted_rect,
                             (StringPtr) "", false, dBoxProc,
                             (WindowPtr)-1, false, 0L, h));
    else
        dp = ((DialogPeek)
                  NewDialog(nullptr, &adjusted_rect,
                            (StringPtr) "", false, dBoxProc,
                            (WindowPtr)-1, false, 0L, h));

    if(color_p)
    {
        {
            ThePortGuard portGuard;
            SetWinColor(DIALOG_WINDOW(dp),
                        (CTabHandle)alert_ctab_res_h);
        }

        if(item_ctab_res_h)
        {
            AuxWinHandle aux_win_h;

            aux_win_h = *lookup_aux_win(DIALOG_WINDOW(dp));
            gui_assert(aux_win_h);

            (*aux_win_h)->dialogCItem = item_ctab_res_h;
        }
    }

    if(alert_extra_icon_id != -32768)
    {
        Handle icon_item_h;

        icon_item_h = NewHandle(sizeof icon_item_template);
        icon_item_template.res_id = alert_extra_icon_id;
        memcpy(*icon_item_h, &icon_item_template,
               sizeof icon_item_template);

        AppendDITL((DialogPtr)dp, icon_item_h, overlayDITL);

        alert_extra_icon_id = -32768;

        DisposeHandle(icon_item_h);
    }

    ShowWindow((WindowPtr)dp);

    {
        ThePortGuard portGuard(FrontWindow());
        defbut = 1 + ((n & 8) >> 3);
        ip = ROMlib_dpnotoip(dp, defbut, &flags);
        if(ip)
        {
            Rect r;

            r = ip->itmr;
            PenSize(3, 3);
            InsetRect(&r, -4, -4);
            if(!ROMlib_rect_buttons)
                FrameRoundRect(&r, 16, 16);
            else
                FrameRect(&r);
        }
        dp->aDefItem = defbut;
        ModalDialog(fp, &hit);
    }
    HSetState(DIALOG_ITEMS(dp), flags);
    DisposeDialog((DialogPtr)dp);

    return hit;
}

INTEGER Executor::C_StopAlert(INTEGER id, ModalFilterUPP fp) /* IMI-419 */
{
    alert_extra_icon_id = stopIcon;
    return Alert(id, fp);
}

INTEGER Executor::C_NoteAlert(INTEGER id, ModalFilterUPP fp) /* IMI-420 */
{
    alert_extra_icon_id = noteIcon;
    return Alert(id, fp);
}

INTEGER Executor::C_CautionAlert(INTEGER id, ModalFilterUPP fp) /* IMI-420 */
{
    alert_extra_icon_id = cautionIcon;
    return Alert(id, fp);
}

static Handle lockres(ResType rt, INTEGER id, Boolean flag)
{
    Handle retval;

    SetResLoad(true);
    if((retval = GetResource(rt, id)))
    {
        if(flag)
            HNoPurge(retval);
        else
            HPurge(retval);
    }
    return retval;
}

#define RESCTL (ctrlItem | resCtrl)

static void lockditl(INTEGER id, Boolean flag)
{
    Handle ih, h;
    INTEGER nitem, procid;
    itmp ip;

    if((ih = lockres("DITL"_4, id, flag)))
    {
        nitem = **(GUEST<GUEST<INTEGER> *> *)ih;
        ip = (itmp)((INTEGER *)*ih + 1);
        while(nitem-- >= 0)
        {
            if((ip->itmtype & RESCTL) == RESCTL)
            {
                h = lockres("CNTL"_4, *(GUEST<INTEGER> *)(&(ip->itmlen) + 1),
                            flag);
                procid = **(GUEST<GUEST<INTEGER> *> *)h + 8;
                lockres("CDEF"_4, procid >> 4, flag);
            }
            else if(ip->itmtype & iconItem)
                lockres("ICON"_4, *(GUEST<INTEGER> *)(&(ip->itmlen) + 1), flag);
            else if(ip->itmtype & picItem)
                lockres("PICT"_4, *(GUEST<INTEGER> *)(&(ip->itmlen) + 1), flag);
            BUMPIP(ip);
        }
    }
}

static void lockalert(INTEGER id, Boolean flag)
{
    alth ah;

    if((ah = (alth)lockres("ALRT"_4, id, flag)))
    {
        lockditl((*ah)->altiid, flag);
        lockres("WDEF"_4, dBoxProc >> 4, flag);
        lockres("ICON"_4, stopIcon, flag);
        lockres("ICON"_4, noteIcon, flag);
        lockres("ICON"_4, cautionIcon, flag);
    }
}

void Executor::C_CouldAlert(INTEGER id) /* IMI-420 */
{
    lockalert(id, true);
}

void Executor::C_FreeAlert(INTEGER id) /* IMI-420 */
{
    lockalert(id, false);
}

static void lockdialog(INTEGER id, Boolean flag)
{
    dlogh dh;

    if((dh = (dlogh)lockres("DLOG"_4, id, flag)))
    {
        lockditl((*dh)->dlgditl, flag);
        lockres("WDEF"_4, (*dh)->dlgprocid >> 4, flag);
    }
}

void Executor::C_CouldDialog(INTEGER id) /* IMI-415 */
{
    lockdialog(id, true);
}

void Executor::C_FreeDialog(INTEGER id) /* IMI-415 */
{
    lockdialog(id, false);
}
