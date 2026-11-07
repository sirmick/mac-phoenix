/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in TextEdit.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <WindowMgr.h>
#include <ControlMgr.h>
#include <EventMgr.h>
#include <TextEdit.h>
#include <MemoryMgr.h>
#include <ScrapMgr.h>
#include <OSUtil.h>

#include <quickdraw/cquick.h>
#include <mman/mman.h>
#include <textedit/tesave.h>
#include <rsys/arrowkeys.h>
#include <osevent/osevent.h>
#include <quickdraw/text.h>
#include <prefs/prefs.h>
#include <textedit/textedit.h>

using namespace Executor;

static void
tedoinserttext(TEHandle te, int16_t hlen, int16_t len,
               int16_t start, Ptr ptr)
{
    Handle hText;
    char *Text;

    hText = TE_HTEXT(te);
    SetHandleSize(hText, hlen + len);

    Text = (char *)*hText;

    memmove(&Text[start + len], &Text[start], hlen - start);
    memmove(&Text[start], ptr, len);

    HASSIGN_3(te,
              selEnd, TE_SEL_END(te) + len,
              selStart, TE_SEL_START(te) + len,
              teLength, TE_LENGTH(te) + len);
}

void Executor::ROMlib_teremovestyleinfo(TEStyleHandle te_style,
                                        int16_t start, int16_t end)
{
    StyleRun *runs, *start_run, *prev_run;
    int16_t start_run_index, end_run_index;
    int16_t current_run_index;
    int16_t shift;
    int16_t n_runs;

    if(start == end)
        return;
    start_run_index = make_style_run_at(te_style, start);
    end_run_index = make_style_run_at(te_style, end);

    runs = TE_STYLE_RUNS(te_style);
    n_runs = TE_STYLE_N_RUNS(te_style);

    start_run = &runs[start_run_index];
    prev_run = &runs[start_run_index - 1];
    if(start_run_index
       && (STYLE_RUN_STYLE_INDEX(start_run) != STYLE_RUN_STYLE_INDEX(prev_run)))
    {
        StScrpHandle null_scrap;
        ScrpSTElement *scrap_elt;
        STElement *st_elt;

        null_scrap = TE_STYLE_NULL_SCRAP(te_style);

        SCRAP_N_STYLES(null_scrap) = 1;
        SetHandleSize((Handle)null_scrap, SCRAP_SIZE_FOR_N_STYLES(1));
        scrap_elt = SCRAP_ST_ELT(null_scrap, 0);
        st_elt = ST_ELT(TE_STYLE_STYLE_TABLE(te_style),
                        STYLE_RUN_STYLE_INDEX(start_run));

        generic_elt_copy(SCRAP_ELT_TO_GENERIC_ELT(scrap_elt),
                         ST_ELT_TO_GENERIC_ELT(st_elt));
        SCRAP_ELT_START_CHAR(scrap_elt) = 0;
    }

    shift = end - start;
    for(current_run_index = start_run_index;
        current_run_index < end_run_index;
        current_run_index++)
    {
        StyleRun *current_run;

        current_run = &runs[current_run_index];
        release_style_index(te_style, STYLE_RUN_STYLE_INDEX(current_run));
    }
    for(; current_run_index <= n_runs; current_run_index++)
    {
        StyleRun *current_run;

        current_run = &runs[current_run_index];
        STYLE_RUN_START_CHAR(current_run)
            = STYLE_RUN_START_CHAR(current_run) - shift;
    }

    memmove(&runs[start_run_index], &runs[end_run_index],
            (n_runs - end_run_index + 1) * sizeof *runs);
    n_runs -= end_run_index - start_run_index;
    TE_STYLE_N_RUNS(te_style) = n_runs;
    SetHandleSize((Handle)te_style,
                  TE_STYLE_SIZE_FOR_N_RUNS(n_runs));

    te_style_combine_runs(te_style);
    stabilize_style_info(te_style);
}

static void
tereplaceselection(TEHandle teh, int16_t start, int16_t stop, int16_t len,
                   Ptr ptr, int16_t nchar, int16_t hlen)
{
    Handle hText;
    /* number of characters after teh selection to relocate */
    int16_t n_to_move;

    hText = TE_HTEXT(teh);
    n_to_move = hlen - stop;
    if(nchar > 0)
        SetHandleSize(TE_HTEXT(teh), hlen + nchar);
    if(nchar != 0)
    {
        BlockMoveData(*hText + stop,
                      *hText + start + len,
                      n_to_move);
    }
    if(nchar < 0)
        SetHandleSize(hText, hlen + nchar);
    BlockMoveData(ptr, *hText + start, len);

    TE_SEL_END(teh) = TE_SEL_START(teh)
        = TE_SEL_START(teh) + len;
    TE_LENGTH(teh) = hlen + nchar;
    TE_CARET_STATE(teh) = -1; /* will be highlit below */
}

