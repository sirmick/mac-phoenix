/* Copyright 1986-1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <MemoryMgr.h>
#include <OSUtil.h>

#include <quickdraw/cquick.h>
#include <quickdraw/picture.h>
#include <print/print.h>
#include <quickdraw/text.h>

using namespace Executor;

void Executor::C_StdComment(INTEGER kind, INTEGER size, Handle hand)
{
    SignedByte state;
    GUEST<INTEGER> swappedsize;

    switch(kind)
    {
        case textbegin:
            disable_stdtext();
            break;
        case textend:
            enable_stdtext();
            break;
    }

    GUEST<INTEGER> kind_s = kind;
    if(size)
    {
        PICSAVEBEGIN(OP_LongComment);
        PICWRITE(&kind_s, sizeof(kind_s));
        swappedsize = size;
        PICWRITE(&swappedsize, sizeof(swappedsize));
        state = HGetState(hand);
        HLock(hand);
        PICWRITE(*hand, size);
        if(size & 1)
            PICWRITE("", 1);
        HSetState(hand, state);
        PICSAVEEND;
    }
    else
    {
        PICSAVEBEGIN(OP_ShortComment);
        PICWRITE(&kind_s, sizeof(kind_s));
        PICSAVEEND;
    }
}

void Executor::C_StdGetPic(void *dp, INTEGER bc) /* TODO */
{
    warning_unimplemented("");
}

void Executor::C_StdPutPic(const void  *sp, INTEGER bc)
{
    piccachehand pch;
    PicHandle ph;
    LONGINT oldhowfar, newhowfar;
    Size newsize;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);

    if(pch)
    {
        oldhowfar = (*pch)->pichowfar;
        ph = (*pch)->pichandle;
        newhowfar = (*pch)->pichowfar + bc;
        (*pch)->pichowfar = newhowfar;
        if(newhowfar > 32766)
            (*ph)->picSize = 32766;
        else
            (*ph)->picSize = newhowfar;

        if((*pch)->pichowfar > (*pch)->picsize)
        {
            newsize = ((*pch)->pichowfar + 0xFF) & ~(LONGINT)0xFF;
            SetHandleSize((Handle)ph, newsize);
            (*pch)->picsize = newsize;
        }
        memmove((char *)*ph + oldhowfar, sp, bc);
    }
}
