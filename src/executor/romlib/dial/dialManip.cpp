/* Copyright 1986-1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DialogMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <ControlMgr.h>
#include <ControlMgr8.h>
#include <DialogMgr.h>
#include <OSUtil.h>
#include <MemoryMgr.h>

#include <dial/itm.h>
#include <dial/dial.h>
#include <wind/wind.h>
#include <ctl/ctl.h>
#include <quickdraw/cquick.h>
#include <mman/mman.h>
#include <textedit/textedit.h>

using namespace Executor;

static bool control_item_text(Handle h, ConstStringPtr set, StringPtr get);
static itmp htoip(Handle h, WindowPeek *wp_return, int16_t *nop_return,
                  SignedByte *flags_return);

void Executor::C_ParamText(ConstStringPtr p0, ConstStringPtr p1, ConstStringPtr p2,
                           ConstStringPtr p3) /* IMI-421 */
{
    if(p0)
        PtrToXHand((Ptr)p0, LM(DAStrings)[0], (LONGINT)p0[0] + 1);
    if(p1)
        PtrToXHand((Ptr)p1, LM(DAStrings)[1], (LONGINT)p1[0] + 1);
    if(p2)
        PtrToXHand((Ptr)p2, LM(DAStrings)[2], (LONGINT)p2[0] + 1);
    if(p3)
        PtrToXHand((Ptr)p3, LM(DAStrings)[3], (LONGINT)p3[0] + 1);
}

itmp Executor::ROMlib_dpnotoip(DialogPeek dp, INTEGER itemno,
                               SignedByte *flags) /* INTERNAL */
{
    Handle items;
    GUEST<INTEGER> *intp;
    itmp retval;

    items = DIALOG_ITEMS(dp);
    *flags = hlock_return_orig_state(items);
    intp = (GUEST<INTEGER> *)*items;
    if(itemno <= 0 || itemno > *intp + 1)
        retval = 0;
    else
    {
        retval = (itmp)(intp + 1);
        while(--itemno)
            BUMPIP(retval);
    }
    return retval;
}

static bool control_item_text(Handle h, ConstStringPtr set, StringPtr get);
static itmp htoip(Handle h, WindowPeek *wp_return, int16_t *nop_return,
                  SignedByte *flags_return)
{
    WindowPeek wp;
    GUEST<INTEGER> *ip;
    INTEGER i, nop;
    itmp retval;

    for(wp = LM(WindowList); wp; wp = WINDOW_NEXT_WINDOW(wp))
    {
        if(WINDOW_KIND(wp) == dialogKind
           || WINDOW_KIND(wp) < 0)
        {
            Handle items;
            SignedByte flags;

            items = DIALOG_ITEMS(wp);
            flags = hlock_return_orig_state(items);

            ip = (GUEST<INTEGER> *)*items;
            retval = (itmp)(ip + 1);
            for(i = *ip + 1, nop = 1; i--; BUMPIP(retval))
            {
                if(retval->itmhand == h)
                {
                    *wp_return = wp;
                    *nop_return = nop;
                    *flags_return = flags;
                    return retval;
                }
                nop++;
            }
            HSetState(items, flags);
        }
    }
    return nullptr;
}

void Executor::C_GetDialogItem(DialogPtr dp, INTEGER itemno, GUEST<INTEGER> *itype,
                          GUEST<Handle> *item, Rect *r) /* IMI-421 */
{
    SignedByte flags;
    itmp ip = ROMlib_dpnotoip((DialogPeek)dp, itemno, &flags);

    if(ip)
    {
        if(itype)
            *itype = (INTEGER)ip->itmtype;
#if 0
	else
	  {
	    /* test on Mac shows unconditional write of *itype */
	    /* of course this is very rude, but that's what the Mac did
	       when we tested it.  Perhaps they've fixed that now and we
	       too should fix it.  ARGH! */
	   *(INTEGER *) (US_TO_SYN68K(itype)) = (INTEGER) ip->itmtype;
          }
          // but why should we duplicate this kind of bug?
#endif
        if(item) /* We didn't test what happens when item is 0 on Mac */
            *item = ip->itmhand;
        if(r) /* test on Mac shows r will not be written if 0 */
            *r = ip->itmr;
    }
    HSetState(((DialogPeek)dp)->items, flags);
}

