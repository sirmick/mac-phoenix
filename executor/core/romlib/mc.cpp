/* MacPhoenix: the movie controller, Executor's own 'play' component
 * (docs/executor/QuickTime.md, route B; QuickTime.yaml's MovieController
 * calls).
 *
 * Inside Macintosh: QuickTime, "Movie Controller Components", and
 * Universal Interfaces 3.4's Movies.h: NewMovieController opens the 'play'
 * component and gives it the movie; the application passes its events to
 * MCIsPlayerEvent, idles it, and asks it to act with MCDoAction, each
 * action shown first to its action filter. The bar under the movie is
 * Executor's drawing, 16 pixels high as QuickTime 2.5's: a speaker, play
 * and pause, the time slider, and steps back and forward. Editing (cut,
 * copy, paste) isn't done: the edit menu's items are left disabled. */
#include <base/common.h>
#include <QuickTime.h>
#include <Components.h>
#include <EventMgr.h>
#include <MemoryMgr.h>
#include <MenuMgr.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <OSUtil.h>
#include <ToolboxEvent.h>
#include <ToolboxUtil.h>
#include <WindowMgr.h>
#include <base/functions.impl.h>
#include <base/traps.impl.h>
#include <rsys/component.h>
#include <rsys/quicktime.h>
#include <syn68k_public.h>

#include <algorithm>
#include <cstring>
#include <map>

using namespace Executor;

