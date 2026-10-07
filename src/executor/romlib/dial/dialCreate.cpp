/* Copyright 1986-1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DialogMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <QuickDraw.h>
#include <OSUtil.h>
#include <DialogMgr.h>
#include <ControlMgr.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>
#include <Iconutil.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <dial/itm.h>
#include <ctl/ctl.h>
#include <mman/mman.h>
#include <res/resource.h>
#include <textedit/textedit.h>

using namespace Executor;

void Executor::dialog_create_item(DialogPeek dp, itmp dst, itmp src,
                                  int item_no, Point base_pt)
{
    GUEST<INTEGER> *data;
    int16_t res_id;
    int gd_bpp;

    if(dst != src)
        memcpy(dst, src, ITEM_LEN(src));
    OffsetRect(&dst->itmr, base_pt.h, base_pt.v);
    data = ITEM_DATA(dst);

    gd_bpp = PIXMAP_PIXEL_SIZE(GD_PMAP(LM(MainDevice)));

    /* many items have a resource id at the beginning of the resource
     data */
    res_id = *data;

    if(dst->itmtype & ctrlItem)
    {
        Rect r;
        bool visible_p = true;
        ControlHandle ctl;

        r = dst->itmr;
        if(r.left > 8192)
        {
            visible_p = false;
            r.left = r.left - 16384;
            r.right = r.right - 16384;
        }

        if((dst->itmtype & resCtrl) == resCtrl)
        {
            Rect *ctl_rect;
            GUEST<INTEGER> top, left;

            /* ### is visibility correct here? */
            ctl = GetNewControl(res_id, (WindowPtr)dp);
            if(ctl)
            {
                ctl_rect = &CTL_RECT(ctl);
                top = ctl_rect->top;
                left = ctl_rect->left;
                if(r.top != top || r.left != left)
                    MoveControl(ctl, r.left, r.top);
            }
        }
        else
        {
            ctl = NewControl((WindowPtr)dp, &r,
                             (StringPtr)&dst->itmlen,
                             visible_p, 0, 0, 1,
                             dst->itmtype & resCtrl, 0L);
        }
        dst->itmhand = (Handle)ctl;

        ValidRect(&dst->itmr);

        {
            AuxWinHandle aux_win_h;

            aux_win_h = *lookup_aux_win((WindowPtr)dp);
            if(aux_win_h && (*aux_win_h)->dialogCItem)
            {
                Handle item_color_info_h;
                item_color_info_t *item_color_info;

                item_color_info_h = (*aux_win_h)->dialogCItem;
                item_color_info = (item_color_info_t *)*item_color_info_h;

                if(item_color_info)
                {
                    item_color_info_t *ctl_color_info;

                    ctl_color_info = &item_color_info[item_no - 1];
                    if(ctl_color_info->data || ctl_color_info->offset)
                    {
                        int color_table_bytes;
                        int color_table_offset;
                        char *color_table_base;

                        CCTabHandle color_table;

                        color_table_bytes = ctl_color_info->data;
                        color_table_offset = ctl_color_info->offset;

                        color_table_base = ((char *)item_color_info
                                            + color_table_offset);

                        color_table = (CCTabHandle)NewHandle(color_table_bytes);
                        memcpy(*color_table, color_table_base,
                               color_table_bytes);

                        SetControlColor(ctl, color_table);
                    }
                }
            }
        }
    }
    else if(dst->itmtype & (statText | editText))
    {
        PtrToHand((Ptr)data, &dst->itmhand, dst->itmlen);
        if((dst->itmtype & editText)
           && DIALOG_EDIT_FIELD(dp) == -1)
            ROMlib_dpntoteh(dp, item_no);
    }
    else if(dst->itmtype & iconItem)
    {
        Handle h = nullptr;

        if(/* CGrafPort_p (dp) */
           /* since copybits will do the right thing copying a color
	     image to a old-style grafport on the screen, we check the
	     screen depth to see if we should check for a color icon */
           gd_bpp > 2)
        {
            h = (Handle)GetCIcon(res_id);
            gui_assert(!h || CICON_P(h));
        }
        if(!h)
        {
            h = GetIcon(res_id);
            if(!h || CICON_P(h))
                warning_unexpected("dubious icon handle");
        }
        dst->itmhand = h;
    }
    else if(dst->itmtype & picItem)
    {
        dst->itmhand = (Handle)GetPicture(res_id);
    }
    else
    {
        /* useritem */
        dst->itmhand = nullptr;
    }
}

