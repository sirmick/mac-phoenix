/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* color window manager functions; introduced in IM-V or beyond */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <WindowMgr.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>

#include <set>

using namespace Executor;

/* return a pointer to the auxilary window record
   associated with `w' */

AuxWinHandle Executor::default_aux_win = nullptr;

static const RGBColor BLACK_RGB = {
    0, 0, 0,
};
static const RGBColor WHITE_RGB = {
    (unsigned short)0xFFFF, (unsigned short)0xFFFF, (unsigned short)0xFFFF,
};

/* bw window colortable */
const ColorSpec default_bw_win_ctab[] = {
    { wContentColor, WHITE_RGB },
    { wFrameColor, BLACK_RGB },
    { wTextColor, BLACK_RGB },
    { wHiliteColor, BLACK_RGB },
    { wTitleBarColor, WHITE_RGB },
    { wHiliteColorLight, WHITE_RGB },
    { wHiliteColorDark, BLACK_RGB },
    { wTitleBarLight, WHITE_RGB },
    { wTitleBarDark, BLACK_RGB },
    { wDialogLight, BLACK_RGB },
    { wDialogDark, BLACK_RGB },
    { wTingeLight, BLACK_RGB },
    { wTingeDark, BLACK_RGB },
};

static const RGBColor LT_BLUISH_RGB = { (unsigned short)0xCCCC, (unsigned short)0xCCCC, (unsigned short)0xFFFF };
static const RGBColor DK_BLUISH_RGB = { (unsigned short)0x3333, (unsigned short)0x3333, (unsigned short)0x6666 };
/* the default `bluish' window color table */
const ColorSpec Executor::default_color_win_ctab[] = {
    { wContentColor, WHITE_RGB },
    { wFrameColor, BLACK_RGB },
    { wTextColor, BLACK_RGB },
    { wHiliteColor, BLACK_RGB },
    { wTitleBarColor, WHITE_RGB },
    { wHiliteColorLight, WHITE_RGB },
    { wHiliteColorDark, BLACK_RGB },
    { wTitleBarLight, WHITE_RGB },
    { wTitleBarDark, BLACK_RGB },
    { wDialogLight, LT_BLUISH_RGB },
    { wDialogDark, BLACK_RGB },
    { wTingeLight, LT_BLUISH_RGB },
    { wTingeDark, DK_BLUISH_RGB },
};
#undef DK_BLUISH_RGB
#undef LT_BLUISH_RGB

static const RGBColor GRAY_RGB = { (unsigned short)0x8888, (unsigned short)0x8888, (unsigned short)0x8888 };
/* stolen from the default colortable excel
   tries to install */
const ColorSpec default_system6_color_win_ctab[] = {
    { wContentColor, WHITE_RGB },
    { wFrameColor, BLACK_RGB },
    { wTextColor, BLACK_RGB },
    { wHiliteColor, BLACK_RGB },
    { wTitleBarColor, WHITE_RGB },
};
#undef GRAY_RGB
#undef WHITE_RGB
#undef BLACK_RGB

void Executor::wind_color_init(void)
{
    /* initalize the default window colortable */
    TheZoneGuard guard(LM(SysZone));

    default_aux_win = (AuxWinHandle)NewHandle(sizeof(AuxWinRec));
    (*default_aux_win)->awNext = 0;
    (*default_aux_win)->awOwner = 0;
    (*default_aux_win)->awCTable
        = (CTabHandle)GetResource("wctb"_4, 0);
    (*default_aux_win)->dialogCItem = 0;
    (*default_aux_win)->awFlags = 0;
    (*default_aux_win)->awReserved = 0;
    (*default_aux_win)->awRefCon = 0;
}

GUEST<AuxWinHandle> *
Executor::lookup_aux_win(WindowPtr w)
{
    GUEST<AuxWinHandle> *t;

    for(t = &LM(AuxWinHead);
        *t && (**t)->awOwner != w;
        t = &(**t)->awNext)
        ;
    return t;
}

/* MacPhoenix: the colour tables the Window and Dialog Managers take from
   'wctb', 'dctb' and 'actb' resources are copies the window owns: an
   application may release the resource once the window is up (7.5.5's
   Color Picker releases its 'dctb'), and the window went on reading a freed
   handle, garbage for its content colour (the picker's background came out
   green, blue or teal). */