namespace
{
/* Movies.h's */
enum
{
    mcTopLeftMovie = 1 << 0,
    mcScaleMovieToFit = 1 << 1,
    mcWithBadge = 1 << 2,
    mcNotVisible = 1 << 3,
    mcFlagSuppressStepButtons = 1 << 1,
    mcFlagSuppressSpeakerButton = 1 << 2,
    mcPositionDontInvalidate = 1 << 5,
    mcInfoHasSound = 1 << 5,
    mcInfoIsPlaying = 1 << 6,
    mcInfoIsLooping = 1 << 7,
    mcInfoIsInPalindrome = 1 << 8,
    mcInfoEditingEnabled = 1 << 9,
    nextTimeMediaSample = 1 << 0,
};

enum : int16_t
{
    mcActionIdle = 1,
    mcActionDraw = 2,
    mcActionActivate = 3,
    mcActionDeactivate = 4,
    mcActionMouseDown = 5,
    mcActionKey = 6,
    mcActionPlay = 8,
    mcActionGoToTime = 12,
    mcActionSetVolume = 14,
    mcActionGetVolume = 15,
    mcActionStep = 18,
    mcActionSetLooping = 21,
    mcActionGetLooping = 22,
    mcActionSetLoopIsPalindrome = 23,
    mcActionGetLoopIsPalindrome = 24,
    mcActionSetGrowBoxBounds = 25,
    mcActionControllerSizeChanged = 26,
    mcActionSetSelectionBegin = 29,
    mcActionSetSelectionDuration = 30,
    mcActionSetKeysEnabled = 32,
    mcActionGetKeysEnabled = 33,
    mcActionSetPlaySelection = 34,
    mcActionGetPlaySelection = 35,
    mcActionSetUseBadge = 36,
    mcActionGetUseBadge = 37,
    mcActionSetFlags = 38,
    mcActionGetFlags = 39,
    mcActionSetPlayEveryFrame = 40,
    mcActionGetPlayEveryFrame = 41,
    mcActionGetPlayRate = 42,
    mcActionShowBalloon = 43,
    mcActionBadgeClick = 44,
    mcActionMovieClick = 45,
    mcActionSuspend = 46,
    mcActionResume = 47,
    mcActionSetControllerKeysEnabled = 48,
    mcActionGetTimeSliderRect = 49,
    mcActionMovieEdited = 50,
    mcActionGetDragEnabled = 51,
    mcActionSetDragEnabled = 52,
    mcActionGetSelectionBegin = 53,
    mcActionGetSelectionDuration = 54,
    mcActionPrerollAndPlay = 55,
    mcActionGetCursorSettingEnabled = 56,
    mcActionSetCursorSettingEnabled = 57,
    mcActionSetColorTable = 58,
    mcActionAdjustCursor = 65,
};

enum : uint32_t
{
    kVideoMediaType = "vide"_4,
    kSoundMediaType = "soun"_4,
};

enum
{
    kBarHeight = 16,
    kButton = 16,
    kThumbWidth = 8,
    kBadge = 14,
};

struct Controller
{
    Movie movie = nullptr;
    WindowPtr window = nullptr;
    CGrafPtr port = nullptr;
    Rect movie_rect;              /* the movie's box */
    Rect bar;                     /* the controls */
    bool attached = true, visible = true, active = true;
    bool editing = false, keys = false, looping = false, palindrome = false;
    bool play_selection = false, badge = false, every_frame = false, drag = false;
    bool cursor_setting = true;
    int32_t flags = 0;            /* mcFlag... */
    void *filter = nullptr;
    bool filter_refcon = false;
    int32_t refcon = 0;
    RgnHandle clip = nullptr, movie_clip = nullptr;
    TimeValue duration = 0;       /* without a movie (MCSetDuration) */
    int16_t mute_volume = 0;      /* the volume before the speaker muted it */
    int thumb_drawn = -1;         /* where the thumb was drawn */
    bool playing_drawn = false;
    bool redraw = false;          /* an update or MCInvalidate came */
    Rect grow_bounds;
};

std::map<uint32_t, Controller> controllers;

uint32_t key_of(ComponentInstance mc)
{
    return ptr_to_longint(mc);
}

Controller *of(ComponentInstance mc)
{
    auto it = controllers.find(key_of(mc));
    return it == controllers.end() ? nullptr : &it->second;
}

/* a value MCDoAction carries in its pointer, and back. Not
   ptr_to_longint: a value needn't be an address in guest memory (a
   negative volume, the speaker muting, is in no window), so the guest
   long is the one whose window's offset gives this host address */
int32_t value_of(void *params)
{
    if(!params)
        return 0;
    uint64_t h = (uintptr_t)params;
    for(int i = 0; i < OFFSET_TABLE_SIZE; i++)
    {
        uint64_t g = h - ROMlib_offsets[i];
        if(g <= 0xFFFFFFFFull && (g >> 30) == (uint64_t)i)
            return (int32_t)(uint32_t)g;
    }
    return (int32_t)h;
}

void *as_param(int32_t v)
{
    return ptr_from_longint<void *>(v);
}

Rect rect(int top, int left, int bottom, int right)
{
    Rect r;
    SetRect(&r, left, top, right, bottom);
    return r;
}

bool empty(const Rect &r)
{
    return r.bottom <= r.top || r.right <= r.left;
}

/* the parts of the bar */
Rect speaker_rect(const Controller &c)
{
    if(c.flags & mcFlagSuppressSpeakerButton)
        return rect(c.bar.top, c.bar.left, c.bar.top, c.bar.left);
    return rect(c.bar.top, c.bar.left, c.bar.bottom, c.bar.left + kButton);
}

Rect play_rect(const Controller &c)
{
    Rect s = speaker_rect(c);
    int left = s.right > s.left ? s.right - 1 : (int)c.bar.left;
    return rect(c.bar.top, left, c.bar.bottom, left + kButton);
}

Rect step_rect(const Controller &c, bool forward)
{
    if(c.flags & mcFlagSuppressStepButtons)
        return rect(c.bar.top, c.bar.right, c.bar.top, c.bar.right);
    int right = forward ? (int)c.bar.right : c.bar.right - kButton + 1;
    return rect(c.bar.top, right - kButton, c.bar.bottom, right);
}

Rect slider_rect(const Controller &c)
{
    Rect p = play_rect(c), b = step_rect(c, false);
    int right = b.right > b.left ? b.left + 1 : (int)c.bar.right;
    return rect(c.bar.top, p.right - 1, c.bar.bottom, right);
}

TimeValue movie_duration(const Controller &c)
{
    return c.movie ? C_GetMovieDuration(c.movie) : c.duration;
}

TimeValue movie_time(const Controller &c)
{
    return c.movie ? C_GetMovieTime(c.movie, nullptr) : 0;
}

bool playing(const Controller &c)
{
    return c.movie && C_GetMovieRate(c.movie) != 0;
}

int thumb_x(const Controller &c)
{
    Rect s = slider_rect(c);
    int span = (s.right - s.left) - 6 - kThumbWidth;
    TimeValue d = movie_duration(c);
    if(span <= 0 || d <= 0)
        return s.left + 3;
    TimeValue t = std::max<TimeValue>(0, std::min(movie_time(c), d));
    return s.left + 3 + (int)((int64_t)span * t / d);
}

void rgb(uint16_t v)
{
    RGBColor c;
    c.red = c.green = c.blue = v;
    RGBForeColor(&c);
}

void hline(int y, int x0, int x1)
{
    MoveTo(x0, y);
    LineTo(x1, y);
}

/* a triangle pointing right (dir 1) or left (-1), its point at x */
void triangle(int x, int y, int size, int dir)
{
    for(int i = 0; i < size; i++)
    {
        int half = i;
        MoveTo(x - dir * (size - 1 - i), y - half);
        LineTo(x - dir * (size - 1 - i), y + half);
    }
}

/* the current port is the controller's; draws the bar */
void draw_bar(Controller &c)
{
    if(!c.visible || empty(c.bar))
        return;
    PenNormal();
    RGBColor black, white;
    black.red = black.green = black.blue = 0;
    white.red = white.green = white.blue = 0xFFFF;
    RGBBackColor(&white);
    Rect b = c.bar;
    rgb(0xCCCC);
    PaintRect(&b);
    RGBForeColor(&black);
    FrameRect(&b);
    uint16_t ink = c.active ? 0x0000 : 0x8888;
    int mid = (b.top + b.bottom) / 2;

    Rect s = speaker_rect(c);
    if(!empty(s))
    {
        RGBForeColor(&black);
        MoveTo(s.right - 1, s.top);
        LineTo(s.right - 1, s.bottom - 1);
        rgb(ink);
        Rect box = rect(mid - 2, s.left + 4, mid + 2, s.left + 7);
        PaintRect(&box);
        for(int i = 0; i < 4; i++)
        {
            MoveTo(s.left + 7 + i, mid - 2 - i);
            LineTo(s.left + 7 + i, mid + 1 + i);
        }
        if(c.movie && C_GetMovieVolume(c.movie) <= 0)
        {
            MoveTo(s.left + 3, mid + 5);
            LineTo(s.left + 12, mid - 5);
        }
    }

    Rect p = play_rect(c);
    RGBForeColor(&black);
    MoveTo(p.right - 1, p.top);
    LineTo(p.right - 1, p.bottom - 1);
    rgb(ink);
    c.playing_drawn = playing(c);
    if(c.playing_drawn)
    {
        Rect l = rect(mid - 4, p.left + 5, mid + 5, p.left + 7);
        Rect r = rect(mid - 4, p.left + 9, mid + 5, p.left + 11);
        PaintRect(&l);
        PaintRect(&r);
    }
    else
        triangle(p.left + 10, mid, 5, 1);

    Rect sl = slider_rect(c);
    RGBForeColor(&black);
    hline(mid - 1, sl.left + 4, sl.right - 5);
    rgb(0xFFFF);
    hline(mid, sl.left + 4, sl.right - 5);
    c.thumb_drawn = thumb_x(c);
    Rect thumb = rect(b.top + 3, c.thumb_drawn, b.bottom - 3, c.thumb_drawn + kThumbWidth);
    rgb(c.active ? 0xFFFF : 0xCCCC);
    PaintRect(&thumb);
    rgb(ink);
    FrameRect(&thumb);

    for(int fwd = 0; fwd < 2; fwd++)
    {
        Rect st = step_rect(c, fwd);
        if(empty(st))
            continue;
        RGBForeColor(&black);
        MoveTo(st.left, st.top);
        LineTo(st.left, st.bottom - 1);
        rgb(ink);
        int x = fwd ? st.left + 10 : st.left + 5;
        triangle(x, mid, 4, fwd ? 1 : -1);
        int bar_x = fwd ? st.left + 11 : st.left + 4;
        MoveTo(bar_x, mid - 3);
        LineTo(bar_x, mid + 3);
    }
    RGBForeColor(&black);
}

struct PortGuard
{
    GUEST<CGrafPtr> port;
    GUEST<GDHandle> dev;
    RGBColor fg, bg;
    PortGuard(CGrafPtr p)
    {
        GetGWorld(&port, &dev);
        if(p)
            SetPort((GrafPtr)p);
        GetForeColor(&fg);
        GetBackColor(&bg);
    }
    ~PortGuard()
    {
        RGBForeColor(&fg);
        RGBBackColor(&bg);
        SetGWorld(port, dev);
    }
};

void show_bar(Controller &c)
{
    if(!c.port || !c.visible)
        return;
    PortGuard g(c.port);
    RgnHandle save = NewRgn();
    GetClip(save);
    if(c.clip)
    {
        RgnHandle r = NewRgn();
        SectRgn(save, c.clip, r);
        SetClip(r);
        DisposeRgn(r);
    }
    draw_bar(c);
    SetClip(save);
    DisposeRgn(save);
}

void show_movie(Controller &c)
{
    if(!c.movie)
        return;
    C_UpdateMovie(c.movie);
    C_MoviesTask(c.movie, 0);
}

void place(Controller &c, const Rect &movie_rect)
{
    c.movie_rect = movie_rect;
    if(c.movie)
        C_SetMovieBox(c.movie, &c.movie_rect);
    int width = movie_rect.right - movie_rect.left;
    if(width <= 0)
        width = 160;
    if(c.attached)
        c.bar = rect(movie_rect.bottom, movie_rect.left, movie_rect.bottom + kBarHeight, movie_rect.left + width);
}

Rect bounds(const Controller &c)
{
    Rect r = c.visible ? c.bar : rect(0, 0, 0, 0);
    if(c.attached && !empty(c.movie_rect))
    {
        if(empty(r))
            r = c.movie_rect;
        else
            UnionRect(&r, &c.movie_rect, &r);
    }
    return r;
}

/* the application's filter: true when it handled the action itself.
   params goes as the long it is (value_of): an action's value in it is
   not always an address in guest memory */
bool filtered(Controller &c, ComponentInstance mc, int16_t action, void *params)
{
    if(!c.filter)
        return false;
    LONGINT p = value_of(params);
    if(c.filter_refcon)
        return UPP<Boolean(ComponentInstance, INTEGER, LONGINT, LONGINT)>(c.filter)(mc, action, p, c.refcon);
    GUEST<INTEGER> *a = (GUEST<INTEGER> *)NewPtr(sizeof(GUEST<INTEGER>));
    if(!a)
        return false;
    *a = action;
    bool r = UPP<Boolean(ComponentInstance, GUEST<INTEGER> *, LONGINT)>(c.filter)(mc, a, p);
    DisposePtr((Ptr)a);
    return r;
}

ComponentResult act(ComponentInstance mc, int16_t action, void *params);

void set_time(Controller &c, TimeValue t)
{
    if(!c.movie)
        return;
    C_SetMovieTimeValue(c.movie, std::max<TimeValue>(0, std::min(t, movie_duration(c))));
    show_movie(c);
    show_bar(c);
}

Fixed play_rate(const Controller &c)
{
    Fixed r = c.movie ? C_GetMoviePreferredRate(c.movie) : 0;
    return r ? r : 0x10000;
}

void play(Controller &c, Fixed rate)
{
    if(!c.movie)
        return;
    if(rate)
    {
        TimeValue t = movie_time(c), d = movie_duration(c);
        if(rate > 0 && t >= d)
            C_GoToBeginningOfMovie(c.movie);
        else if(rate < 0 && t <= 0)
            C_GoToEndOfMovie(c.movie);
    }
    C_SetMovieRate(c.movie, rate);
    show_bar(c);
}

/* n frames on (back when negative), from the video's samples */
void step(Controller &c, int n)
{
    if(!c.movie || !n)
        return;
    C_SetMovieRate(c.movie, 0);
    TimeValue t = movie_time(c);
    GUEST<OSType> vide = kVideoMediaType;
    for(int i = 0; i < std::abs(n); i++)
    {
        GUEST<TimeValue> next, dur;
        Fixed dir = n > 0 ? 0x10000 : -0x10000;
        C_GetMovieNextInterestingTime(c.movie, nextTimeMediaSample, 1, &vide, t, dir, &next, &dur);
        if(next < 0 || next == t)
            break;
        t = next;
    }
    set_time(c, t);
}

void click_bar(Controller &c, ComponentInstance mc, Point where)
{
    PortGuard g(c.port);
    Rect pr = play_rect(c);
    if(PtInRect(where, &pr))
    {
        act(mc, mcActionPlay, as_param(playing(c) ? 0 : play_rate(c)));
        while(StillDown())
            ;
        return;
    }
    Rect s = speaker_rect(c);
    if(!empty(s) && PtInRect(where, &s))
    {
        if(c.movie)
        {
            int16_t v = C_GetMovieVolume(c.movie);
            if(v > 0)
            {
                c.mute_volume = v;
                act(mc, mcActionSetVolume, as_param(-v));
            }
            else
                act(mc, mcActionSetVolume, as_param(c.mute_volume ? c.mute_volume : 0x100));
        }
        while(StillDown())
            ;
        return;
    }
    for(int fwd = 0; fwd < 2; fwd++)
    {
        Rect st = step_rect(c, fwd);
        if(!empty(st) && PtInRect(where, &st))
        {
            unsigned long next = TickCount();
            do
            {
                if(TickCount() >= next)
                {
                    act(mc, mcActionStep, as_param(fwd ? 1 : -1));
                    next = TickCount() + 8;
                }
            } while(StillDown());
            return;
        }
    }
    Rect sl = slider_rect(c);
    if(PtInRect(where, &sl))
    {
        bool was = playing(c);
        if(c.movie)
            C_SetMovieRate(c.movie, 0);
        int span = (sl.right - sl.left) - 6 - kThumbWidth;
        do
        {
            GUEST<Point> pt;
            GetMouse(&pt);
            Point p = pt;
            int x = std::max<int>(0, std::min<int>(span, p.h - sl.left - 3 - kThumbWidth / 2));
            TimeValue t = span > 0 ? (TimeValue)((int64_t)movie_duration(c) * x / span) : 0;
            if(t != movie_time(c))
                set_time(c, t);
        } while(StillDown());
        if(was)
            play(c, play_rate(c));
    }
}

void idle(Controller &c, ComponentInstance mc)
{
    if(!c.movie)
        return;
    if(c.redraw)
    {
        c.redraw = false;
        show_movie(c);
        show_bar(c);
    }
    Fixed rate = C_GetMovieRate(c.movie);
    if(rate)
    {
        TimeValue t = movie_time(c), d = movie_duration(c);
        TimeValue start = 0, end = d;
        if(c.play_selection)
        {
            GUEST<TimeValue> st, du;
            C_GetMovieSelection(c.movie, &st, &du);
            if(du > 0)
            {
                start = st;
                end = st + du;
            }
        }
        bool done = rate > 0 ? t >= end : t <= start;
        if(done)
        {
            if(c.looping && c.palindrome)
                C_SetMovieRate(c.movie, -rate);
            else if(c.looping)
                C_SetMovieTimeValue(c.movie, rate > 0 ? start : end);
            else
                C_SetMovieRate(c.movie, 0);
        }
    }
    C_MoviesTask(c.movie, 0);
    if(thumb_x(c) != c.thumb_drawn || playing(c) != c.playing_drawn)
        show_bar(c);
}

bool key(Controller &c, ComponentInstance mc, int8_t ch, int32_t modifiers)
{
    switch((uint8_t)ch)
    {
        case ' ':
        case '\r':
            act(mc, mcActionPlay, as_param(playing(c) ? 0 : play_rate(c)));
            return true;
        case 0x1C: /* left arrow */
            act(mc, mcActionStep, as_param(-1));
            return true;
        case 0x1D: /* right arrow */
            act(mc, mcActionStep, as_param(1));
            return true;
        case 0x1E: /* up arrow */
        case 0x1F: /* down arrow */
            if(c.movie)
            {
                int16_t v = C_GetMovieVolume(c.movie) + ((uint8_t)ch == 0x1E ? 0x20 : -0x20);
                act(mc, mcActionSetVolume, as_param(std::max<int16_t>(0, std::min<int16_t>(v, 0x100))));
            }
            return true;
    }
    return false;
}

void put_boolean(void *params, bool v)
{
    if(params)
        *(GUEST<Boolean> *)params = v;
}

/* an action, shown to the filter first */
ComponentResult act(ComponentInstance mc, int16_t action, void *params)
{
    Controller *cp = of(mc);
    if(!cp)
        return badComponentInstance;
    Controller &c = *cp;
    if(filtered(c, mc, action, params))
        return noErr;
    switch(action)
    {
        case mcActionIdle:
            idle(c, mc);
            break;
        case mcActionDraw:
            show_movie(c);
            show_bar(c);
            break;
        case mcActionActivate:
        case mcActionDeactivate:
            c.active = action == mcActionActivate;
            show_bar(c);
            break;
        case mcActionMouseDown:
        case mcActionKey:
            break; /* the event's own handling, after the filter let it be */
        case mcActionPlay:
            play(c, value_of(params));
            break;
        case mcActionPrerollAndPlay:
            if(c.movie)
                C_PrerollMovie(c.movie, movie_time(c), value_of(params));
            play(c, value_of(params));
            break;
        case mcActionGoToTime:
            if(params && c.movie)
            {
                TimeRecord *tr = (TimeRecord *)params;
                TimeScale scale = C_GetMovieTimeScale(c.movie);
                int64_t v = ((int64_t)(int32_t)tr->value.hi << 32) | (uint32_t)tr->value.lo;
                if(tr->scale && tr->scale != scale)
                    v = v * scale / tr->scale;
                set_time(c, (TimeValue)v);
            }
            break;
        case mcActionSetVolume:
            if(c.movie)
                C_SetMovieVolume(c.movie, (int16_t)value_of(params));
            show_bar(c);
            break;
        case mcActionGetVolume:
            if(params)
                *(GUEST<INTEGER> *)params = c.movie ? C_GetMovieVolume(c.movie) : 0;
            break;
        case mcActionStep:
            step(c, (int16_t)value_of(params));
            break;
        case mcActionSetLooping:
            c.looping = value_of(params) != 0;
            break;
        case mcActionGetLooping:
            put_boolean(params, c.looping);
            break;
        case mcActionSetLoopIsPalindrome:
            c.palindrome = value_of(params) != 0;
            break;
        case mcActionGetLoopIsPalindrome:
            put_boolean(params, c.palindrome);
            break;
        case mcActionSetGrowBoxBounds:
            if(params)
                c.grow_bounds = *(Rect *)params;
            break;
        case mcActionSetSelectionBegin:
        case mcActionSetSelectionDuration:
            if(params && c.movie)
            {
                TimeRecord *tr = (TimeRecord *)params;
                GUEST<TimeValue> st, du;
                C_GetMovieSelection(c.movie, &st, &du);
                TimeValue v = tr->value.lo;
                if(action == mcActionSetSelectionBegin)
                    C_SetMovieSelection(c.movie, v, du);
                else
                    C_SetMovieSelection(c.movie, st, v);
            }
            break;
        case mcActionGetSelectionBegin:
        case mcActionGetSelectionDuration:
            if(params && c.movie)
            {
                TimeRecord *tr = (TimeRecord *)params;
                GUEST<TimeValue> st, du;
                C_GetMovieSelection(c.movie, &st, &du);
                tr->value.hi = 0;
                tr->value.lo = action == mcActionGetSelectionBegin ? (TimeValue)st : (TimeValue)du;
                tr->scale = C_GetMovieTimeScale(c.movie);
                tr->base = nullptr;
            }
            break;
        case mcActionSetKeysEnabled:
        case mcActionSetControllerKeysEnabled:
            c.keys = value_of(params) != 0;
            break;
        case mcActionGetKeysEnabled:
            put_boolean(params, c.keys);
            break;
        case mcActionSetPlaySelection:
            c.play_selection = value_of(params) != 0;
            break;
        case mcActionGetPlaySelection:
            put_boolean(params, c.play_selection);
            break;
        case mcActionSetUseBadge:
            c.badge = value_of(params) != 0;
            break;
        case mcActionGetUseBadge:
            put_boolean(params, c.badge);
            break;
        case mcActionSetFlags:
            c.flags = value_of(params);
            show_bar(c);
            break;
        case mcActionGetFlags:
            if(params)
                *(GUEST<LONGINT> *)params = c.flags;
            break;
        case mcActionSetPlayEveryFrame:
            c.every_frame = value_of(params) != 0;
            break;
        case mcActionGetPlayEveryFrame:
            put_boolean(params, c.every_frame);
            break;
        case mcActionGetPlayRate:
            if(params)
                *(GUEST<Fixed> *)params = c.movie ? C_GetMovieRate(c.movie) : 0;
            break;
        case mcActionGetTimeSliderRect:
            if(params)
                *(Rect *)params = slider_rect(c);
            break;
        case mcActionSetDragEnabled:
            c.drag = value_of(params) != 0;
            break;
        case mcActionGetDragEnabled:
            put_boolean(params, c.drag);
            break;
        case mcActionSetCursorSettingEnabled:
            c.cursor_setting = value_of(params) != 0;
            break;
        case mcActionGetCursorSettingEnabled:
            put_boolean(params, c.cursor_setting);
            break;
        case mcActionSetColorTable:
            if(c.movie)
                C_SetMovieColorTable(c.movie, (CTabHandle)params);
            break;
        case mcActionControllerSizeChanged:
        case mcActionSuspend:
        case mcActionResume:
        case mcActionMovieEdited:
        case mcActionShowBalloon:
        case mcActionBadgeClick:
        case mcActionMovieClick:
        case mcActionAdjustCursor:
            break;
        default:
            return badComponentSelector;
    }
    return noErr;
}

/* the controller's window, or the movie's port */
CGrafPtr port_of(Movie m)
{
    GUEST<CGrafPtr> port;
    GUEST<GDHandle> gdh;
    C_GetMovieGWorld(m, &port, &gdh);
    return port;
}

ComponentResult attach_movie(ComponentInstance mc, Movie m, WindowPtr w, Point where)
{
    Controller *cp = of(mc);
    if(!cp)
        return badComponentInstance;
    Controller &c = *cp;
    c.movie = m;
    c.window = w;
    c.port = w ? (CGrafPtr)w : (m ? port_of(m) : nullptr);
    if(m && w)
        C_SetMovieGWorld(m, (CGrafPtr)w, nullptr);
    Rect box = rect(0, 0, 0, 0);
    if(m)
        C_GetMovieBox(m, &box);
    OffsetRect(&box, where.h - box.left, where.v - box.top);
    place(c, box);
    return noErr;
}

/* QuickTime calls a controller made without a movie (MCSetMovie later)
   by these too */
void ensure(ComponentInstance mc)
{
    controllers.emplace(key_of(mc), Controller{});
}

ComponentResult close_controller(syn68k_addr_t params)
{
    /* the instance is the parameter */
    uint32_t ci = READUL(params + 4);
    auto it = controllers.find(ci);
    if(it != controllers.end())
    {
        if(it->second.clip)
            DisposeRgn(it->second.clip);
        if(it->second.movie_clip)
            DisposeRgn(it->second.movie_clip);
        controllers.erase(it);
    }
    return noErr;
}
}