static DialogPtr
ROMlib_new_dialog_common(DialogPtr dp,
                         bool color_p,
                         CTabHandle w_ctab,
                         Handle item_color_table_h,
                         const Rect *bounds, ConstStringPtr title,
                         bool visible_p,
                         int16_t proc_id, WindowPtr behind,
                         bool go_away_flag,
                         int32_t ref_con,
                         Handle items)
{
    GUEST<INTEGER> *ip;
    INTEGER i;
    itmp itp;

    if(!dp)
        dp = (DialogPtr)NewPtr(sizeof(DialogRecord));

    if(color_p)
    {
        NewCWindow((Ptr)dp, bounds, title, visible_p,
                   proc_id, behind, go_away_flag, ref_con);
        if(w_ctab && CTAB_SIZE(w_ctab) > -1)
        {
            ThePortGuard guard(qdGlobals().thePort);
            SetWinColor(DIALOG_WINDOW(dp), w_ctab);
        }
    }
    else
        NewWindow((Ptr)dp, bounds, title, visible_p, proc_id,
                  behind, go_away_flag, ref_con);

    if(item_color_table_h)
    {
        AuxWinHandle aux_win_h;

        aux_win_h = *lookup_aux_win(dp);
        gui_assert(aux_win_h);

        (*aux_win_h)->dialogCItem = item_color_table_h;
    }

    // FIXME: #warning We no longer call TEStyleNew, this helps LB password

    ThePortGuard guard((GrafPtr)dp);

    Rect newr;

    TextFont(LM(DlgFont));
    newr.top = newr.left = 0;
    newr.bottom = bounds->bottom - bounds->top;
    newr.right = bounds->right - bounds->left;
    InvalRect(&newr);
    WINDOW_KIND(dp) = dialogKind;

    {
        Rect emptyrect = {};
        TEHandle te;

     /************************ DO NOT CHECK THIS IN ****************
	 if (color_p && item_color_table_h)
	   te = TEStyleNew (&emptyrect, &emptyrect);
	 else
	 ***************************************************************/
        te = TENew(&emptyrect, &emptyrect);

        DIALOG_TEXTH(dp) = te;
        TEAutoView(true, te);
        DisposeHandle(TE_HTEXT(te));
        TE_HTEXT(te) = nullptr;
    }

    DIALOG_EDIT_FIELD(dp) = -1;
    DIALOG_EDIT_OPEN(dp) = 0;
    DIALOG_ADEF_ITEM(dp) = 1;

    DIALOG_ITEMS(dp) = items;
    if(items)
    {
        Point zero_pt;

        memset(&zero_pt, '\000', sizeof zero_pt);

        MoveHHi(items);

        HLockGuard guard(items);

        int item_no;

        ip = (GUEST<INTEGER> *)*items;
        itp = (itmp)(ip + 1);
        i = *ip;
        item_no = 1;
        while(i-- >= 0)
        {
            dialog_create_item((DialogPeek)dp, itp, itp, item_no,
                               zero_pt);

            BUMPIP(itp);
            item_no++;
        }
    }

    return (DialogPtr)dp;
}

/* IM-MTE calls this NewColorDialog () */
DialogPtr Executor::C_NewColorDialog(void* storage, const Rect *bounds, ConstStringPtr title,
                                  Boolean visible_p, INTEGER proc_id,
                                  WindowPtr behind, Boolean go_away_flag,
                                  LONGINT ref_con, Handle items) /* IMI-412 */
{
    return ROMlib_new_dialog_common((DialogPtr)storage,
                                                /* color */ true, nullptr, nullptr,
                                                bounds, title, visible_p, proc_id,
                                                behind, go_away_flag, ref_con,
                                                items);
}

DialogPtr Executor::C_NewDialog(void* storage, const Rect *bounds, ConstStringPtr title,
                                Boolean visible_p, INTEGER proc_id,
                                WindowPtr behind, Boolean go_away_flag,
                                LONGINT ref_con, Handle items) /* IMI-412 */
{
    return ROMlib_new_dialog_common((DialogPtr)storage,
                                    /* not color */ false, nullptr, nullptr,
                                    bounds, title, visible_p, proc_id,
                                    behind, go_away_flag, ref_con, items);
}

