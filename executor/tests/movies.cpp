/* MacPhoenix: Executor's Movie Toolbox's own bookkeeping (movies.cpp;
   docs/executor/QuickTime.md): a movie made in memory, its samples and
   their times, edits, the box, and the movie through a handle and back.
   Drawing goes through Apple's codecs, which QTTest checks
   (tests/guest/qt, ctest qt_executor_761). */
#include "gtest/gtest.h"
#include "compat.h"

#include <ImageCompression.h>
#include <MemoryMgr.h>
#include <QuickDraw.h>
#include <QuickTime.h>

#include <cstring>

using namespace Executor;

namespace
{
enum
{
    kRaw = "raw "_4,
    kVideo = "vide"_4,
    nextTimeMediaSample = 1,
    mediaSampleNotSync = 1,
};

SampleDescriptionHandle raw_description(int16_t w, int16_t h)
{
    ImageDescriptionHandle d = (ImageDescriptionHandle)NewHandleClear(sizeof(ImageDescription));
    (*d)->idSize = sizeof(ImageDescription);
    (*d)->cType = kRaw;
    (*d)->dataRefIndex = 1;
    (*d)->width = w;
    (*d)->height = h;
    (*d)->hRes = (*d)->vRes = 72 << 16;
    (*d)->frameCount = 1;
    (*d)->depth = 32;
    (*d)->clutID = -1;
    return (SampleDescriptionHandle)d;
}

/* An 80x60 movie, time scale 600: one video track of three samples of
   100 bytes, 60 units each, the second not a sync sample */
Movie three_samples(Track *track_out, Media *media_out)
{
    Movie m = C_NewMovie(1);
    Track t = C_NewMovieTrack(m, 80 << 16, 60 << 16, 0);
    Media md = C_NewTrackMedia(t, kVideo, 600, nullptr, 0);
    SampleDescriptionHandle desc = raw_description(80, 60);
    Handle data = NewHandle(100);
    C_BeginMediaEdits(md);
    for(int k = 0; k < 3; k++)
    {
        memset(*data, 'a' + k, 100);
        GUEST<TimeValue> at;
        EXPECT_EQ(noErr, C_AddMediaSample(md, data, 0, 100, 60, desc, 1, k == 1 ? mediaSampleNotSync : 0, &at));
        EXPECT_EQ(60 * k, at);
    }
    C_EndMediaEdits(md);
    EXPECT_EQ(noErr, C_InsertMediaIntoTrack(t, 0, 0, C_GetMediaDuration(md), 0x10000));
    DisposeHandle(data);
    DisposeHandle((Handle)desc);
    if(track_out)
        *track_out = t;
    if(media_out)
        *media_out = md;
    return m;
}
}

TEST(Movies, MadeInMemory)
{
    ASSERT_EQ(noErr, C_EnterMovies());
    Track t;
    Media md;
    Movie m = three_samples(&t, &md);
    ASSERT_NE(nullptr, m);
    EXPECT_EQ(1, C_GetMovieTrackCount(m));
    EXPECT_EQ(t, C_GetMovieIndTrack(m, 1));
    EXPECT_EQ(md, C_GetTrackMedia(t));
    EXPECT_EQ(t, C_GetMediaTrack(md));
    EXPECT_EQ(m, C_GetTrackMovie(t));
    EXPECT_EQ(600, C_GetMovieTimeScale(m));
    EXPECT_EQ(180, C_GetMediaDuration(md));
    EXPECT_EQ(180, C_GetTrackDuration(t));
    EXPECT_EQ(180, C_GetMovieDuration(m));
    EXPECT_EQ(3, C_GetMediaSampleCount(md));
    EXPECT_EQ(1, C_GetMediaSampleDescriptionCount(md));

    GUEST<OSType> type;
    C_GetMediaHandlerDescription(md, &type, nullptr, nullptr);
    EXPECT_EQ(kVideo, type);

    /* times and samples, 1-based */
    GUEST<LONGINT> n;
    GUEST<TimeValue> start, dur;
    C_MediaTimeToSampleNum(md, 90, &n, &start, &dur);
    EXPECT_EQ(2, n);
    EXPECT_EQ(60, start);
    EXPECT_EQ(60, dur);
    C_SampleNumToMediaTime(md, 3, &start, &dur);
    EXPECT_EQ(120, start);

    /* a sample's bytes, and whether it is a sync sample */
    Handle out = NewHandle(0);
    GUEST<LONGINT> size, index, count;
    GUEST<INTEGER> flags;
    EXPECT_EQ(noErr, C_GetMediaSample(md, out, 0, &size, 70, &start, &dur, nullptr, &index, 1, &count, &flags));
    EXPECT_EQ(100, size);
    EXPECT_EQ(60, start);
    EXPECT_EQ(1, index);
    EXPECT_EQ(mediaSampleNotSync, flags);
    EXPECT_EQ('b', (*out)[0]);
    EXPECT_EQ(noErr, C_GetMediaSample(md, out, 0, &size, 130, &start, &dur, nullptr, &index, 1, &count, &flags));
    EXPECT_EQ('c', (*out)[99]);
    EXPECT_EQ(0, flags);
    DisposeHandle(out);

    /* the next sample after 0 */
    GUEST<TimeValue> next, next_dur;
    GUEST<OSType> types[1] = { kVideo };
    C_GetMovieNextInterestingTime(m, nextTimeMediaSample, 1, types, 0, 0x10000, &next, &next_dur);
    EXPECT_EQ(60, next);
    EXPECT_EQ(60, next_dur);

    /* the box: the track's size; a new box scales the movie */
    Rect box;
    C_GetMovieBox(m, &box);
    EXPECT_EQ(0, box.top);
    EXPECT_EQ(0, box.left);
    EXPECT_EQ(60, box.bottom);
    EXPECT_EQ(80, box.right);
    Rect big;
    SetRect(&big, 10, 20, 170, 140);
    C_SetMovieBox(m, &big);
    C_GetMovieBox(m, &box);
    EXPECT_EQ(20, box.top);
    EXPECT_EQ(10, box.left);
    EXPECT_EQ(140, box.bottom);
    EXPECT_EQ(170, box.right);

    C_DisposeMovie(m);
}