/* --- the component ---------------------------------------------------------- */

void Executor::ROMlib_install_movie_controller()
{
    /* as QuickTime 2.5's standard controller describes itself (QTTest's
       component list on the Quadra): subtype "\0std", cmpFastDispatch */
    ROMlib_register_native_component("play"_4, 0x00737464, "appl"_4, "Movie Controller", "MovieController",
                                     0x00020500, cmpFastDispatch);
    traps::registerComponentFunction("MovieController", kComponentCloseSelect, "MCClose", close_controller);
}

ComponentInstance Executor::C_NewMovieController(Movie movie, const Rect *movierectp, LONGINT flags)
{
    ComponentInstance mc = OpenDefaultComponent("play"_4, 0);
    if(!mc)
        return nullptr;
    ensure(mc);
    Controller &c = *of(mc);
    c.badge = flags & mcWithBadge;
    c.visible = !(flags & mcNotVisible);
    CGrafPtr port = movie ? port_of(movie) : nullptr;
    attach_movie(mc, movie, (WindowPtr)port, Point{ 0, 0 });
    if(movierectp && !empty(*movierectp))
    {
        Rect area = *movierectp;
        Rect box = c.movie_rect;
        int w = box.right - box.left, h = box.bottom - box.top;
        int room_h = (area.bottom - area.top) - (c.visible ? kBarHeight : 0);
        int room_w = area.right - area.left;
        if((flags & mcScaleMovieToFit) && w > 0 && h > 0 && (w > room_w || h > room_h))
        {
            /* scaled down to fit, its proportions kept */
            if((int64_t)w * room_h > (int64_t)h * room_w)
            {
                h = (int)((int64_t)h * room_w / w);
                w = room_w;
            }
            else
            {
                w = (int)((int64_t)w * room_h / h);
                h = room_h;
            }
        }
        int left = area.left, top = area.top;
        if(!(flags & mcTopLeftMovie))
        {
            left += std::max(0, (room_w - w) / 2);
            top += std::max(0, (room_h - h) / 2);
        }
        place(c, rect(top, left, top + h, left + w));
    }
    return mc;
}