void te_style_insert_runs(TEStyleHandle te_style,
                          int16_t start, int len,
                          StyleRun *new_runs, int16_t n_new_runs)
{
    STHandle style_table;
    StyleRun *runs;
    int16_t start_run_index;
    int16_t run_i, n_runs;

    start_run_index = te_char_to_run_index(te_style, start);
    runs = TE_STYLE_RUNS(te_style);
    n_runs = TE_STYLE_N_RUNS(te_style);
    style_table = TE_STYLE_STYLE_TABLE(te_style);
    for(run_i = start_run_index + 1; run_i <= n_runs; run_i++)
    {
        StyleRun *run = &runs[run_i];

        STYLE_RUN_START_CHAR(run)
            = STYLE_RUN_START_CHAR(run) + len;
    }

    for(run_i = 0; run_i < n_new_runs; run_i++)
    {
        int16_t style_index, new_style_index;
        StyleRun *new_run, *next_new_run, *run;
        int16_t run_start, run_end, run_index;

        new_run = &new_runs[run_i];
        next_new_run = &new_runs[run_i + 1];

        /* compute `run'.  since we are going to change the style for
	 this run, make sure that `run' contains only the range to be
	 changed */
        run_start = STYLE_RUN_START_CHAR(new_run) + start;
        run_index = make_style_run_at(te_style, run_start);

        run_end = STYLE_RUN_START_CHAR(next_new_run) + start;
        make_style_run_at(te_style, run_end);

        run = TE_STYLE_RUN(te_style, run_index);

        new_style_index = STYLE_RUN_STYLE_INDEX(new_run);
        style_index = STYLE_RUN_STYLE_INDEX(run);

        if(style_index != new_style_index)
        {
            STElement *new_style;

            new_style = ST_ELT(style_table, new_style_index);

            ST_ELT_COUNT(new_style) = ST_ELT_COUNT(new_style) + 1;
            release_style_index(te_style, style_index);

            STYLE_RUN_STYLE_INDEX(run) = new_style_index;
        }
    }
    stabilize_style_info(te_style);
    te_style_combine_runs(te_style);
}

void Executor::ROMlib_teinsertstyleinfo(TEHandle te,
                                        int16_t start, int16_t len, StScrpHandle scrap)
{
    StScrpHandle null_scrap;
    TEStyleHandle te_style;
    int16_t scrap_n_styles;
    int cleanup_scrap_p = false;
    StyleRun *new_runs;
    int i;

    te_style = TE_GET_STYLE(te);

    if(!scrap)
    {
        null_scrap = TE_STYLE_NULL_SCRAP(te_style);

        if(SCRAP_N_STYLES(null_scrap))
            scrap = null_scrap;
        else
        {
            int16_t start_run_index;
            StyleRun *start_run;
            STElement *start_st_elt;
            ScrpSTElement *scrap_elt;
            STHandle style_table;

            /* allocate a temp scrap */
            /* enough for a single scrap style element */
            scrap = (StScrpHandle)NewHandle(sizeof(StScrpRec));
            cleanup_scrap_p = true;

            SCRAP_N_STYLES(scrap) = 1;
            scrap_elt = SCRAP_ST_ELT(scrap, 0);

            start_run_index = te_char_to_run_index(te_style, (start
                                                                  ? start - 1
                                                                  : start));
            start_run = TE_STYLE_RUN(te_style, start_run_index);
            style_table = TE_STYLE_STYLE_TABLE(te_style);
            start_st_elt = ST_ELT(style_table, STYLE_RUN_STYLE_INDEX(start_run));

            generic_elt_copy(SCRAP_ELT_TO_GENERIC_ELT(scrap_elt),
                             ST_ELT_TO_GENERIC_ELT(start_st_elt));
            SCRAP_ELT_START_CHAR(scrap_elt) = 0;
        }
    }

    scrap_n_styles = SCRAP_N_STYLES(scrap);
    new_runs = (StyleRun *)alloca((scrap_n_styles + 1) * sizeof *new_runs);
    for(i = 0; i < scrap_n_styles; i++)
    {
        ScrpSTElement *scrap_elt;
        StyleRun *new_run;
        int16_t style_index;

        new_run = &new_runs[i];
        scrap_elt = SCRAP_ST_ELT(scrap, i);

        style_index = get_style_index(te_style, SCRAP_ELT_TO_ATTR(scrap_elt),
                                      false);

        /* must swap here, the elt start char is a `int32_t' */
        STYLE_RUN_START_CHAR(new_run) = SCRAP_ELT_START_CHAR(scrap_elt);
        STYLE_RUN_STYLE_INDEX(new_run) = style_index;
    }
    {
        StyleRun *new_run;

        new_run = &new_runs[scrap_n_styles];
        STYLE_RUN_START_CHAR(new_run) = len;
        STYLE_RUN_STYLE_INDEX(new_run) = -1;
    }

    te_style_insert_runs(te_style, start, len, new_runs, scrap_n_styles);

    if(cleanup_scrap_p)
        DisposeHandle((Handle)scrap);
}