static void
settexth(DialogPeek dp, itmp ip, int item_no)
{
    GrafPtr current_port;
    Handle item_text_h;
    TEHandle te;
    TEPtr tep;
    int16_t length;

    current_port = qdGlobals().thePort;

    te = DIALOG_TEXTH(dp);
    tep = *te;

    TEP_DEST_RECT(tep) = TEP_VIEW_RECT(tep) = ITEM_RECT(ip);

#if 0
  /* #### this was commented out, otherwise it's origin is unknown */
  InsetRect (TEP_VIEW_RECT (tep), -3, -3);
#endif

    item_text_h = ITEM_H(ip);
    length = GetHandleSize(item_text_h);
    TEP_LENGTH(tep) = length;
    /* this is not a leak, always a duplicate */
    TEP_HTEXT(tep) = item_text_h;

    /* set up the text styles */
    {
        int16_t te_style_size, te_style_font;
        Style te_style_face;
        RGBColor te_style_color;
        uint16_t flags;
        item_style_info_t style_info;

        te_style_font = PORT_TX_FONT(dp);
        te_style_face = PORT_TX_FACE(dp);
        te_style_size = PORT_TX_SIZE(dp);

        {
            ThePortGuard guard((GrafPtr)dp);
            GetForeColor(&te_style_color);
        }

        if(get_item_style_info((DialogPtr)dp, item_no, &flags, &style_info))
        {
            if(flags & TEdoFont)
                te_style_font = style_info.font;
            if(flags & TEdoFace)
                te_style_face = style_info.face;
            if(flags & TEdoSize)
                te_style_size = style_info.size;

            if(flags & TEdoColor)
                te_style_color = style_info.foreground;
#if 0
	/* ##### */
	if (flags & doBColor)
	  ... = style_info.background;
#endif
        }

        /* #### this code should be somehow unified with `TESetText ()',
       although that cannot be called directly */

        if(TEP_STYLIZED_P(tep))
        {
            TEStyleHandle te_style;
            STHandle style_table;
            FontInfo finfo;
            GUEST<int16_t> tx_font_save_x, tx_size_save_x;
            GUEST<Style> tx_face_save;

            te_style = TE_GET_STYLE(te);
            HASSIGN_2(te_style,
                      nRuns, 1,
                      nStyles, 1);

            SetHandleSize((Handle)te_style,
                          TE_STYLE_SIZE_FOR_N_RUNS(1));
            (*te_style)->runs[0].startChar = 0;
            (*te_style)->runs[0].styleIndex = 0;
            (*te_style)->runs[1].startChar = length + 1;
            (*te_style)->runs[1].styleIndex = -1;
            style_table = TE_STYLE_STYLE_TABLE(te_style);
            SetHandleSize((Handle)style_table,
                          STYLE_TABLE_SIZE_FOR_N_STYLES(1));

            tx_font_save_x = PORT_TX_FONT(current_port);
            tx_size_save_x = PORT_TX_SIZE(current_port);
            tx_face_save = PORT_TX_FACE(current_port);

            PORT_TX_FONT(current_port) = te_style_font;
            PORT_TX_SIZE(current_port) = te_style_size;
            PORT_TX_FACE(current_port) = te_style_face;

            GetFontInfo(&finfo);

            PORT_TX_FONT(current_port) = tx_font_save_x;
            PORT_TX_SIZE(current_port) = tx_size_save_x;
            PORT_TX_FACE(current_port) = tx_face_save;

            HASSIGN_7(style_table,
                      stCount, 1,
                      stFont, te_style_font,
                      stFace, te_style_face,
                      stSize, te_style_size,
                      stColor, te_style_color,
                      stHeight, finfo.ascent
                                   + finfo.descent
                                   + finfo.leading,
                      stAscent, finfo.ascent);
        }
        else
        {
            TEP_TX_FONT(tep) = te_style_font;
            TEP_TX_SIZE(tep) = te_style_size;
            TEP_TX_FACE(tep) = te_style_face;
        }
    }

    TEP_SEL_START(tep) = TEP_SEL_END(tep) = 0;

    TECalText(te);
    if(WINDOW_VISIBLE(dp))
        TEActivate(te);

    DIALOG_EDIT_FIELD(dp) = item_no - 1;
    DIALOG_EDIT_OPEN(dp) = !(ITEM_TYPE(ip) & itemDisable);
}