void Executor::C_DisposeMovieController(ComponentInstance controller)
{
    if(controller)
        CloseComponent(controller);
}

ComponentResult Executor::C_MCSetMovie(ComponentInstance mc, Movie theMovie, WindowRef movieWindow, Point where)
{
    ensure(mc);
    return attach_movie(mc, theMovie, movieWindow, where);
}

ComponentResult Executor::C_MCNewAttachedController(ComponentInstance mc, Movie theMovie, WindowRef w, Point where)
{
    ensure(mc);
    of(mc)->attached = true;
    return attach_movie(mc, theMovie, w, where);
}

ComponentResult Executor::C_MCRemoveAMovie(ComponentInstance mc, Movie m)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    if(c->movie == m)
        c->movie = nullptr;
    return noErr;
}

ComponentResult Executor::C_MCRemoveAllMovies(ComponentInstance mc)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->movie = nullptr;
    return noErr;
}

Movie Executor::C_MCGetIndMovie(ComponentInstance mc, INTEGER index)
{
    Controller *c = of(mc);
    return c && index <= 1 ? c->movie : nullptr;
}

ComponentResult Executor::C_MCIsPlayerEvent(ComponentInstance mc, const EventRecord *e)
{
    Controller *cp = of(mc);
    if(!cp || !e)
        return 0;
    Controller &c = *cp;
    WindowPtr ew = (WindowPtr)ptr_from_longint<Ptr>((uint32_t)(LONGINT)e->message);
    switch(e->what)
    {
        case nullEvent:
            act(mc, mcActionIdle, nullptr);
            return 0;
        case updateEvt:
            if(ew && ew == c.window)
            {
                /* drawn now and again at the next idle, after the
                   application's own update has erased what it had to */
                act(mc, mcActionDraw, ew);
                c.redraw = true;
            }
            return 0;
        case activateEvt:
            if(ew && ew == c.window)
                act(mc, (e->modifiers & activeFlag) ? mcActionActivate : mcActionDeactivate, nullptr);
            return 0;
        case mouseDown:
        {
            GUEST<WindowPtr> w;
            if(FindWindow(e->where, &w) != inContent || (WindowPtr)w != c.window || !c.window)
                return 0;
            PortGuard g(c.port);
            Point local = e->where;
            GUEST<Point> lp = local;
            GlobalToLocal(&lp);
            local = lp;
            if(c.visible && PtInRect(local, &c.bar))
            {
                if(filtered(c, mc, mcActionMouseDown, (void *)e))
                    return 1;
                click_bar(c, mc, local);
                return 1;
            }
            if(c.movie && PtInRect(local, &c.movie_rect))
            {
                if(filtered(c, mc, mcActionMovieClick, (void *)e))
                    return 1;
                act(mc, mcActionPlay, as_param(playing(c) ? 0 : play_rate(c)));
                return 1;
            }
            return 0;
        }
        case keyDown:
        case autoKey:
            if(!c.keys || !c.window || FrontWindow() != c.window || (e->modifiers & cmdKey))
                return 0;
            if(filtered(c, mc, mcActionKey, (void *)e))
                return 1;
            return key(c, mc, (int8_t)(e->message & 0xFF), e->modifiers);
    }
    return 0;
}