void Executor::dialog_compute_rect(Rect *dialog_rect, Rect *dst_rect,
                                   int position)
{
    Rect *screen_rect;
    int dialog_width, dialog_height;
    int screen_width, screen_height;

    dialog_width = RECT_WIDTH(dialog_rect);
    dialog_height = RECT_HEIGHT(dialog_rect);

    screen_rect = &GD_RECT(LM(MainDevice));
    screen_width = RECT_WIDTH(screen_rect);
    screen_height = RECT_HEIGHT(screen_rect);

    switch(position)
    {
        /* #### find out what `stagger' position means */

        default:
        case noAutoCenter:
            /* noAutoCenter */
            *dst_rect = *dialog_rect;
            break;

        case alertPositionParentWindow:
        case dialogPositionParentWindow:
        {
            WindowPtr parent;

            parent = FrontWindow();
            if(parent)
            {
                Rect *parent_rect;
                int top;
                int left;

                parent_rect = &PORT_RECT(parent);

                top = parent_rect->top + 16;
                left = ((parent_rect->left
                         + parent_rect->right)
                            / 2
                        + dialog_width / 2);

                SetRect(dst_rect,
                        left, top,
                        left + dialog_width, top + dialog_height);
                break;
            }
            /* else fall through */
        }

        case alertPositionMainScreen:
        case dialogPositionMainScreen:
        case alertPositionParentWindowScreen:
        case dialogPositionParentWindowScreen:
            SetRect(dst_rect,
                    (screen_width - dialog_width) / 2,
                    (screen_height - dialog_height) / 3,
                    (screen_width - dialog_width) / 2 + dialog_width,
                    (screen_height - dialog_height) / 3 + dialog_height);
            break;
    }
}

DialogPtr Executor::C_GetNewDialog(INTEGER id, void* dst,
                                   WindowPtr behind) /* IMI-413 */
{
    dlogh dialog_res_h;
    Handle dialog_item_list_res_h;
    Handle item_ctab_res_h;
    DialogPtr retval;
    Handle dialog_ctab_res_h;
    bool color_p;

    dialog_res_h = (dlogh)ROMlib_getrestid("DLOG"_4, id);

    dialog_item_list_res_h = ROMlib_getrestid("DITL"_4,
                                              (*dialog_res_h)->dlgditl);
    HandToHand(inout(dialog_item_list_res_h));

    if(!dialog_res_h || !dialog_item_list_res_h)
        return nullptr;

    dialog_ctab_res_h = ROMlib_getrestid("dctb"_4, id);
    item_ctab_res_h = ROMlib_getrestid("ictb"_4, id);

    color_p = (dialog_ctab_res_h || item_ctab_res_h);

    HLockGuard guard(dialog_res_h);
    Rect adjusted_rect;

    dialog_compute_rect(&(*dialog_res_h)->dlgr,
                        &adjusted_rect,
                        (DIALOG_RES_HAS_POSITION_P(dialog_res_h)
                             ? toHost(DIALOG_RES_POSITION(dialog_res_h))
                             : noAutoCenter));

    /* if there is a dialog color table resource make this a color
	  dialog */
    retval
        = ROMlib_new_dialog_common((DialogPtr)dst, color_p,
                                   (CTabHandle)dialog_ctab_res_h,
                                   item_ctab_res_h,
                                   &adjusted_rect,
                                   (StringPtr)(&(*dialog_res_h)->dlglen),
                                   (*dialog_res_h)->dlgvis,
                                   (*dialog_res_h)->dlgprocid,
                                   behind,
                                   (*dialog_res_h)->dlggaflag,
                                   (*dialog_res_h)->dlgrc,
                                   dialog_item_list_res_h);

    return retval;
}

void Executor::C_CloseDialog(DialogPtr dp) /* IMI-413 */
{
    Handle items;
    GUEST<INTEGER> *ip;
    INTEGER i;
    itmp itp;

    items = DIALOG_ITEMS(dp);
    if(items)
    {
        /* #### should `items' be locked? */
        ip = (GUEST<INTEGER> *)*items;
        i = *ip;
        itp = (itmp)(ip + 1);
        while(i-- >= 0)
        {
            if(itp->itmtype & (editText | statText))
                DisposeHandle((Handle)itp->itmhand);
            BUMPIP(itp);
        }
    }
    CloseWindow((WindowPtr)dp);
}

void Executor::C_DisposeDialog(DialogPtr dp) /* IMI-415 */
{
    TEHandle teh;

    CloseDialog(dp);
    DisposeHandle(((DialogPeek)dp)->items);
    teh = DIALOG_TEXTH(dp);

    /* accounted for elsewhere */
    TE_HTEXT(teh) = nullptr;
    TEDispose(teh);

    DisposePtr((Ptr)dp);
}

/* see dialAlert.c for CouldDialog, FreeDialog */