TEST(Movies, Edits)
{
    Track t;
    Media md;
    Movie m = three_samples(&t, &md);
    /* the media's second sample again at the end: 60 more units */
    EXPECT_EQ(noErr, C_InsertMediaIntoTrack(t, -1, 60, 60, 0x10000));
    EXPECT_EQ(240, C_GetTrackDuration(t));
    EXPECT_EQ(240, C_GetMovieDuration(m));
    /* at the half rate the media's 60 units last 120 */
    EXPECT_EQ(noErr, C_InsertMediaIntoTrack(t, 0, 0, 60, 0x8000));
    EXPECT_EQ(360, C_GetTrackDuration(t));
    /* past the media's end: refused */
    EXPECT_NE(noErr, C_InsertMediaIntoTrack(t, 0, 120, 120, 0x10000));
    C_DisposeMovie(m);
}

TEST(Movies, TimeAndRate)
{
    Movie m = three_samples(nullptr, nullptr);
    EXPECT_EQ(0, C_GetMovieTime(m, nullptr));
    C_SetMovieTimeValue(m, 100);
    EXPECT_EQ(100, C_GetMovieTime(m, nullptr));
    EXPECT_FALSE(C_IsMovieDone(m));
    TimeRecord tr;
    EXPECT_EQ(100, C_GetMovieTime(m, &tr));
    EXPECT_EQ(600, tr.scale);
    EXPECT_EQ(100u, (uint32_t)tr.value.lo);
    EXPECT_EQ(C_GetMovieTimeBase(m), (TimeBase)tr.base);
    EXPECT_EQ(100 * 60 / 600, C_GetTimeBaseTime(C_GetMovieTimeBase(m), 60, nullptr));
    C_GoToEndOfMovie(m);
    EXPECT_EQ(180, C_GetMovieTime(m, nullptr));
    EXPECT_TRUE(C_IsMovieDone(m));
    /* playing from the end starts again at the beginning */
    C_StartMovie(m);
    EXPECT_EQ(0x10000, C_GetMovieRate(m));
    EXPECT_LT(C_GetMovieTime(m, nullptr), 180);
    C_StopMovie(m);
    EXPECT_EQ(0, C_GetMovieRate(m));
    TimeValue stopped = C_GetMovieTime(m, nullptr);
    EXPECT_EQ(stopped, C_GetMovieTime(m, nullptr));
    /* a looping time base is never done */
    C_SetTimeBaseFlags(C_GetMovieTimeBase(m), 1);
    C_GoToEndOfMovie(m);
    EXPECT_FALSE(C_IsMovieDone(m));
    C_DisposeMovie(m);
}

TEST(Movies, ThroughAHandle)
{
    Movie m = three_samples(nullptr, nullptr);
    Handle h = NewHandle(0);
    ASSERT_EQ(noErr, C_PutMovieIntoHandle(m, h));
    EXPECT_EQ("moov"_4, *(GUEST<uint32_t> *)(*h + 4));
    GUEST<Movie> back;
    ASSERT_EQ(noErr, C_NewMovieFromHandle(&back, h, 0, nullptr));
    Movie b = back;
    EXPECT_EQ(C_GetMovieDuration(m), C_GetMovieDuration(b));
    EXPECT_EQ(1, C_GetMovieTrackCount(b));
    Media md = C_GetTrackMedia(C_GetMovieIndTrack(b, 1));
    EXPECT_EQ(3, C_GetMediaSampleCount(md));
    EXPECT_EQ(180, C_GetMediaDuration(md));
    GUEST<LONGINT> n;
    GUEST<TimeValue> start, dur;
    C_MediaTimeToSampleNum(md, 130, &n, &start, &dur);
    EXPECT_EQ(3, n);
    Handle desc = NewHandle(0);
    C_GetMediaSampleDescription(md, 1, (SampleDescriptionHandle)desc);
    ASSERT_EQ((Size)sizeof(ImageDescription), GetHandleSize(desc));
    EXPECT_EQ(kRaw, (*(ImageDescriptionHandle)desc)->cType);
    EXPECT_EQ(80, (*(ImageDescriptionHandle)desc)->width);
    Rect box;
    C_GetMovieBox(b, &box);
    EXPECT_EQ(80, box.right);
    EXPECT_EQ(60, box.bottom);
    DisposeHandle(desc);
    DisposeHandle(h);
    C_DisposeMovie(b);
    C_DisposeMovie(m);
}