ComponentResult Executor::C_MCSetActionFilter(ComponentInstance mc, MCActionFilterUPP blob)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->filter = (void *)blob;
    c->filter_refcon = false;
    return noErr;
}

ComponentResult Executor::C_MCSetActionFilterWithRefCon(ComponentInstance mc, MCActionFilterWithRefConUPP blob,
                                                        LONGINT refCon)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->filter = (void *)blob;
    c->filter_refcon = true;
    c->refcon = refCon;
    return noErr;
}

ComponentResult Executor::C_MCDoAction(ComponentInstance mc, INTEGER action, void *params)
{
    return act(mc, action, params);
}

ComponentResult Executor::C_MCSetControllerAttached(ComponentInstance mc, Boolean attach)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->attached = attach;
    place(*c, c->movie_rect);
    return noErr;
}

ComponentResult Executor::C_MCIsControllerAttached(ComponentInstance mc)
{
    Controller *c = of(mc);
    return c ? c->attached : 0;
}

ComponentResult Executor::C_MCSetControllerPort(ComponentInstance mc, CGrafPtr gp)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->port = gp;
    return noErr;
}

CGrafPtr Executor::C_MCGetControllerPort(ComponentInstance mc)
{
    Controller *c = of(mc);
    return c ? c->port : nullptr;
}

