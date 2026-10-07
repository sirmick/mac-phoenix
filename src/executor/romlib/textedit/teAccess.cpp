/* Copyright 1986 - 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in TextEdit.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <WindowMgr.h>
#include <ControlMgr.h>
#include <TextEdit.h>
#include <MemoryMgr.h>
#include <OSUtil.h>

#include <quickdraw/cquick.h>
#include <mman/mman.h>
#include <textedit/textedit.h>

using namespace Executor;

void Executor::C_TESetText(Ptr p, LONGINT length, TEHandle teh)
{
    TE_SLAM(teh);
    if(PtrToXHand(p, TE_HTEXT(teh), length) != noErr)
        length = 0;
#if 0
  HASSIGN_3
    (teh,
     selStart, length,
     selEnd, length,
     teLength, length);
#else
    (*teh)->teLength = length;
#endif
    /* ### adjust recal* fields? */
    if(TE_STYLIZED_P(teh))
    {
        TEStyleHandle te_style;
        STHandle style_table;
        FontInfo finfo;

        te_style = TE_GET_STYLE(teh);
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
        GetFontInfo(&finfo);
        HASSIGN_7(style_table,
                  stCount, 1,
                  stFont, PORT_TX_FONT(qdGlobals().thePort),
                  stFace, PORT_TX_FACE(qdGlobals().thePort),
                  stSize, PORT_TX_SIZE(qdGlobals().thePort),
                  stColor, ROMlib_black_rgb_color,
                  stHeight, finfo.ascent
                               + finfo.descent
                               + finfo.leading,
                  stAscent, finfo.ascent);
    }
    TECalText(teh);
    TE_SLAM(teh);
}

CharsHandle Executor::C_TEGetText(TEHandle teh)
{
    return (CharsHandle)TE_HTEXT(teh);
}
