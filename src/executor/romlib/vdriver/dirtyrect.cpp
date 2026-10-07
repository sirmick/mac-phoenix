/* Copyright 1995 - 1997 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <vdriver/vdriver.h>
#include <vdriver/dirtyrect.h>
#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <prefs/prefs.h>
#include <commandline/flags.h>
#include <vdriver/autorefresh.h>
#include <OSEvent.h>
#include <algorithm>


using namespace Executor;

/* We will glom a new rectangle into an existing one if it adds no more
 * than this many pixels.
 */
#define ACCEPTABLE_PIXELS_ADDED 8000

static inline bool
rects_overlap_p(int top, int left, int bottom, int right,
                const DirtyRects::Rect *r)
{
    return (top < r->bottom
            && r->top < bottom
            && left < r->right
            && r->left < right);
}

/* This routine is only valid for non-overlapping rectangles! */
static inline unsigned long
area_added(int top, int left, int bottom, int right, int r1_area,
           const DirtyRects::Rect *r2)
{
    int t, l, b, r, new_area, r2_area;

    t = std::min(top, r2->top);
    l = std::min(left, r2->left);
    b = std::max(bottom, r2->bottom);
    r = std::max(right, r2->right);

    /* This works because we know the rects don't overlap. */
    new_area = (r - l) * (b - t);
    r2_area = (r2->right - r2->left) * (r2->bottom - r2->top);

    return new_area - (r1_area + r2_area);
}

static inline void
union_rect(int top, int left, int bottom, int right, DirtyRects::Rect *r)
{
    if(top < r->top)
        r->top = top;
    if(left < r->left)
        r->left = left;
    if(bottom > r->bottom)
        r->bottom = bottom;
    if(right > r->right)
        r->right = right;
}

/* This function adds the specified rectangle to the dirty rect list.
 * If this rectangle is very close to another rectangle already in the
 * rect list, they may be glommed together into one larger rectangle.
 * Since we don't allow overlapping rects in the dirty rect list, if
 * this rect overlaps any rects already in the list, they will be
 * glommed together into a larger rectange.  Since that larger
 * rectangle might suddenly overlap other rectangles already in the
 * list, that glommed rectangle must get reinserted.
 */
void DirtyRects::add(int top, int left, int bottom, int right)
{
    unsigned long best_area_added;
    int i, best;

    int width = vdriver->width();
    int height = vdriver->height();

    top = std::clamp(top, 0, height);
    bottom = std::clamp(bottom, 0, height);
    left = std::clamp(left, 0, width);
    right = std::clamp(right, 0, width);

    if(bottom <= top || right <= left)
        return;
    

    /* Quickly handle the common case of no dirty rects. */
    if(rects_.empty())
    {
        rects_.push_back({top, left, bottom, right});
        return;
    }

    /* Otherwise, glom away! */
    for(bool done = false; !done;)
    {
        int new_area;

        new_area = (right - left) * (bottom - top);

        /* Figure out which union adds the least new area. */
        best_area_added = ~0UL;
        best = 0;
        for(i = rects_.size() - 1; i >= 0; i--)
        {
            unsigned long added;

            /* We don't allow overlapping rectangles, so if this overlaps
             * _any_ of them, then we glom them together.
             */
            if(rects_overlap_p(top, left, bottom, right, &rects_[i]))
            {
                best_area_added = 0;
                best = i;
                break;
            }
            else
            {
                /* We know that they don't overlap. */
                added = area_added(top, left, bottom, right, new_area,
                                   &rects_[i]);
                if(added < best_area_added)
                {
                    best_area_added = added;
                    best = i;
                }
            }
        }

        /* Add this rect to the dirty list, either by glomming together
         * with an existing rect, or by adding a new rect.
         */
        if(best_area_added <= ACCEPTABLE_PIXELS_ADDED
           || rects_.size() == MAX_DIRTY_RECTS)
        {
            if(rects_.size() == 1)
            {
                union_rect(top, left, bottom, right, &rects_[0]);
                done = true;
            }
            else
            {
                Rect *d = &rects_[best];

                /* We now re-insert the glommed rectangle into the list, to
                 * see if it overlaps anyone else.
                 */

                bool old_rect_grows_p = false;
                if(d->top <= top)
                    top = d->top;
                else
                    old_rect_grows_p = true;

                if(d->left <= left)
                    left = d->left;
                else
                    old_rect_grows_p = true;

                if(d->bottom >= bottom)
                    bottom = d->bottom;
                else
                    old_rect_grows_p = true;

                if(d->right >= right)
                    right = d->right;
                else
                    old_rect_grows_p = true;

                /* If this new rect is not entirely subsumed by the rect
                 * it interesects, then we delete the subsumed rect,
                 * and insert the union of the two rectangles into
                 * our rect list.  Otherwise, we're done, because the
                 * new rectangle doesn't add any new information.
                 */
                if(old_rect_grows_p)
                {
                    *d = rects_.back();
                    rects_.pop_back();
                }
                else
                    done = true;
            }
        }
        else /* No glomming required. */
        {
            /* Just add a new rectangle to the list. */
            rects_.push_back({top, left, bottom, right});
            done = true;
        }
    }
}

DirtyRects::Rects DirtyRects::getAndClear()
{
    auto rects = rects_;
    rects_.clear();
    return rects;
}


void Executor::dirty_rect_accrue(int top, int left, int bottom, int right)
{
    if(bottom <= top || right <= left
       || ROMlib_refresh)
        return;

    /* Note that Executor has touched the screen. */
    note_executor_changed_screen(top, bottom);

    vdriver->updateScreen(top, left, bottom, right);
}