ComponentResult Executor::C_MCSetVisible(ComponentInstance mc, Boolean visible)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->visible = visible;
    if(visible)
        show_bar(*c);
    return noErr;
}

ComponentResult Executor::C_MCGetVisible(ComponentInstance mc)
{
    Controller *c = of(mc);
    return c ? c->visible : 0;
}

ComponentResult Executor::C_MCGetControllerBoundsRect(ComponentInstance mc, Rect *r)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    if(r)
        *r = bounds(*c);
    return noErr;
}

ComponentResult Executor::C_MCSetControllerBoundsRect(ComponentInstance mc, const Rect *r)
{
    Controller *c = of(mc);
    if(!c || !r)
        return c ? paramErr : badComponentInstance;
    if(c->attached)
        place(*c, rect(r->top, r->left, r->bottom - (c->visible ? kBarHeight : 0), r->right));
    else
        c->bar = *r;
    return noErr;
}

RgnHandle Executor::C_MCGetControllerBoundsRgn(ComponentInstance mc)
{
    Controller *c = of(mc);
    RgnHandle r = NewRgn();
    if(c)
    {
        Rect b = bounds(*c);
        RectRgn(r, &b);
    }
    return r;
}

RgnHandle Executor::C_MCGetWindowRgn(ComponentInstance mc, WindowRef w)
{
    Controller *c = of(mc);
    RgnHandle r = NewRgn();
    if(c && w == c->window)
    {
        Rect b = bounds(*c);
        RectRgn(r, &b);
    }
    return r;
}

