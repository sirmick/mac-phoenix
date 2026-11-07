/* Copyright 1986, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <ToolboxUtil.h>

#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <quickdraw/picture.h>

using namespace Executor;

void Executor::C_TextFont(INTEGER f)
{
    if(qdGlobals().thePort)
        PORT_TX_FONT(qdGlobals().thePort) = f;
}

void Executor::C_TextFace(INTEGER thef)
{
    if(qdGlobals().thePort)
        PORT_TX_FACE(qdGlobals().thePort) = thef;
}

void Executor::C_TextMode(INTEGER m)
{
    if(qdGlobals().thePort)
        PORT_TX_MODE(qdGlobals().thePort) = m;
}

void Executor::C_TextSize(INTEGER s)
{
    if(qdGlobals().thePort)
        PORT_TX_SIZE(qdGlobals().thePort) = s;
}

void Executor::C_SpaceExtra(Fixed e)
{
    if(qdGlobals().thePort)
        PORT_SP_EXTRA(qdGlobals().thePort) = e;
}

void Executor::C_DrawChar(CharParameter thec)
{
    Point p;
    Byte c;

    c = thec;
    p.h = 1;
    p.v = 1;
    CALLTEXT(1, (Ptr)&c, p, p);
}

void Executor::C_DrawString(ConstStringPtr s)
{
    Point p;

    p.h = 1;
    p.v = 1;
    CALLTEXT((INTEGER)s[0], (Ptr)(s + 1), p, p);
}

void Executor::C_DrawText(Ptr tb, INTEGER fb, INTEGER bc)
{
    Point p;

    p.h = 1;
    p.v = 1;
    CALLTEXT(bc, tb + fb, p, p);
}

/* This is a convenience function so we can more easily call DrawText
 * with C strings.
 */
void Executor::DrawText_c_string(const char *string)
{
    DrawText((Ptr)string, 0, strlen(string));
}

INTEGER Executor::C_CharWidth(CharParameter thec)
{
    GUEST<Point> np, dp;
    INTEGER retval;
    FontInfo fi;
    Byte c;

    c = thec;
    np.h = np.v = dp.h = dp.v = 256;
    retval = CALLTXMEAS(1, (Ptr)&c, &np, &dp, &fi);
    return FixMul((LONGINT)retval << 16, (Fixed)np.h << 8) / ((LONGINT)dp.h << 8);
}

INTEGER Executor::C_StringWidth(ConstStringPtr s)
{
    GUEST<Point> np, dp;
    INTEGER retval;
    FontInfo fi;

    np.h = np.v = dp.h = dp.v = 256;
    retval = CALLTXMEAS((INTEGER)s[0], (Ptr)s + 1, &np, &dp, &fi);
    return FixMul((LONGINT)retval << 16,
                  (Fixed)np.h << 8)
        / ((LONGINT)dp.h << 8);
}

INTEGER Executor::C_TextWidth(Ptr tb, INTEGER fb, INTEGER bc)
{
    GUEST<Point> np, dp;
    INTEGER retval;
    FontInfo fi;

    np.h = np.v = dp.h = dp.v = 256;
    retval = CALLTXMEAS(bc, tb + fb, &np, &dp, &fi);
    return FixMul((LONGINT)retval << 16,
                  (Fixed)np.h << 8)
        / ((LONGINT)dp.h << 8);
}

void Executor::C_GetFontInfo(FontInfo *ip)
{
    GUEST<Point> pn, pd;

    pn.h = 1;
    pn.v = 1;
    pd.v = 1;
    pd.h = 1;
    CALLTXMEAS(0, (Ptr) "", &pn, &pd, ip);
    ip->ascent = ip->ascent * pn.v / pd.v;
    ip->descent = ip->descent * pn.v / pd.v;
    ip->leading = ip->leading * pn.v / pd.v;
}
