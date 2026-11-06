#include "gtest/gtest.h"

#include <hidpi/hidpi_internal.h>

using Executor::hidpi::impl::glyph_lefts;
using TextRun = Executor::hidpi::impl::Run;

/* Where --executor-scale's text puts its outline glyphs (glyph_lefts):
   words at their 1x places, the glyphs in a word evenly spaced. */
namespace
{
TextRun run_of(std::vector<uint16_t> glyphs, std::vector<float> xs, std::vector<float> advances)
{
    TextRun r{};
    r.glyphs = glyphs;
    r.xs = xs;
    r.advances = advances;
    return r;
}
constexpr uint16_t kSpace = 3;
}

/* "Pro" in a bitmap strike's advances (7, 5, 6 at 1x) with an outline r
   much narrower than its cell: the gaps between the glyphs are the same
   (centred in its cell, the r left a wide gap after the P), and the word
   still starts and ends where the 1x layout has it. */
TEST(HiDPILayout, wordEvenlySpaced)
{
    TextRun r = run_of({ 10, 11, 12 }, { 100, 107, 112 }, { 7, 5, 6 });
    std::vector<float> widths = { 13, 6, 11 }; /* at 2x */
    std::vector<float> at = glyph_lefts(r, widths, 0, 2, kSpace);
    float gap1 = at[1] - (at[0] + widths[0]), gap2 = at[2] - (at[1] + widths[1]);
    EXPECT_FLOAT_EQ(gap1, gap2);
    EXPECT_FLOAT_EQ(at[0] - 200, gap1 / 2);              /* half a gap before */
    EXPECT_FLOAT_EQ(236 - (at[2] + widths[2]), gap1 / 2); /* and after */
}

/* A space stays at its 1x origin and splits the run into words, each laid
   out on its own: the second word starts as it would drawn alone. */
TEST(HiDPILayout, spaceSeparatesWords)
{
    TextRun r = run_of({ 10, 11, kSpace, 12, 13 }, { 0, 6, 12, 15, 21 }, { 6, 6, 3, 6, 6 });
    std::vector<float> widths = { 10, 12, 6, 14, 8 };
    std::vector<float> at = glyph_lefts(r, widths, 0, 2, kSpace);
    EXPECT_FLOAT_EQ(at[2], 24);
    TextRun alone = run_of({ 12, 13 }, { 15, 21 }, { 6, 6 });
    std::vector<float> at2 = glyph_lefts(alone, { 14, 8 }, 0, 2, kSpace);
    EXPECT_FLOAT_EQ(at[3], at2[0]);
    EXPECT_FLOAT_EQ(at[4], at2[1]);
}

/* A glyph alone (DrawChar) is centred in its advance, bold's smear in its
   width; a pen that jumps (chExtra, spExtra) starts a new word. */
TEST(HiDPILayout, loneGlyphCentredAndPenJumps)
{
    TextRun one = run_of({ 10 }, { 50 }, { 8 });
    EXPECT_FLOAT_EQ(glyph_lefts(one, { 10 }, 2, 2, kSpace)[0], 100 + (16 - 12) / 2.0f);
    TextRun jump = run_of({ 10, 11 }, { 0, 9 }, { 6, 6 }); /* 3 of extra after the first */
    std::vector<float> at = glyph_lefts(jump, { 8, 8 }, 0, 2, kSpace);
    EXPECT_FLOAT_EQ(at[0], 2);
    EXPECT_FLOAT_EQ(at[1], 20);
}