ComponentResult Executor::C_MCMovieChanged(ComponentInstance mc, Movie m)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    if(m == c->movie && m)
    {
        Rect box;
        C_GetMovieBox(m, &box);
        place(*c, box);
        show_bar(*c);
    }
    return noErr;
}

ComponentResult Executor::C_MCSetDuration(ComponentInstance mc, TimeValue duration)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->duration = duration;
    return noErr;
}

TimeValue Executor::C_MCGetCurrentTime(ComponentInstance mc, GUEST<TimeScale> *scale)
{
    Controller *c = of(mc);
    if(!c || !c->movie)
    {
        if(scale)
            *scale = 600;
        return 0;
    }
    if(scale)
        *scale = C_GetMovieTimeScale(c->movie);
    return movie_time(*c);
}

ComponentResult Executor::C_MCDraw(ComponentInstance mc, WindowRef w)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    if(w != c->window)
        return noErr;
    return act(mc, mcActionDraw, w);
}

ComponentResult Executor::C_MCActivate(ComponentInstance mc, WindowRef w, Boolean activate)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    return act(mc, activate ? mcActionActivate : mcActionDeactivate, nullptr);
}

ComponentResult Executor::C_MCIdle(ComponentInstance mc)
{
    return act(mc, mcActionIdle, nullptr);
}

ComponentResult Executor::C_MCKey(ComponentInstance mc, SignedByte k, LONGINT modifiers)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    return key(*c, mc, k, modifiers);
}

ComponentResult Executor::C_MCClick(ComponentInstance mc, WindowRef w, Point where, LONGINT when, LONGINT modifiers)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    if(w == c->window && c->visible && PtInRect(where, &c->bar))
        click_bar(*c, mc, where);
    return noErr;
}

ComponentResult Executor::C_MCEnableEditing(ComponentInstance mc, Boolean enabled)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    c->editing = enabled;
    return noErr;
}

LONGINT Executor::C_MCIsEditingEnabled(ComponentInstance mc)
{
    Controller *c = of(mc);
    return c ? c->editing : 0;
}

/* editing isn't done: nothing to cut, copy, paste, clear or undo */
Movie Executor::C_MCCopy(ComponentInstance mc)
{
    return nullptr;
}

Movie Executor::C_MCCut(ComponentInstance mc)
{
    return nullptr;
}

ComponentResult Executor::C_MCPaste(ComponentInstance mc, Movie srcMovie)
{
    return noErr;
}

ComponentResult Executor::C_MCClear(ComponentInstance mc)
{
    return noErr;
}

ComponentResult Executor::C_MCUndo(ComponentInstance mc)
{
    return noErr;
}

ComponentResult Executor::C_MCPositionController(ComponentInstance mc, const Rect *movieRect,
                                                 const Rect *controllerRect, LONGINT someFlags)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    if(movieRect)
        place(*c, *movieRect);
    if(controllerRect && !empty(*controllerRect))
        c->bar = *controllerRect;
    if(!(someFlags & mcPositionDontInvalidate) && c->port)
        c->redraw = true;
    return noErr;
}

