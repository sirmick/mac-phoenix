/* Copyright 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in MenuMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <MenuMgr.h>
#include <ResourceMgr.h>

#include <base/cpu.h>
#include <quickdraw/cquick.h>
#include <menu/menu.h>
#include <wind/wind.h>
#include <prefs/prefs.h>

using namespace Executor;

void Executor::C_InitProcMenu(INTEGER mbid)
{
    if(!LM(MenuList))
        InitMenus();

#if 0
    /* NOTE:  We don't dispose this guy because it is a phoney resource */
    if (LM(MBDFHndl))
	DisposeHandle(LM(MBDFHndl));
#endif

    /* NOTE: even though the docs imply that the low three bits of the
     * mbid should be masked off, Microsoft PowerPoint showed that
     * this is not the case.  They try to get mbid 300, and there is a
     * 300 in the resource file but not a 296, so masking causes the
     * GetResource to fail.  A small test program then confirmed that
     * the bits are not masked off.
     */
    LM(MBDFHndl) = GetResource("MBDF"_4, mbid);
    (*MENULIST)->mufu = mbid;
    MBDFCALL(mbInit, 0, 0L);
}

LONGINT Executor::C_MenuChoice()
{
    return LM(MenuDisable);
}

void Executor::C_GetItemCmd(MenuHandle mh, INTEGER item,
                            GUEST<CharParameter> *cmdp)
{
    mextp mep;

    if((mep = ROMlib_mitemtop(mh, item, (StringPtr *)0)))
        *cmdp = (unsigned short)(unsigned char)mep->mkeyeq;
}

void Executor::C_SetItemCmd(MenuHandle mh, INTEGER item, CharParameter cmd)
{
    mextp mep;

    if((mep = ROMlib_mitemtop(mh, item, (StringPtr *)0)))
    {
        mep->mkeyeq = cmd;
        CalcMenuSize(mh);
    }
}

LONGINT Executor::C_PopUpMenuSelect(MenuHandle mh, INTEGER top, INTEGER left,
                                    INTEGER item)
{
    Point p;
    Rect saver;
    GUEST<INTEGER> tempi;
    LONGINT where;
    int count;

    /*
 * The following call to ROMlib_destroy_blocks is only here because
 * CompuServe Information Manager creates code on the fly right before
 * calling PopUpMenuSelect.  How does this work on a Quadra?  I suspect
 * that the particular code in question isn't hit if color QuickDraw is
 * present (they call Gestalt for "qd  " before doing this goofy stuff).
 */
    if(ROMlib_flushoften)
        ROMlib_destroy_blocks(0, ~0, true); /* For CIM 2.1.4 */
    p.h = top; /* MacWriteII seems to use these in this fashion */
    p.v = left;

    /* ### what to do if `mh' is empty -- is this correct? */
    count = CountMItems(mh);

#if 0
    /* if we blow off empty menus, then ClarisImpact custom
       pulldown/popup menus don't come up */
    if (! count)
      return (int32_t) (*mh)->menuID << 16;
#endif

    /* if item is zero, it means no menu item was previously selected,
       and a `default' should be chosen */
    if(!item)
        item = 1;
    else if(item > count)
    {
        warning_unexpected("menu item exceeds number of items in menu");
        item = count;
    }

    ThePortGuard guard(wmgr_port);
    tempi = item;
    MENUCALL(mPopUpRect, mh, &saver, p, &tempi);
    ROMlib_rootless_openmenu(saver);
    LM(TopMenuItem) = tempi;
    where = ROMlib_mentosix((*mh)->menuID);

    MBDFCALL(mbSave, where, ptr_to_longint(&saver));

    auto saveclip = PORT_CLIP_REGION(qdGlobals().thePort); /* ick */
    PORT_CLIP_REGION(qdGlobals().thePort) = NewRgn();
    RectRgn(PORT_CLIP_REGION(qdGlobals().thePort), &saver);
    MENUCALL(mDrawMsg, mh, &saver, p, nullptr);
    DisposeRgn(PORT_CLIP_REGION(qdGlobals().thePort));
    PORT_CLIP_REGION(qdGlobals().thePort) = saveclip;
    MBDFCALL(mbSaveAlt, 0, where);
    
    return ROMlib_menuhelper(mh, &saver, where, true, 1);
}
