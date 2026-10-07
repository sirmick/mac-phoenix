/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <ToolboxUtil.h>
#include <algorithm>

using namespace Executor;

void Executor::C_SetRect(Rect *r, INTEGER left, INTEGER top, INTEGER right,
                         INTEGER bottom)
{
    r->top = top;
    r->left = left;
    r->bottom = bottom;
    r->right = right;
}

void Executor::C_OffsetRect(Rect *r, INTEGER dh, INTEGER dv)
{
    r->top    =  r->top    + dv ;
    r->bottom =  r->bottom + dv ;
    r->left   =  r->left   + dh ;
    r->right  =  r->right  + dh ;
}

void Executor::C_InsetRect(Rect *r, INTEGER dh, INTEGER dv)
{
    r->top    =  r->top    + dv ;
    r->bottom =  r->bottom - dv ;
    r->left   =  r->left   + dh ;
    r->right  =  r->right  - dh ;

#if defined(INCOMPATIBLEBUTSANE)
    if(r->top >= r->bottom || r->left >= r->right)
        *r = {};
#endif /* INCOMPATIBLEBUTSANE */
}

Boolean Executor::C_SectRect(const Rect *s1, const Rect *s2, Rect *dest)
{
    if(s1->top < s2->bottom
       && s2->top < s1->bottom
       && s1->left < s2->right
       && s2->left < s1->right)
    {
        dest->top = std::max(s1->top, s2->top);
        dest->left = std::max(s1->left, s2->left);
        dest->bottom = std::min(s1->bottom, s2->bottom);
        dest->right = std::min(s1->right, s2->right);
        return !EmptyRect(dest);
    }
    else
    {
        *dest = {};
        return false;
    }
}

Boolean Executor::C_EmptyRect(const Rect *r)
{
    return (r->top >= r->bottom || r->left >= r->right);
}

void Executor::C_UnionRect(const Rect *s1, const Rect *s2, Rect *dest)
{
    dest->top = std::min(s1->top, s2->top);
    dest->left = std::min(s1->left, s2->left);
    dest->bottom = std::max(s1->bottom, s2->bottom);
    dest->right = std::max(s1->right, s2->right);
}

Boolean Executor::C_PtInRect(Point p, const Rect *r)
{
    Boolean retval;

    retval = (p.h >= r->left
              && p.h < r->right
              && p.v >= r->top
              && p.v < r->bottom);
    return retval;
}

void Executor::C_Pt2Rect(Point p1, Point p2, Rect *dest)
{
    dest->top = std::min(p1.v, p2.v);
    dest->left = std::min(p1.h, p2.h);
    dest->bottom = std::max(p1.v, p2.v);
    dest->right = std::max(p1.h, p2.h);
}

void Executor::C_PtToAngle(const Rect *rp, Point p, GUEST<INTEGER> *angle)
{
    int a, dx, dy;

    /* I ran some tests on this code, for a square input rectangle.  We
   * are now off by no more than one degree from what the Mac
   * generates for any point within that rectangle.
   * Since we aren't exactly the same as the Mac here, why not
   * just call atan2()?
   */

    dx = p.h - (rp->left + rp->right) / 2;
    dy = p.v - (rp->top + rp->bottom) / 2;

    if(dx != 0)
    {
        a = (AngleFromSlope(FixMul(FixRatio(dx, dy),
                                   FixRatio(RECT_HEIGHT(rp),
                                            RECT_WIDTH(rp))))
             % 180);
        if(dx < 0)
            a += 180;
    }
    else /* dx == 0 */
    {
        if(dy <= 0)
            a = 0;
        else
            a = 180;
    }

    *angle = a;
}

Boolean Executor::C_EqualRect(const Rect *r1, const Rect *r2)
{
    return *reinterpret_cast<const uint64_t*>(r1) == *reinterpret_cast<const uint64_t*>(r2);
}

/* see regular.c for {Frame, Paint, Erase, Invert, Fill} Rect */