ComponentResult Executor::C_MCGetControllerInfo(ComponentInstance mc, GUEST<LONGINT> *someFlags)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    int32_t f = 0;
    if(c->movie)
    {
        for(LONGINT i = 1; i <= C_GetMovieTrackCount(c->movie); i++)
        {
            Track t = C_GetMovieIndTrack(c->movie, i);
            GUEST<OSType> type;
            C_GetMediaHandlerDescription(C_GetTrackMedia(t), &type, nullptr, nullptr);
            if(type == kSoundMediaType && C_GetTrackEnabled(t))
                f |= mcInfoHasSound;
        }
        if(playing(*c))
            f |= mcInfoIsPlaying;
    }
    if(c->looping)
        f |= mcInfoIsLooping;
    if(c->looping && c->palindrome)
        f |= mcInfoIsInPalindrome;
    if(c->editing)
        f |= mcInfoEditingEnabled;
    if(someFlags)
        *someFlags = f;
    return noErr;
}

ComponentResult Executor::C_MCSetClip(ComponentInstance mc, RgnHandle theClip, RgnHandle movieClip)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    auto keep = [](RgnHandle &mine, RgnHandle theirs) {
        if(mine)
            DisposeRgn(mine);
        mine = nullptr;
        if(theirs)
        {
            mine = NewRgn();
            CopyRgn(theirs, mine);
        }
    };
    keep(c->clip, theClip);
    keep(c->movie_clip, movieClip);
    if(c->movie)
    {
        RgnHandle r = nullptr;
        if(c->clip || c->movie_clip)
        {
            r = NewRgn();
            if(c->clip && c->movie_clip)
                SectRgn(c->clip, c->movie_clip, r);
            else
                CopyRgn(c->clip ? c->clip : c->movie_clip, r);
        }
        C_SetMovieDisplayClipRgn(c->movie, r);
        if(r)
            DisposeRgn(r);
    }
    return noErr;
}

ComponentResult Executor::C_MCGetClip(ComponentInstance mc, GUEST<RgnHandle> *theClip, GUEST<RgnHandle> *movieClip)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    auto give = [](GUEST<RgnHandle> *out, RgnHandle mine) {
        if(!out)
            return;
        RgnHandle r = nullptr;
        if(mine)
        {
            r = NewRgn();
            CopyRgn(mine, r);
        }
        *out = r;
    };
    give(theClip, c->clip);
    give(movieClip, c->movie_clip);
    return noErr;
}

/* a small film frame at the movie's bottom left: the movie has a
   controller to show */
ComponentResult Executor::C_MCDrawBadge(ComponentInstance mc, RgnHandle movieRgn, GUEST<RgnHandle> *badgeRgn)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    Rect area = c->movie_rect;
    if(movieRgn)
        area = (*movieRgn)->rgnBBox;
    Rect b = rect(area.bottom - kBadge - 2, area.left + 2, area.bottom - 2, area.left + 2 + kBadge);
    if(c->port)
    {
        PortGuard g(c->port);
        PenNormal();
        rgb(0xFFFF);
        PaintRect(&b);
        rgb(0x0000);
        FrameRect(&b);
        Rect film = rect(b.top + 3, b.left + 3, b.bottom - 3, b.right - 3);
        PaintRect(&film);
        rgb(0xFFFF);
        for(int x = film.left + 1; x < film.right - 1; x += 3)
        {
            MoveTo(x, film.top + 1);
            LineTo(x, film.top + 1);
            MoveTo(x, film.bottom - 2);
            LineTo(x, film.bottom - 2);
        }
    }
    if(badgeRgn)
    {
        RgnHandle r = NewRgn();
        RectRgn(r, &b);
        *badgeRgn = r;
    }
    return noErr;
}

/* the edit menu: nothing to undo, cut, copy, paste or clear */
ComponentResult Executor::C_MCSetUpEditMenu(ComponentInstance mc, LONGINT modifiers, MenuHandle mh)
{
    if(!of(mc))
        return badComponentInstance;
    if(mh)
    {
        Str255 undo;
        memcpy(undo, "\pCan't Undo", 11);
        SetMenuItemText(mh, 1, undo);
        for(INTEGER item : { 1, 3, 4, 5, 6 })
            DisableItem(mh, item);
    }
    return noErr;
}

ComponentResult Executor::C_MCGetMenuString(ComponentInstance mc, LONGINT modifiers, INTEGER item, StringPtr aString)
{
    if(!of(mc))
        return badComponentInstance;
    static const char *const names[] = { "", "Can't Undo", "", "Cut", "Copy", "Paste", "Clear" };
    if(aString)
    {
        const char *s = item >= 1 && item <= 6 ? names[item] : "";
        aString[0] = strlen(s);
        memcpy(aString + 1, s, aString[0]);
    }
    return noErr;
}

ComponentResult Executor::C_MCPtInController(ComponentInstance mc, Point thePt, Boolean *inController)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    Rect b = bounds(*c);
    if(inController)
        *inController = PtInRect(thePt, &b);
    return noErr;
}

ComponentResult Executor::C_MCInvalidate(ComponentInstance mc, WindowRef w, RgnHandle invalidRgn)
{
    Controller *c = of(mc);
    if(!c)
        return badComponentInstance;
    if(w == c->window)
        c->redraw = true;
    return noErr;
}

ComponentResult Executor::C_MCAdjustCursor(ComponentInstance mc, WindowRef w, Point where, LONGINT modifiers)
{
    if(!of(mc))
        return badComponentInstance;
    return 0; /* the application's cursor stands */
}
