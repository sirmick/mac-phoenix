#pragma once
/* MacPhoenix: TrueType ('sfnt') fonts as bitmap strikes (sfnt.cpp). */

#include <FontMgr.h>

namespace Executor
{
/* What FMSwapFont answers for an outline font at a size (the strike's own
   ascent/descent may be taller, to hold every glyph). */
struct SfntMetrics
{
    INTEGER ascent, descent, widMax, leading;
};

/* An NFNT-format strike (FontRec, bit image, location and offset/width
   tables, fractional width table) of the 'sfnt' resource SFNT at PPEM
   pixels per em, in the system heap; nullptr when the font can't be read
   or the strike wouldn't fit NFNT's 16-bit offsets. */
extern Handle ROMlib_sfnt_strike(Handle sfnt, int ppem, SfntMetrics *metrics);
}
