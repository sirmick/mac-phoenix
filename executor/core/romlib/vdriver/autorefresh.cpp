/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <vdriver/autorefresh.h>
#include <vdriver/vdriver.h>
#include <vdriver/refresh.h>
#include <commandline/flags.h>
#include <prefs/prefs.h>

/* This file provides a mechanism to detect when applications are
 * bypassing QuickDraw and writing directly to screen memory.
 * It works by partitioning the screen into NUM_AUTOREFRESH_STRIPS
 * horizontal strips of equal size.  Each strip is periodically
 * checksummed and compared against the last checksum.  If the
 * checksum value changes, and QuickDraw didn't change that area
 * of the screen, we know we need to turn on "refresh" mode.
 */

namespace Executor
{

static bool executor_changed_screen_p[NUM_AUTOREFRESH_STRIPS];

/* Returns a value that will tend to change when the screen contents change
 * for the specified horizontal strip on the screen.
 */
static uint32_t
checksum_strip(int which_strip)
{
    int n, row_size, strip_rows, next_row_delta;
    const uint32_t *base;
    uint32_t sum;

    strip_rows = vdriver->height() / NUM_AUTOREFRESH_STRIPS;
    base = (const uint32_t *)(vdriver->framebuffer()
                              + which_strip * strip_rows * vdriver->rowBytes());
    row_size = vdriver->rowBytes() / (4U * sizeof(uint32_t));
    next_row_delta = (vdriver->rowBytes() * 2 / sizeof(uint32_t)) - (row_size * 4U);

    /* Sum (most of) the longs on every other row in this strip. */
    for(sum = 0, n = strip_rows; n > 0; n -= 2)
    {
        int r;

        for(r = row_size; r > 0; r--)
        {
            sum += base[0] + base[1] + base[2] + base[3];
            base += 4;
        }

        base += next_row_delta;
    }

    return sum;
}

/* Call this when part of the screen has been changed by Executor,
 * so we don't mistakenly think we need refresh mode.
 */
void note_executor_changed_screen(int top, int bottom)
{
    int i, first, last, strip_rows;

    strip_rows = vdriver->height() / NUM_AUTOREFRESH_STRIPS;
    first = top / strip_rows;
    last = (bottom - 1) / strip_rows;

    if(first >= (int)NUM_AUTOREFRESH_STRIPS || last < 0)
        return;

    if(first < 0)
        first = 0;
    if(last > (int)NUM_AUTOREFRESH_STRIPS - 1)
        last = NUM_AUTOREFRESH_STRIPS - 1;

    for(i = first; i <= last; i++)
        executor_changed_screen_p[i] = true;
}

void note_executor_changed_screen()
{
    std::fill(std::begin(executor_changed_screen_p), std::end(executor_changed_screen_p), true);
}

/* Returns true if refresh should be turned on. */
bool autodetect_refresh(void)
{
    static uint32_t last_checksum[NUM_AUTOREFRESH_STRIPS];
    static bool last_checksum_valid_p[NUM_AUTOREFRESH_STRIPS];
    bool need_refresh_p;
    int i;

    /* We don't need to autodetect refresh if we're already doing refresh
   * or if we have direct screen access.
   */
    if(ROMlib_refresh != 0)
    {
        memset(last_checksum_valid_p, 0, sizeof last_checksum_valid_p);
        return false;
    }

    need_refresh_p = false; /* default value */

    /* Compute checksums for the screen. */
    for(i = NUM_AUTOREFRESH_STRIPS - 1; i >= 0; i--)
    {
        if(executor_changed_screen_p[i])
        {
            executor_changed_screen_p[i] = false; /* reset it */
            last_checksum_valid_p[i] = false;
        }
        else
        {
            uint32_t c;
            c = checksum_strip(i);
            if(last_checksum_valid_p[i] && c != last_checksum[i])
                need_refresh_p = true;
            last_checksum[i] = c;
            last_checksum_valid_p[i] = true;
        }
    }

    return need_refresh_p;
}
}
