#pragma once

#include <TextEdit.h>

namespace Executor
{

#define TE_DO_TEXT(te, start, end, what)                            \
    ({                                                              \
        int16_t retval;                                             \
                                                                    \
        HLockGuard guard(te);                                       \
        retval = ROMlib_call_TEDoText(*(te), start, end, what); \
        retval;                                                     \
    })
#define TE_CHAR_TO_POINT(te, sel, pt)         \
    ({                                        \
        HLockGuard guard(te);                 \
        te_char_to_point(*(te), sel, pt); \
    })

/* no need to lock te, since `te_char_to_lineno' cannot move memory
   blocks */
#define TE_CHAR_TO_LINENO(te, sel) \
    te_char_to_lineno(*(te), sel)

#define TE_DEST_RECT(te) ((*(te))->destRect)
#define TE_VIEW_RECT(te) ((*(te))->viewRect)
#define TE_SEL_RECT(te) ((*(te))->selRect)
#define TE_SEL_POINT(te) ((*(te))->selPoint)
#define TE_LINE_STARTS(te) ((*(te))->lineStarts)

extern void ROMlib_sledgehammer_te(TEHandle te);
#if ERROR_SUPPORTED_P(ERROR_TEXT_EDIT_SLAM)
#define TE_SLAM(te)                               \
    do                                            \
    {                                             \
        if(ERROR_ENABLED_P(ERROR_TEXT_EDIT_SLAM)) \
            ROMlib_sledgehammer_te(te);           \
    } while(false)
#else /* No ERROR_TEXT_EDIT_SLAM */
#define TE_SLAM(te)
#endif /* No ERROR_TEXT_EDIT_SLAM */

#define TE_TX_FACE(te) ((*(te))->txFace)

#define TE_STYLIZED_P(te) ((*(te))->txSize == -1)
#define TE_LINE_HEIGHT(te) ((*(te))->lineHeight)
#define TE_FONT_ASCENT(te) ((*(te))->fontAscent)
#define TE_LENGTH(te) ((*(te))->teLength)
#define TE_ACTIVE(te) ((*(te))->active)
#define TE_CARET_STATE(te) ((*(te))->caretState)
#define TE_SEL_START(te) ((*(te))->selStart)
#define TE_SEL_END(te) ((*(te))->selEnd)
#define TE_N_LINES(te) ((*(te))->nLines)
#define TE_HTEXT(te) ((*(te))->hText)
#define TE_CLICK_STUFF(te) ((*(te))->clikStuff)
#define TE_CLICK_LOC(te) ((*(te))->clickLoc)
#define TE_CLICK_TIME(te) ((*(te))->clickTime)
#define TE_JUST(te) ((*(te))->just)
#define TE_TX_FONT(te) ((*(te))->txFont)
#define TE_TX_SIZE(te) ((*(te))->txSize)
#define TE_TX_MODE(te) ((*(te))->txMode)
#define TE_IN_PORT(te) ((*(te))->inPort)

#define TE_FLAGS(te) ((*TEHIDDENH(te))->flags)

#define TEP_DO_TEXT(tep, start, end, what) \
    ROMlib_call_TEDoText(tep, start, end, what)

#if !defined(NDEBUG)
#define TEP_CHAR_TO_POINT(tep, sel, pt)              \
    do {                                               \
        Handle te;                                   \
                                                     \
        te = RecoverHandle((Ptr)tep);                \
        gui_assert(te && (HGetState(te) & LOCKBIT)); \
        te_char_to_point(tep, sel, pt);              \
    } while(false)
#else
#define TEP_CHAR_TO_POINT(tep, sel, pt) \
    te_char_to_point(tep, sel, pt)
#endif
#define TEP_CHAR_TO_LINENO(tep, sel) \
    te_char_to_lineno(tep, sel)

#define TEP_SLAM(tep)                   \
    do {                                  \
        Handle te;                      \
                                        \
        te = RecoverHandle(tep)         \
            ROMlib_sledgehammer_te(te); \
    } while(false)

#define TEP_SEL_RECT(te) ((tep)->selRect)
#define TEP_DEST_RECT(tep) ((tep)->destRect)
#define TEP_VIEW_RECT(tep) ((tep)->viewRect)
#define TEP_LINE_STARTS(tep) ((tep)->lineStarts)
#define TEP_SEL_POINT(tep) ((tep)->selPoint)
#define TEP_TX_FACE(tep) ((tep)->txFace)

#define TEP_STYLIZED_P(tep) ((tep)->txSize == -1)
#define TEP_LINE_HEIGHT(tep) ((tep)->lineHeight)
#define TEP_FONT_ASCENT(tep) ((tep)->fontAscent)
#define TEP_LENGTH(tep) ((tep)->teLength)
#define TEP_ACTIVE(tep) ((tep)->active)
#define TEP_CARET_STATE(tep) ((tep)->caretState)
#define TEP_SEL_START(tep) ((tep)->selStart)
#define TEP_SEL_END(tep) ((tep)->selEnd)
#define TEP_N_LINES(tep) ((tep)->nLines)
#define TEP_HTEXT(tep) ((tep)->hText)
#define TEP_CLICK_STUFF(tep) ((tep)->clikStuff)
#define TEP_CLICK_LOC(tep) ((tep)->clickLoc)
#define TEP_JUST(tep) ((tep)->just)
#define TEP_TX_FONT(tep) ((tep)->txFont)
#define TEP_TX_SIZE(tep) ((tep)->txSize)
#define TEP_IN_PORT(tep) ((tep)->inPort)

inline TEStyleHandle TE_GET_STYLE(TEHandle te)
{
    if(!TE_STYLIZED_P(te))
        return nullptr;
    return *(GUEST<TEStyleHandle> *)&TE_TX_FONT(te);
}
inline TEStyleHandle TEP_GET_STYLE(TERec *tep)
{
    if(!TEP_STYLIZED_P(tep))
        return nullptr;
    return *(GUEST<TEStyleHandle> *)&TEP_TX_FONT(tep);
}

#define TEP_HEIGHT_FOR_LINE(tep, lineno)                           \
    (TEP_STYLIZED_P(tep) && TEP_LINE_HEIGHT(tep) == -1      \
         ? ({                                                      \
               TEStyleHandle te_style = TEP_GET_STYLE(tep);        \
               LHElement *lh = *TE_STYLE_LH_TABLE(te_style); \
               LH_HEIGHT(&lh[lineno]);                             \
           })                                                      \
         : TEP_LINE_HEIGHT(tep))
#define TEP_ASCENT_FOR_LINE(tep, lineno)                           \
    (TEP_STYLIZED_P(tep) && TEP_FONT_ASCENT(tep) == -1      \
         ? ({                                                      \
               TEStyleHandle te_style = TEP_GET_STYLE(tep);        \
               LHElement *lh = *TE_STYLE_LH_TABLE(te_style); \
               LH_ASCENT(&lh[lineno]);                             \
           })                                                      \
         : TEP_FONT_ASCENT(tep))

#define TEP_TEXT_WIDTH(tep, text, start, len)     \
    (TEP_STYLIZED_P(tep)                          \
         ? ROMlib_StyleTextWidth(tep, start, len) \
         : TextWidth(text, start, len))

#define LINE_START(line_starts, index) \
    ((line_starts)[index])

#define TE_STYLE_SIZE_FOR_N_RUNS(n_runs)         \
    (sizeof(TEStyleRec)                          \
     - sizeof TE_STYLE_RUNS((TEStyleHandle)nullptr) \
     + (n_runs + 1) * sizeof *TE_STYLE_RUNS((TEStyleHandle)nullptr))

#define TE_STYLE_N_RUNS(te_style) ((*(te_style))->nRuns)
#define TE_STYLE_N_STYLES(te_style) ((*(te_style))->nStyles)
#define TE_STYLE_RUNS(te_style) ((*(te_style))->runs)
#define TE_STYLE_RUN(te_style, run_i) \
    (&TE_STYLE_RUNS(te_style)[run_i])
#define TE_STYLE_STYLE_TABLE(te_style) \
    ((*(te_style))->styleTab)
#define TE_STYLE_LH_TABLE(te_style) \
    ((*(te_style))->lhTab)
#define TE_STYLE_NULL_STYLE(te_style) \
    ((*(te_style))->nullStyle)

#define TE_STYLE_NULL_SCRAP(te_style) \
    (NULL_STYLE_NULL_SCRAP(TE_STYLE_NULL_STYLE(te_style)))

#define NULL_STYLE_NULL_SCRAP(null_style) \
    ((*(null_style))->nullScrap)

#define SCRAP_N_STYLES(scrap) \
    ((*(scrap))->scrpNStyles)

#define SCRAP_ST_ELT(scrap, elt_i) \
    (&((*(scrap))->scrpStyleTab)[elt_i])

#define SCRAP_SIZE_FOR_N_STYLES(n_styles) \
    (sizeof(StScrpRec) + ((n_styles)-1) * sizeof(ScrpSTElement))

#define STYLE_RUN_START_CHAR(run) ((run)->startChar)
#define STYLE_RUN_STYLE_INDEX(run) ((run)->styleIndex)


#define TS_FACE(ts) ((ts)->tsFace)

#define TS_FONT(ts) ((ts)->tsFont)
#define TS_SIZE(ts) ((ts)->tsSize)
#define TS_COLOR(ts) ((ts)->tsColor)


#define LH_HEIGHT(lh) ((lh)->lhHeight)

#define LH_ASCENT(lh) ((lh)->lhAscent)


#define STYLE_TABLE_SIZE_FOR_N_STYLES(n_styles) \
    ((n_styles) * sizeof(STElement))

#define ST_ELT(st, st_elt_i) \
    (&(*(st))[st_elt_i])

#define ST_ELT_FACE(st_elt) ((st_elt)->stFace)

#define ST_ELT_COUNT(st_elt) ((st_elt)->stCount)
#define ST_ELT_HEIGHT(st_elt) ((st_elt)->stHeight)
#define ST_ELT_ASCENT(st_elt) ((st_elt)->stAscent)
#define ST_ELT_FONT(st_elt) ((st_elt)->stFont)
#define ST_ELT_SIZE(st_elt) ((st_elt)->stSize)
#define ST_ELT_COLOR(st_elt) ((st_elt)->stColor)

#define SCRAP_ELT_START_CHAR(scrap_elt) \
    ((scrap_elt)->scrpStartChar)

extern bool adjust_attrs(TextStyle *orig_attrs, TextStyle *new_attrs,
                         TextStyle *dst_attrs, TextStyle *continuous_attrs,
                         int16_t mode);
extern int16_t make_style_run_at(TEStyleHandle te_style, int16_t sel);
extern int16_t get_style_index(TEStyleHandle te_style, TextStyle *attrs,
                               int incr_count_p);
extern void release_style_index(TEStyleHandle te_style, int16_t style_index);
extern void stabilize_style_info(TEStyleHandle te_style);
extern void te_style_combine_runs(TEStyleHandle te_style);

}