void Executor::ROMlib_tedoitall(TEHandle teh, Ptr ptr, /* INTERNAL */
                                int16_t len, bool insert, StScrpHandle styleh)
{
    INTEGER start, stop, sellen, hlen, nchar;
    int16_t calstart, calend;
    Point oldend, newend;
    Rect eraser;
    TEStyleHandle te_style;
    INTEGER oldlh, newlh;
    LHHandle lht;

    TESAVE(teh);

#if !defined(LETGCCWAIL)
    te_style = nullptr;
    newlh = 0;
#endif /* LETGCCWAIL */

    if(TE_STYLIZED_P(teh))
        te_style = TE_GET_STYLE(teh);

    if(TE_STYLIZED_P(teh)
       && TE_LINE_HEIGHT(teh) == -1)
    {
        lht = TE_STYLE_LH_TABLE(te_style);
        oldlh = (*lht + (*teh)->nLines - 1)->lhHeight;
    }
    else
    {
        oldlh = newlh = TE_LINE_HEIGHT(teh);
        lht = nullptr;
    }

#if 1
    /* If start and stop are both -1 on a Mac, text is inserted at the end.
     My guess is they're using unsigned numbers, so this is a hacky
     approximation until we can run a lot of tests to figure out exactly
     what is going on.  PhysTCL points this out. */

    if(TE_SEL_START(teh) < 0)
        TE_SEL_START(teh) = 32767;
    if(TE_SEL_END(teh) < 0)
        TE_SEL_END(teh) = 32767;
#endif

    start = TE_SEL_START(teh);
    stop = TE_SEL_END(teh);
    hlen = TE_LENGTH(teh);

    if(start < 0)
    {
        warning_unexpected("start = %d", start);
        TE_SEL_START(teh) = 0;
        start = 0;
    }

    if(stop < 0)
    {
        warning_unexpected("stop = %d", stop);
        TE_SEL_END(teh) = 0;
        stop = 0;
    }

    if(hlen < 0)
    {
        warning_unexpected("nlen = %d", hlen);
        TE_LENGTH(teh) = 0;
        hlen = 0;
    }

    if(start > hlen)
    {
        warning_unexpected("start = %d, hlen = %d", start, hlen);
        TE_SEL_START(teh) = hlen;
        start = hlen;
    }

    if(stop > hlen)
    {
        warning_unexpected("stop = %d, hlen = %d", stop, hlen);
        TE_SEL_END(teh) = hlen;
        stop = hlen;
    }

    if(start > stop)
    {
        warning_unexpected("start = %d, stop = %d", start, stop);
        TE_SEL_START(teh) = stop;
        start = stop;
    }

    TE_CHAR_TO_POINT(teh, hlen, &oldend);

    if(TE_CARET_STATE(teh) != 255)
        /* turn off any highliting */
        ROMlib_togglelite(teh);
    if(insert)
    {
        nchar = len;
        tedoinserttext(teh, hlen, len, start, ptr);
    }
    else
    {
        // TODO: check whether this shouldn't be done just in TEKey
        if(ptr && (*ptr == '\010' || *ptr == '\177'))
        {
            bool forwardDelete = *ptr == '\177';
            ptr++;
            len--;
            if(start == stop)
            {
                if(!forwardDelete)
                {
                    if(start > 0)
                    {
                        (*teh)->selStart = (*teh)->selStart - 1;
                        start = (*teh)->selStart;
                    }
                }
                else
                {
                    if(stop < hlen)
                    {
                        (*teh)->selEnd = (*teh)->selEnd + 1;
                        stop = (*teh)->selEnd;
                    }
                }
            }
        }
        sellen = stop - start;
        nchar = len - sellen;
        if(TE_STYLIZED_P(teh))
            ROMlib_teremovestyleinfo(te_style, start, stop);
        tereplaceselection(teh, start, stop, len, ptr, nchar, hlen);
    }
    if(TE_STYLIZED_P(teh) && len != 0)
        ROMlib_teinsertstyleinfo(teh, start, len, styleh);
    ROMlib_caltext(teh, start, nchar, &calstart, &calend);
    TE_CHAR_TO_POINT(teh, TE_LENGTH(teh), &newend);
    if(TE_STYLIZED_P(teh) && TE_LINE_HEIGHT(teh) == -1)
        newlh = (*lht + TE_N_LINES(teh) - 1)->lhHeight;

    if(oldend.v > newend.v)
    {
        eraser.top = newend.v;
        eraser.left = newend.h;
        eraser.bottom = newend.v + newlh;
        eraser.right = (*teh)->viewRect.right;
        SectRect(&(*teh)->viewRect, &eraser, &eraser);
        EraseRect(&eraser);
        eraser.top = eraser.bottom;
        eraser.left = (*teh)->viewRect.left;
        if(eraser.top != oldend.v)
        {
            eraser.bottom = oldend.v;
            eraser.right = (*teh)->viewRect.right;
            SectRect(&(*teh)->viewRect, &eraser, &eraser);
            EraseRect(&eraser);
            eraser.top = oldend.v;
        }
        eraser.bottom = oldend.v + oldlh;
        eraser.right = oldend.h;
        SectRect(&(*teh)->viewRect, &eraser, &eraser);
        EraseRect(&eraser);
    }
    else if(oldend.v == newend.v && oldend.h > newend.h)
    {
        eraser.top = oldend.v;
        eraser.left = newend.h;
        eraser.bottom = oldend.v + oldlh;
        eraser.right = oldend.h;
        SectRect(&(*teh)->viewRect, &eraser, &eraser);
        EraseRect(&eraser);
    }
    if(TE_STYLIZED_P(teh))
        SCRAP_N_STYLES(TE_STYLE_NULL_SCRAP(te_style)) = 0;

    TE_DO_TEXT(teh, calstart, calend, teDraw);

    if(TE_SEL_START(teh) != TE_SEL_END(teh))
        /* turn on any highliting */
        ROMlib_togglelite(teh);
    else
        TE_CARET_STATE(teh) = 255;

    TERESTORE();
}