void Executor::C_SetDialogItem(DialogPtr dp, INTEGER itemno, INTEGER itype,
                          Handle item, const Rect *r) /* IMI-421 */
{

    SignedByte flags;
    itmp ip = ROMlib_dpnotoip((DialogPeek)dp, itemno, &flags);

    if(ip)
    {
        /* MacPhoenix: the item's own control as its handle (Navigation
           Services' static text): the item keeps its text, as 8.x
           (appr dlgitems.setctl) */
        if(item && item == (Handle)ROMlib_dialog_item_control(dp, itemno))
            item = ip->itmhand;
        ip->itmtype = itype;
        ip->itmhand = item;
        ip->itmr = *r;
        /* MacPhoenix: with the control hierarchy (8.x), a control item's
           control goes where its item goes (appr dlgitems.set; Navigation
           Services lays its dialogs out this way) */
        ROMlib_dialog_item_control_sync(dp, itemno, r);
        GUEST<ControlHandle> root;
        if((itype & ctrlItem) && item && GetRootControl(dp, &root) == noErr)
        {
            ControlHandle ctl = (ControlHandle)item;
            Rect old = CTL_RECT(ctl);
            if(old.top != r->top || old.left != r->left)
                MoveControl(ctl, r->left, r->top);
            if(old.bottom - old.top != r->bottom - r->top
               || old.right - old.left != r->right - r->left)
                SizeControl(ctl, r->right - r->left, r->bottom - r->top);
        }
        if(itemno - 1 == DIALOG_EDIT_FIELD(dp)
           && (itype & editText)
           && !(itype & itemDisable))
        {
            TEDeactivate(DIALOG_TEXTH(dp));
            if(item)
                settexth((DialogPeek)dp, ip, itemno);
            else
                warning_unexpected("");
        }
    }
    HSetState(((DialogPeek)dp)->items, flags);
}

void Executor::C_GetDialogItemText(Handle item, StringPtr text) /* IMI-422 */
{
    Size hs;

    if(text && control_item_text(item, nullptr, text))
        return;
    if(text)
    {
        if(!item)
            text[0] = 0;
        else
        {
            hs = GetHandleSize(item);
            if(hs > 255) /* can't strassign with no leading count */
                hs = 255;
            text[0] = hs;
            BlockMoveData(*item, (Ptr)text + 1, hs);
        }
    }
}

/* MacPhoenix: a handle that is a control of a window (8.x programs give
   SetDialogItemText and GetDialogItemText the controls
   GetDialogItemAsControl answers: BBEdit Lite 6.1 an alert's button) */
static bool is_window_control(Handle h)
{
    for(WindowPeek wp = LM(WindowList); wp; wp = WINDOW_NEXT_WINDOW(wp))
        for(ControlHandle c = WINDOW_CONTROL_LIST(wp); c; c = CTL_NEXT_CONTROL(c))
            if((Handle)c == h)
                return true;
    return false;
}

/* The text of an item's control, as 8.x (appr dlgitems.settext): a text
   item's control stands for the item's text; another control takes it as
   its 'text' data if its definition has that (edit and static text),
   else nothing changes (a button keeps its title, the handle its size);
   'text': kControlEditTextTextTag, kControlStaticTextTextTag. */
static bool control_item_text(Handle h, ConstStringPtr set, StringPtr get)
{
    if(!h || !is_window_control(h))
        return false;
    ControlHandle c = (ControlHandle)h;
    DialogPtr dp;
    int item;
    if(ROMlib_dialog_item_of_control(c, &dp, &item))
    {
        GUEST<INTEGER> type;
        GUEST<Handle> th;
        Rect r;
        GetDialogItem(dp, item, &type, &th, &r);
        if((INTEGER)type & (statText | editText))
        {
            if(set)
                SetDialogItemText(th, set);
            else
                GetDialogItemText(th, get);
        }
        return true;
    }
    if(set)
        SetControlData(c, kControlEntireControl, "text"_4, set[0], (Ptr)set + 1);
    else
    {
        GUEST<Size> size;
        if(GetControlData(c, kControlEntireControl, "text"_4, 255, (Ptr)get + 1,
                          &size) == noErr)
            get[0] = std::min<Size>(size, 255);
    }
    return true;
}

void Executor::C_SetDialogItemText(Handle item, ConstStringPtr text) /* IMI-422 */
{
    if(control_item_text(item, text, nullptr))
        return;
    if(item) /* put this test in for Golf 6.0's sake */
    {
        WindowPeek wp;
        SignedByte flags;
        int16_t no;
        itmp ip;
        Size hs;

        hs = text[0];
        /* test on Mac suggests not reallochandle */
        SetHandleSize(item, hs);

        /* test on Mac shows that if the size can't be set, the copy
	 isn't done, but the rest is */
        if(LM(MemErr) == noErr)
            BlockMoveData((Ptr)&text[1], *item, hs);
        ip = htoip(item, &wp, &no, &flags);
        if(ip)
        {
            ThePortGuard guard((WindowPtr)wp);
            ROMlib_drawiptext((DialogPtr)wp, ip, no);
            if(no == DIALOG_EDIT_FIELD(wp) + 1)
            {
                TEHandle text_h;

                text_h = DIALOG_TEXTH(wp);

                TE_CARET_STATE(text_h) = 255;
                TESetText((Ptr)&text[1], text[0],
                          text_h);
            }
            ValidRect(&ip->itmr);
            HSetState(DIALOG_ITEMS(wp), flags);
        }
    }
}

