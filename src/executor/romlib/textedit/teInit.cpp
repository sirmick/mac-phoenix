/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in TextEdit.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <WindowMgr.h>
#include <ControlMgr.h>
#include <ToolboxUtil.h>
#include <FontMgr.h>
#include <TextEdit.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <mman/mman.h>
#include <textedit/tesave.h>
#include <textedit/textedit.h>

using namespace Executor;

void Executor::C_TEInit()
{
    LM(TEScrpHandle) = NewHandle(0);
    LM(TEScrpLength) = 0;
    LM(TEDoText) = (ProcPtr)&ROMlib_dotext;
}

/* This code just does "moveql #1,d0 ; rts".  We use it because
 * DNA strider chains to the old clikLoop handler.
 */
static GUEST<uint16_t> default_clik_loop[2] = { 0x7001, 0x4E75 };

TEHandle Executor::C_TENew(const Rect *dst, const Rect *view)
{
    TEHandle teh;
    FontInfo finfo;
    GUEST<Handle> hText;
    GUEST<tehiddenh> temptehiddenh;
    GUEST<int16_t> *tehlinestarts;
    int te_size;

    te_size = ((sizeof(TERec)
                - sizeof TE_LINE_STARTS(teh))
               + 4 * sizeof *TE_LINE_STARTS(teh));
    teh = (TEHandle)NewHandle(te_size);

    hText = NewHandle(0);
    GetFontInfo(&finfo);
    /* zero the te record */
    memset(*teh, 0, te_size);
    /* ### find out what to assign to the `selRect', `selPoint'
     and `clikStuff' fields */
    HASSIGN_15(teh,
               destRect, *dst,
               viewRect, *view,
               lineHeight, finfo.ascent
                              + finfo.descent
                              + finfo.leading,
               fontAscent, finfo.ascent,
               active, false,
               caretState, caret_invis,
               just, teFlushDefault,
               crOnly, 1,
               clikLoop, (ProcPtr)&default_clik_loop[0],
               inPort, qdGlobals().thePort,
               txFont, PORT_TX_FONT(qdGlobals().thePort),
               txFace, PORT_TX_FACE(qdGlobals().thePort),
               txMode, PORT_TX_MODE(qdGlobals().thePort),
               txSize, PORT_TX_SIZE(qdGlobals().thePort),
               hText, hText);

    tehlinestarts = (*teh)->lineStarts;
    tehlinestarts[0] = 0;
    tehlinestarts[1] = 0; /* this one is only for mix & match w/mac */

    temptehiddenh = (tehiddenh)NewHandle(sizeof(tehidden));
    /* don't merge with line above */
    TEHIDDENH(teh) = temptehiddenh;
    memset(*TEHIDDENH(teh), 0, sizeof(tehidden));

    TE_SLAM(teh);

    return teh;
}

void Executor::C_TEDispose(TEHandle teh)
{
    DisposeHandle((Handle)TEHIDDENH(teh));
    DisposeHandle(TE_HTEXT(teh));
    DisposeHandle((Handle)teh);
}