static void doarrow(TEHandle te, CharParameter thec)
{
    int16_t sel_start, sel_end;
    int16_t length;
    int16_t lineno;
    Point pt;
    TEPtr tep;
    SignedByte te_flags;
    Byte c;

    TESAVE(te);

    te_flags = HGetState((Handle)te);
    HLock((Handle)te);
    tep = *te;

    c = thec;
    sel_start = TEP_SEL_START(tep);
    sel_end = TEP_SEL_END(tep);
    length = TEP_LENGTH(tep);

    if(TEP_CARET_STATE(tep) != caret_invis)
    {
        ROMlib_togglelite(te);
        TEP_CARET_STATE(tep) = caret_invis;
    }

    switch(c)
    {
        case ASCIILEFTARROW:
            if(sel_start)
                sel_start--;
            break;
        case ASCIIRIGHTARROW:
            sel_start = sel_end;
            if(sel_start < length)
                sel_start++;
            break;
        case ASCIIUPARROW:
        case ASCIIDOWNARROW:
        {
            int16_t offset;

            TEP_CHAR_TO_POINT(tep, sel_start, &pt);
            if(TEP_LINE_HEIGHT(tep) != -1)
                offset = TEP_LINE_HEIGHT(tep);
            else if(c == ASCIIUPARROW)
                offset = 1;
            else
            {
                lineno = TEP_CHAR_TO_LINENO(tep, sel_start);
                offset = TEP_HEIGHT_FOR_LINE(tep, lineno);
            }
            if(c == ASCIIUPARROW)
                pt.v -= offset;
            else
                pt.v += offset;
            TEP_SEL_POINT(tep).h = pt.h;
            TEP_SEL_POINT(tep).v = pt.v;
            sel_start = TEP_DO_TEXT(tep, 0, length, teFind);
            break;
        }
    }

    TEP_SEL_START(tep) = sel_start;
    TEP_SEL_END(tep) = sel_start;
    if(TEP_CARET_STATE(tep))
        TEP_CARET_STATE(tep) = caret_vis;
    ROMlib_togglelite(te);
    HSetState((Handle)te, te_flags);

    TERESTORE();
}