static std::set<CTabHandle> owned_ctabs;

CTabHandle Executor::ROMlib_window_ctab_copy(Handle res)
{
    if(!res)
        return nullptr;
    if(!*res)
        LoadResource(res);
    Handle copy = res;
    if(HandToHand(inout(copy)) != noErr)
        return (CTabHandle)res;
    owned_ctabs.insert((CTabHandle)copy);
    return (CTabHandle)copy;
}

void Executor::ROMlib_window_ctab_release(CTabHandle ctab)
{
    if(ctab && owned_ctabs.erase(ctab))
        DisposeHandle((Handle)ctab);
}

void Executor::C_SetWinColor(WindowPtr w, CTabHandle new_w_ctab)
{
    if(w)
    {
        AuxWinHandle aux_w;

        aux_w = *lookup_aux_win(w);

        if(!aux_w)
        {
            GUEST<AuxWinHandle> t_aux_w;

            t_aux_w = LM(AuxWinHead);
            aux_w = (AuxWinHandle)NewHandle(sizeof(AuxWinRec));
            LM(AuxWinHead) = aux_w;
            (*aux_w)->awNext = t_aux_w;
            (*aux_w)->awOwner = (WindowPtr)w;
            /* FIXME: copy? */
            (*aux_w)->awCTable = new_w_ctab;
            (*aux_w)->dialogCItem = 0;
            (*aux_w)->awFlags = /* (proc_id & 0x0F) */ 0;
            (*aux_w)->awReserved = 0;
            (*aux_w)->awRefCon = 0;
        }
        else
        {
            CTabHandle old = (*aux_w)->awCTable;
            (*aux_w)->awCTable = new_w_ctab;
            if(old != new_w_ctab)
                ROMlib_window_ctab_release(old);
        }

        if(CGrafPort_p(w))
        {
            ColorSpec *w_ctab_table;
            RGBColor *color;

            w_ctab_table = CTAB_TABLE(new_w_ctab);
            color = &w_ctab_table[wContentColor].rgb;

            CPORT_RGB_BK_COLOR(w) = *color;

            /* pick the best color and store it into window's port's
	     bkColor field */
            PORT_BK_COLOR(w) = Color2Index(color);

            if(WINDOW_VISIBLE(w))
            {
                ThePortGuard guard(w);
                RgnHandle t;
                t = NewRgn();

                CopyRgn(WINDOW_CONT_REGION(w), t);
                OffsetRgn(t,
                          PORT_BOUNDS(w).left,
                          PORT_BOUNDS(w).top);
                EraseRgn(t);
                DisposeRgn(t);
            }
        }

        if(WINDOW_VISIBLE(w))
        {
            /* set the port here, don't just save it while drawing the
	     window
	     FIXME: i'm not sure to what extent this should be the
	     case, at the bare minimum at least here */
            SetPort(wmgr_port);

            SetClip(WINDOW_STRUCT_REGION(w));
            ClipAbove((WindowPeek)w);
            WINDCALL(w, wDraw, 0);
        }
    }
    else
    {
        /* modify the default color window table */
        abort();
    }
}

/* NOTE: all documentation on this function is either vague to the
   point of certain mis-interpretation, or plain wrong.

   it's second argument is `AuxWinHandle *' (or `AuxWinHndl *' in
   Think C speak), and returns the aux window entry for the given
   window; if nullptr is passed, an handle to a `AuxWinRec' is placed in
   the aux_w_out argument which contains the default window color
   table

   they named it correctly, but noone got the documentation right */

Boolean Executor::C_GetAuxWin(WindowPtr w, GUEST<AuxWinHandle> *aux_win_out)
{
    if(!w)
    {
        /* return default window color table */
        *aux_win_out = default_aux_win;
        return true;
    }
    else
    {
        GUEST<AuxWinHandle> t;

        t = *lookup_aux_win(w);
        if(t)
        {
            /* return this window's color table */
            *aux_win_out = t;
            return true;
        }
        else
        {
            /* return default window color table */
            *aux_win_out = default_aux_win;
            return false;
        }
    }
}