/*
 * fields of TERec that need to be updated:
 * destRect, viewRect, selStart, selEnd, caretState, teLength, hText, nLines,
 * lineStarts
 */

void Executor::ROMlib_dpntoteh(DialogPeek dp, INTEGER no) /* INTERNAL */
{
    SignedByte flags;
    itmp ip;
    GUEST<INTEGER> *intp;
    INTEGER num;

    if(no == 0)
    {
        /* special case ... find next */
        intp = (GUEST<INTEGER> *)(*dp->items);
        num = *intp + 1;
        ip = ROMlib_dpnotoip(dp, no = dp->editField + 1, &flags);
        do
        {
            if(ip)
                BUMPIP(ip);
            else
                warning_unexpected("no (inherited from editField) = %d", no);
            if(!ip || ++no > num)
            {
                ip = (itmp)(intp + 1);
                no = 1;
            }
        } while(!(ip->itmtype & editText));
    }
    else
        ip = ROMlib_dpnotoip(dp, no, &flags);
    if(ip && (dp->editField != no - 1))
    {
        if(dp->editField != -1)
            TEDeactivate(dp->textH);
        settexth(dp, ip, no);
    }
    HSetState(((DialogPeek)dp)->items, flags);
}

void Executor::C_SelectDialogItemText(DialogPtr dp, INTEGER itemno, INTEGER start,
                          INTEGER stop) /* IMI-422 */
{
    ROMlib_dpntoteh((DialogPeek)dp, itemno);
    TESetSelect(start, stop, DIALOG_TEXTH(dp));
}

INTEGER Executor::GetAlertStage() /* IMI-422 */
{
    return LM(ACount);
}

void Executor::ResetAlertStage() /* IMI-423 */
{
    LM(ACount) = 0;
}

void Executor::C_HideDialogItem(DialogPtr dp, INTEGER item) /* IMIV-59 */
{
    itmp ip;
    Rect r;
    SignedByte flags;

    ip = ROMlib_dpnotoip((DialogPeek)dp, item, &flags);
    if(ip && ip->itmr.left < 8192)
    {
        r = ip->itmr;
        ip->itmr.left = ip->itmr.left + 16384;
        ip->itmr.right = ip->itmr.right + 16384;
        ROMlib_dialog_item_control_sync(dp, item, &ip->itmr);
        if(ip->itmtype & editText)
        {
            InsetRect(&r, -3, -3);
            if(item - 1 == DIALOG_EDIT_FIELD(dp))
            {
                TEDeactivate(DIALOG_TEXTH(dp));
                DIALOG_EDIT_FIELD(dp) = -1;
            }
        }
        else if(ip->itmtype & ctrlItem)
        {
            ControlHandle ctl;

            ctl = (ControlHandle)ip->itmhand;
            CTL_VIS(ctl) = 0;

            if(item == DIALOG_ADEF_ITEM(dp))
                InsetRect(&r, -4, -4);
        }

        ThePortGuard guard(dp);
        EraseRect(&r);
        InvalRect(&r);
    }
    HSetState(DIALOG_ITEMS(dp), flags);
}

void Executor::C_ShowDialogItem(DialogPtr dp, INTEGER item) /* IMIV-59 */
{
    itmp ip;
    Rect r;
    SignedByte flags;

    ip = ROMlib_dpnotoip((DialogPeek)dp, item, &flags);
    if(!ip) /* MacPhoenix: no such item, nothing to show */
    {
        HSetState(DIALOG_ITEMS(dp), flags);
        return;
    }
    if(ip->itmr.left > 8192)
    {
        ip->itmr.left = ip->itmr.left - 16384;
        ip->itmr.right = ip->itmr.right - 16384;
        ROMlib_dialog_item_control_sync(dp, item, &ip->itmr);
        r = ip->itmr;
        if(ip->itmtype & editText)
        {
            InsetRect(&r, -3, -3);
            if(item - 1 == DIALOG_EDIT_FIELD(dp))
                TEActivate(DIALOG_TEXTH(dp));
        }
        else if(ip->itmtype & ctrlItem)
        {
            ControlHandle ctl;

            ctl = (ControlHandle)ip->itmhand;
            CTL_VIS(ctl) = 255;

            if(item == DIALOG_ADEF_ITEM(dp))
                InsetRect(&r, -4, -4);
        }

        ThePortGuard guard(dp);
        InvalRect(&r);
    }
    if(ip->itmtype & ctrlItem)
    {
        ControlHandle ctl;

        ctl = (ControlHandle)ip->itmhand;
        ShowControl(ctl);
    }
    HSetState(DIALOG_ITEMS(dp), flags);
}