void Executor::C_TEKey(CharParameter thec, TEHandle te)
{
    Byte c;

    TE_SLAM(te);

    c = thec;
    switch(c)
    {
        case ASCIILEFTARROW: /* <-- left*/
        case ASCIIRIGHTARROW: /* --> right*/
        case ASCIIUPARROW: /* ^ up*/
        case ASCIIDOWNARROW: /* v down*/
            doarrow(te, c);
            if(TE_STYLIZED_P(te))
            {
                TEStyleHandle te_style;

                te_style = TE_GET_STYLE(te);
                SCRAP_N_STYLES(TE_STYLE_NULL_SCRAP(te_style)) = 0;
            }
            break;
        case NUMPAD_ENTER:
            c = '\r';
        /* FALL THROUGH */
        default:
            ROMlib_tedoitall(te, (Ptr)&c, 1, false, nullptr);
            break;
    }
    ROMlib_recompute_caret(te);
    TE_SLAM(te);
}

void Executor::C_TECopy(TEHandle te)
{
    Handle hText;
    SignedByte hText_flags;
    char *Text;
    int16_t len;
    int16_t start, end;

    start = TE_SEL_START(te);
    end = TE_SEL_END(te);

    len = end - start;

    hText = TE_HTEXT(te);
    hText_flags = HGetState(hText);
    HLock(hText);
    Text = (char *)*hText;

    PtrToXHand((Ptr)&Text[start], LM(TEScrpHandle), len);
    LM(TEScrpLength) = len;

    if(TE_STYLIZED_P(te))
    {
        TEStyleHandle te_style;
        SignedByte te_style_flags;
        STHandle style_table;
        StScrpHandle scrap;
        int16_t start_run_index, end_run_index, current_run_index;
        int16_t n_scrap_styles;
        StyleRun *runs;

        ZeroScrap();
        PutScrap(len, "TEXT"_4, (Ptr)&Text[start]);

        te_style = TE_GET_STYLE(te);
        te_style_flags = HGetState((Handle)te_style);
        HLock((Handle)te_style);

        style_table = TE_STYLE_STYLE_TABLE(te_style);
        runs = TE_STYLE_RUNS(te_style);

        n_scrap_styles = 0;
        scrap = (StScrpHandle)NewHandle(SCRAP_SIZE_FOR_N_STYLES(n_scrap_styles));

        start_run_index = te_char_to_run_index(te_style, start);
        /* ### boundary case, do we put an extra run into the scrap? */
        end_run_index = te_char_to_run_index(te_style, end);

        for(current_run_index = start_run_index;
            current_run_index <= end_run_index;
            current_run_index++)
        {
            StyleRun *current_run;
            STElement *style;
            ScrpSTElement *scrap_elt;
            int16_t run_start;

            n_scrap_styles++;
            SetHandleSize((Handle)scrap,
                          SCRAP_SIZE_FOR_N_STYLES(n_scrap_styles));

            current_run = &runs[current_run_index];
            style = ST_ELT(style_table, STYLE_RUN_STYLE_INDEX(current_run));
            scrap_elt = SCRAP_ST_ELT(scrap, n_scrap_styles - 1);

            generic_elt_copy(SCRAP_ELT_TO_GENERIC_ELT(scrap_elt),
                             ST_ELT_TO_GENERIC_ELT(style));
            run_start = STYLE_RUN_START_CHAR(current_run);
            SCRAP_ELT_START_CHAR(scrap_elt) = (run_start < start
                                                     ? 0
                                                     : run_start - start);
        }
        SCRAP_N_STYLES(scrap) = n_scrap_styles;

        {
            HLockGuard guard(scrap);
            PutScrap(SCRAP_SIZE_FOR_N_STYLES(n_scrap_styles),
                     "styl"_4, (Ptr)*scrap);
        }
        DisposeHandle((Handle)scrap);

        HSetState((Handle)te_style, te_style_flags);
    }

    HSetState(hText, hText_flags);
}

void Executor::C_TECut(TEHandle teh)
{
    TECopy(teh);
    ROMlib_tedoitall(teh, nullptr, 0, false, nullptr);
}

void Executor::C_TEPaste(TEHandle teh)
{
    HLockGuard guard(LM(TEScrpHandle));
    ROMlib_tedoitall(teh, *LM(TEScrpHandle), LM(TEScrpLength),
                     false, nullptr);
}

void Executor::C_TEDelete(TEHandle teh)
{
    ROMlib_tedoitall(teh, nullptr, 0, false, nullptr);
}

void Executor::C_TEInsert(Ptr p, LONGINT ln, TEHandle teh)
{
    ROMlib_tedoitall(teh, p, ln, true, nullptr);
}
