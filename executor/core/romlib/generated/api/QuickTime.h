/*****************************************************************************
 *                                                                           *
 *                                QuickTime.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h            LONGINT, INTEGER, OSErr, Fixed, Boolean,        *
 *                           StringPtr, Rect, OSType, Handle, ULONGINT,      *
 *                           ConstStr255Param                                *
 *     FileMgr.h             wide, FSSpec                                    *
 *     QuickDraw.h           CGrafPtr, RgnHandle, CTabHandle                 *
 *     CQuickDraw.h          GDHandle                                        *
 *     Components.h          ComponentInstance, ComponentResult              *
 *     ImageCompression.h    MatrixRecord                                    *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "FileMgr.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "Components.h"
#include "ImageCompression.h"

#define MODULE_NAME QuickTime
#include <base/api-module.h>


namespace Executor {

struct MovieRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(MovieRecord) == 4);void describeStruct(const MovieRecord&, std::ostream&);

typedef MovieRecord *Movie;

typedef LONGINT TimeValue;

typedef LONGINT TimeScale;

struct TrackRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(TrackRecord) == 4);void describeStruct(const TrackRecord&, std::ostream&);

typedef TrackRecord *Track;

struct MediaRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(MediaRecord) == 4);void describeStruct(const MediaRecord&, std::ostream&);

typedef MediaRecord *Media;

struct TimeBaseRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(TimeBaseRecord) == 4);void describeStruct(const TimeBaseRecord&, std::ostream&);

typedef TimeBaseRecord *TimeBase;

struct UserDataRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(UserDataRecord) == 4);void describeStruct(const UserDataRecord&, std::ostream&);

typedef UserDataRecord *UserData;

struct TimeRecord{GUEST_STRUCT;wide value;GUEST<TimeScale> scale;GUEST<TimeBase> base;};static_assert(sizeof(TimeRecord) == 16);void describeStruct(const TimeRecord&, std::ostream&);

struct SampleDescription{GUEST_STRUCT;GUEST<LONGINT> descSize;GUEST<LONGINT> dataFormat;GUEST<LONGINT> resvd1;GUEST<INTEGER> resvd2;GUEST<INTEGER> dataRefIndex;};static_assert(sizeof(SampleDescription) == 16);void describeStruct(const SampleDescription&, std::ostream&);

typedef SampleDescription *SampleDescriptionPtr;

typedef GUEST<SampleDescription*> *SampleDescriptionHandle;

DISPATCHER_TRAP(QuickTime, 0xAAAA, D0<0x7FFF>);


OSErr C_EnterMovies();PASCAL_SUBTRAP(EnterMovies, 0xAAAA, 0x0001, QuickTime);


void C_ExitMovies();PASCAL_SUBTRAP(ExitMovies, 0xAAAA, 0x0002, QuickTime);


void C_MoviesTask(Movie movie, LONGINT maxmillisecs);PASCAL_SUBTRAP(MoviesTask, 0xAAAA, 0x0005, QuickTime);


OSErr C_PrerollMovie(Movie movie, TimeValue time, Fixed rate);PASCAL_SUBTRAP(PrerollMovie, 0xAAAA, 0x0006, QuickTime);


void C_SetMovieActive(Movie movie, Boolean active);PASCAL_SUBTRAP(SetMovieActive, 0xAAAA, 0x0009, QuickTime);


void C_StartMovie(Movie movie);PASCAL_SUBTRAP(StartMovie, 0xAAAA, 0x000B, QuickTime);


void C_StopMovie(Movie movie);PASCAL_SUBTRAP(StopMovie, 0xAAAA, 0x000C, QuickTime);


void C_GoToBeginningOfMovie(Movie movie);PASCAL_SUBTRAP(GoToBeginningOfMovie, 0xAAAA, 0x000D, QuickTime);


void C_SetMovieGWorld(Movie movie, CGrafPtr cgrafp, GDHandle gdh);PASCAL_SUBTRAP(SetMovieGWorld, 0xAAAA, 0x0016, QuickTime);


OSErr C_UpdateMovie(Movie movie);PASCAL_SUBTRAP(UpdateMovie, 0xAAAA, 0x001F, QuickTime);


void C_DisposeMovie(Movie movie);PASCAL_SUBTRAP(DisposeMovie, 0xAAAA, 0x0023, QuickTime);


INTEGER C_GetMovieVolume(Movie movie);PASCAL_SUBTRAP(GetMovieVolume, 0xAAAA, 0x002E, QuickTime);


OSErr C_CloseMovieFile(INTEGER refnum);PASCAL_SUBTRAP(CloseMovieFile, 0xAAAA, 0x00D5, QuickTime);


Boolean C_IsMovieDone(Movie movie);PASCAL_SUBTRAP(IsMovieDone, 0xAAAA, 0x00DD, QuickTime);


OSErr C_NewMovieFromFile(GUEST<Movie> *moviep, INTEGER refnum, GUEST<INTEGER> *residp, StringPtr resnamep, INTEGER flags, Boolean *datarefwaschangedp);PASCAL_SUBTRAP(NewMovieFromFile, 0xAAAA, 0x00F0, QuickTime);


Fixed C_GetMoviePreferredRate(Movie movie);PASCAL_SUBTRAP(GetMoviePreferredRate, 0xAAAA, 0x00F3, QuickTime);


void C_GetMovieBox(Movie movie, Rect *boxp);PASCAL_SUBTRAP(GetMovieBox, 0xAAAA, 0x00F9, QuickTime);


void C_SetMovieBox(Movie movie, const Rect *boxp);PASCAL_SUBTRAP(SetMovieBox, 0xAAAA, 0x00FA, QuickTime);


ComponentInstance C_NewMovieController(Movie movie, const Rect *mrectp, LONGINT flags);PASCAL_SUBTRAP(NewMovieController, 0xAAAA, 0x018A, QuickTime);


void C_DisposeMovieController(ComponentInstance cntrller);PASCAL_SUBTRAP(DisposeMovieController, 0xAAAA, 0x018B, QuickTime);


OSErr C_OpenMovieFile(const FSSpec *filespecp, GUEST<INTEGER> *refnump, uint8_t perm);PASCAL_SUBTRAP(OpenMovieFile, 0xAAAA, 0x0192, QuickTime);


OSErr C_GetMoviesError();PASCAL_SUBTRAP(GetMoviesError, 0xAAAA, 0x0003, QuickTime);


void C_ClearMoviesStickyError();PASCAL_SUBTRAP(ClearMoviesStickyError, 0xAAAA, 0x00DE, QuickTime);


OSErr C_GetMoviesStickyError();PASCAL_SUBTRAP(GetMoviesStickyError, 0xAAAA, 0x0004, QuickTime);


OSErr C_LoadMovieIntoRam(Movie theMovie, TimeValue time, TimeValue duration, LONGINT flags);PASCAL_SUBTRAP(LoadMovieIntoRam, 0xAAAA, 0x0007, QuickTime);


OSErr C_LoadTrackIntoRam(Track theTrack, TimeValue time, TimeValue duration, LONGINT flags);PASCAL_SUBTRAP(LoadTrackIntoRam, 0xAAAA, 0x016E, QuickTime);


Boolean C_GetMovieActive(Movie theMovie);PASCAL_SUBTRAP(GetMovieActive, 0xAAAA, 0x000A, QuickTime);


void C_GoToEndOfMovie(Movie theMovie);PASCAL_SUBTRAP(GoToEndOfMovie, 0xAAAA, 0x000E, QuickTime);


void C_GetMovieGWorld(Movie theMovie, GUEST<CGrafPtr> *port, GUEST<GDHandle> *gdh);PASCAL_SUBTRAP(GetMovieGWorld, 0xAAAA, 0x0015, QuickTime);


void C_GetMovieMatrix(Movie theMovie, MatrixRecord *matrix);PASCAL_SUBTRAP(GetMovieMatrix, 0xAAAA, 0x0031, QuickTime);


void C_SetMovieMatrix(Movie theMovie, const MatrixRecord *matrix);PASCAL_SUBTRAP(SetMovieMatrix, 0xAAAA, 0x0032, QuickTime);


RgnHandle C_GetMovieDisplayBoundsRgn(Movie theMovie);PASCAL_SUBTRAP(GetMovieDisplayBoundsRgn, 0xAAAA, 0x00FB, QuickTime);


RgnHandle C_GetMovieBoundsRgn(Movie theMovie);PASCAL_SUBTRAP(GetMovieBoundsRgn, 0xAAAA, 0x00FE, QuickTime);


RgnHandle C_GetMovieClipRgn(Movie theMovie);PASCAL_SUBTRAP(GetMovieClipRgn, 0xAAAA, 0x0100, QuickTime);


void C_SetMovieClipRgn(Movie theMovie, RgnHandle theClip);PASCAL_SUBTRAP(SetMovieClipRgn, 0xAAAA, 0x0101, QuickTime);


RgnHandle C_GetMovieDisplayClipRgn(Movie theMovie);PASCAL_SUBTRAP(GetMovieDisplayClipRgn, 0xAAAA, 0x00FC, QuickTime);


void C_SetMovieDisplayClipRgn(Movie theMovie, RgnHandle theClip);PASCAL_SUBTRAP(SetMovieDisplayClipRgn, 0xAAAA, 0x00FD, QuickTime);


Boolean C_PtInMovie(Movie theMovie, Point pt);PASCAL_SUBTRAP(PtInMovie, 0xAAAA, 0x009A, QuickTime);


OSErr C_GetMovieColorTable(Movie theMovie, GUEST<CTabHandle> *ctab);PASCAL_SUBTRAP(GetMovieColorTable, 0xAAAA, 0x0206, QuickTime);


OSErr C_SetMovieColorTable(Movie theMovie, CTabHandle ctab);PASCAL_SUBTRAP(SetMovieColorTable, 0xAAAA, 0x0205, QuickTime);


TimeBase C_GetMovieTimeBase(Movie theMovie);PASCAL_SUBTRAP(GetMovieTimeBase, 0xAAAA, 0x0012, QuickTime);


TimeScale C_GetMovieTimeScale(Movie theMovie);PASCAL_SUBTRAP(GetMovieTimeScale, 0xAAAA, 0x0029, QuickTime);


void C_SetMovieTimeScale(Movie theMovie, TimeScale timeScale);PASCAL_SUBTRAP(SetMovieTimeScale, 0xAAAA, 0x002A, QuickTime);


TimeValue C_GetMovieDuration(Movie theMovie);PASCAL_SUBTRAP(GetMovieDuration, 0xAAAA, 0x002B, QuickTime);


Fixed C_GetMovieRate(Movie theMovie);PASCAL_SUBTRAP(GetMovieRate, 0xAAAA, 0x002C, QuickTime);


void C_SetMovieRate(Movie theMovie, Fixed rate);PASCAL_SUBTRAP(SetMovieRate, 0xAAAA, 0x002D, QuickTime);


void C_SetMoviePreferredRate(Movie theMovie, Fixed rate);PASCAL_SUBTRAP(SetMoviePreferredRate, 0xAAAA, 0x00F4, QuickTime);


TimeValue C_GetMovieTime(Movie theMovie, TimeRecord *currentTime);PASCAL_SUBTRAP(GetMovieTime, 0xAAAA, 0x0039, QuickTime);


void C_SetMovieTime(Movie theMovie, const TimeRecord *newtime);PASCAL_SUBTRAP(SetMovieTime, 0xAAAA, 0x003C, QuickTime);


void C_SetMovieTimeValue(Movie theMovie, TimeValue newtime);PASCAL_SUBTRAP(SetMovieTimeValue, 0xAAAA, 0x003D, QuickTime);


void C_SetMovieVolume(Movie theMovie, INTEGER volume);PASCAL_SUBTRAP(SetMovieVolume, 0xAAAA, 0x002F, QuickTime);


INTEGER C_GetMoviePreferredVolume(Movie theMovie);PASCAL_SUBTRAP(GetMoviePreferredVolume, 0xAAAA, 0x00F5, QuickTime);


void C_SetMoviePreferredVolume(Movie theMovie, INTEGER volume);PASCAL_SUBTRAP(SetMoviePreferredVolume, 0xAAAA, 0x00F6, QuickTime);


void C_SetMoviePlayHints(Movie theMovie, LONGINT flags, LONGINT flagsMask);PASCAL_SUBTRAP(SetMoviePlayHints, 0xAAAA, 0x01A1, QuickTime);


TimeValue C_GetMoviePosterTime(Movie theMovie);PASCAL_SUBTRAP(GetMoviePosterTime, 0xAAAA, 0x0035, QuickTime);


void C_SetMoviePosterTime(Movie theMovie, TimeValue posterTime);PASCAL_SUBTRAP(SetMoviePosterTime, 0xAAAA, 0x0036, QuickTime);


void C_GetMovieSelection(Movie theMovie, GUEST<TimeValue> *selectionTime, GUEST<TimeValue> *selectionDuration);PASCAL_SUBTRAP(GetMovieSelection, 0xAAAA, 0x0037, QuickTime);


void C_SetMovieSelection(Movie theMovie, TimeValue selectionTime, TimeValue selectionDuration);PASCAL_SUBTRAP(SetMovieSelection, 0xAAAA, 0x0038, QuickTime);


void C_GetMovieActiveSegment(Movie theMovie, GUEST<TimeValue> *startTime, GUEST<TimeValue> *duration);PASCAL_SUBTRAP(GetMovieActiveSegment, 0xAAAA, 0x015D, QuickTime);


void C_SetMovieActiveSegment(Movie theMovie, TimeValue startTime, TimeValue duration);PASCAL_SUBTRAP(SetMovieActiveSegment, 0xAAAA, 0x015C, QuickTime);


void C_GetMovieNextInterestingTime(Movie theMovie, INTEGER interestingTimeFlags, INTEGER numMediaTypes, const GUEST<OSType> *whichMediaTypes, TimeValue time, Fixed rate, GUEST<TimeValue> *interestingTime, GUEST<TimeValue> *interestingDuration);PASCAL_SUBTRAP(GetMovieNextInterestingTime, 0xAAAA, 0x010E, QuickTime);


TimeValue C_GetTimeBaseTime(TimeBase tb, TimeScale s, TimeRecord *tr);PASCAL_SUBTRAP(GetTimeBaseTime, 0xAAAA, 0x00A6, QuickTime);


void C_SetTimeBaseTime(TimeBase tb, const TimeRecord *tr);PASCAL_SUBTRAP(SetTimeBaseTime, 0xAAAA, 0x00A7, QuickTime);


void C_SetTimeBaseValue(TimeBase tb, TimeValue t, TimeScale s);PASCAL_SUBTRAP(SetTimeBaseValue, 0xAAAA, 0x00A8, QuickTime);


Fixed C_GetTimeBaseRate(TimeBase tb);PASCAL_SUBTRAP(GetTimeBaseRate, 0xAAAA, 0x00A9, QuickTime);


void C_SetTimeBaseRate(TimeBase tb, Fixed r);PASCAL_SUBTRAP(SetTimeBaseRate, 0xAAAA, 0x00AA, QuickTime);


LONGINT C_GetTimeBaseFlags(TimeBase tb);PASCAL_SUBTRAP(GetTimeBaseFlags, 0xAAAA, 0x00B1, QuickTime);


void C_SetTimeBaseFlags(TimeBase tb, LONGINT timeBaseFlags);PASCAL_SUBTRAP(SetTimeBaseFlags, 0xAAAA, 0x00B2, QuickTime);


TimeValue C_GetTimeBaseStartTime(TimeBase tb, TimeScale s, TimeRecord *tr);PASCAL_SUBTRAP(GetTimeBaseStartTime, 0xAAAA, 0x00AB, QuickTime);


TimeValue C_GetTimeBaseStopTime(TimeBase tb, TimeScale s, TimeRecord *tr);PASCAL_SUBTRAP(GetTimeBaseStopTime, 0xAAAA, 0x00AD, QuickTime);


void C_SetTimeBaseStartTime(TimeBase tb, const TimeRecord *tr);PASCAL_SUBTRAP(SetTimeBaseStartTime, 0xAAAA, 0x00AC, QuickTime);


void C_SetTimeBaseStopTime(TimeBase tb, const TimeRecord *tr);PASCAL_SUBTRAP(SetTimeBaseStopTime, 0xAAAA, 0x00AE, QuickTime);


LONGINT C_GetMovieTrackCount(Movie theMovie);PASCAL_SUBTRAP(GetMovieTrackCount, 0xAAAA, 0x003F, QuickTime);


Track C_GetMovieTrack(Movie theMovie, LONGINT trackID);PASCAL_SUBTRAP(GetMovieTrack, 0xAAAA, 0x0040, QuickTime);


Track C_GetMovieIndTrack(Movie theMovie, LONGINT index);PASCAL_SUBTRAP(GetMovieIndTrack, 0xAAAA, 0x0117, QuickTime);


Track C_GetMovieIndTrackType(Movie theMovie, LONGINT index, OSType trackType, LONGINT flags);PASCAL_SUBTRAP(GetMovieIndTrackType, 0xAAAA, 0x0208, QuickTime);


LONGINT C_GetTrackID(Track theTrack);PASCAL_SUBTRAP(GetTrackID, 0xAAAA, 0x0127, QuickTime);


Movie C_GetTrackMovie(Track theTrack);PASCAL_SUBTRAP(GetTrackMovie, 0xAAAA, 0x00D0, QuickTime);


Media C_GetTrackMedia(Track theTrack);PASCAL_SUBTRAP(GetTrackMedia, 0xAAAA, 0x0062, QuickTime);


Boolean C_GetTrackEnabled(Track theTrack);PASCAL_SUBTRAP(GetTrackEnabled, 0xAAAA, 0x0045, QuickTime);


void C_SetTrackEnabled(Track theTrack, Boolean isEnabled);PASCAL_SUBTRAP(SetTrackEnabled, 0xAAAA, 0x0046, QuickTime);


TimeValue C_GetTrackDuration(Track theTrack);PASCAL_SUBTRAP(GetTrackDuration, 0xAAAA, 0x004B, QuickTime);


TimeValue C_GetTrackOffset(Track theTrack);PASCAL_SUBTRAP(GetTrackOffset, 0xAAAA, 0x004C, QuickTime);


void C_GetTrackDimensions(Track theTrack, GUEST<Fixed> *width, GUEST<Fixed> *height);PASCAL_SUBTRAP(GetTrackDimensions, 0xAAAA, 0x005D, QuickTime);


void C_SetTrackDimensions(Track theTrack, Fixed width, Fixed height);PASCAL_SUBTRAP(SetTrackDimensions, 0xAAAA, 0x005E, QuickTime);


void C_GetTrackMatrix(Track theTrack, MatrixRecord *matrix);PASCAL_SUBTRAP(GetTrackMatrix, 0xAAAA, 0x0056, QuickTime);


void C_SetTrackMatrix(Track theTrack, const MatrixRecord *matrix);PASCAL_SUBTRAP(SetTrackMatrix, 0xAAAA, 0x0057, QuickTime);


INTEGER C_GetTrackLayer(Track theTrack);PASCAL_SUBTRAP(GetTrackLayer, 0xAAAA, 0x0050, QuickTime);


void C_SetTrackLayer(Track theTrack, INTEGER layer);PASCAL_SUBTRAP(SetTrackLayer, 0xAAAA, 0x0051, QuickTime);


Track C_GetTrackAlternate(Track theTrack);PASCAL_SUBTRAP(GetTrackAlternate, 0xAAAA, 0x0052, QuickTime);


void C_SetTrackAlternate(Track theTrack, Track alternateT);PASCAL_SUBTRAP(SetTrackAlternate, 0xAAAA, 0x0053, QuickTime);


INTEGER C_GetTrackVolume(Track theTrack);PASCAL_SUBTRAP(GetTrackVolume, 0xAAAA, 0x0054, QuickTime);


void C_SetTrackVolume(Track theTrack, INTEGER volume);PASCAL_SUBTRAP(SetTrackVolume, 0xAAAA, 0x0055, QuickTime);


UserData C_GetTrackUserData(Track theTrack);PASCAL_SUBTRAP(GetTrackUserData, 0xAAAA, 0x005F, QuickTime);


LONGINT C_GetTrackUsage(Track theTrack);PASCAL_SUBTRAP(GetTrackUsage, 0xAAAA, 0x0047, QuickTime);


void C_SetTrackUsage(Track theTrack, LONGINT usage);PASCAL_SUBTRAP(SetTrackUsage, 0xAAAA, 0x0048, QuickTime);


Track C_NewMovieTrack(Movie theMovie, Fixed width, Fixed height, INTEGER trackVolume);PASCAL_SUBTRAP(NewMovieTrack, 0xAAAA, 0x0188, QuickTime);


void C_DisposeMovieTrack(Track theTrack);PASCAL_SUBTRAP(DisposeMovieTrack, 0xAAAA, 0x0042, QuickTime);


Track C_GetMediaTrack(Media theMedia);PASCAL_SUBTRAP(GetMediaTrack, 0xAAAA, 0x00C5, QuickTime);


TimeScale C_GetMediaTimeScale(Media theMedia);PASCAL_SUBTRAP(GetMediaTimeScale, 0xAAAA, 0x0068, QuickTime);


void C_SetMediaTimeScale(Media theMedia, TimeScale timeScale);PASCAL_SUBTRAP(SetMediaTimeScale, 0xAAAA, 0x0069, QuickTime);


TimeValue C_GetMediaDuration(Media theMedia);PASCAL_SUBTRAP(GetMediaDuration, 0xAAAA, 0x006A, QuickTime);


void C_GetMediaHandlerDescription(Media theMedia, GUEST<OSType> *mediaType, StringPtr creatorName, GUEST<OSType> *creatorManufacturer);PASCAL_SUBTRAP(GetMediaHandlerDescription, 0xAAAA, 0x006F, QuickTime);


LONGINT C_GetMediaSampleCount(Media theMedia);PASCAL_SUBTRAP(GetMediaSampleCount, 0xAAAA, 0x0079, QuickTime);


LONGINT C_GetMediaSampleDescriptionCount(Media theMedia);PASCAL_SUBTRAP(GetMediaSampleDescriptionCount, 0xAAAA, 0x0077, QuickTime);


void C_GetMediaSampleDescription(Media theMedia, LONGINT index, SampleDescriptionHandle descH);PASCAL_SUBTRAP(GetMediaSampleDescription, 0xAAAA, 0x0078, QuickTime);


INTEGER C_GetMediaLanguage(Media theMedia);PASCAL_SUBTRAP(GetMediaLanguage, 0xAAAA, 0x006B, QuickTime);


INTEGER C_GetMediaQuality(Media theMedia);PASCAL_SUBTRAP(GetMediaQuality, 0xAAAA, 0x006D, QuickTime);


Media C_NewTrackMedia(Track theTrack, OSType mediaType, TimeScale timeScale, Handle dataRef, OSType dataRefType);PASCAL_SUBTRAP(NewTrackMedia, 0xAAAA, 0x018E, QuickTime);


void C_DisposeTrackMedia(Media theMedia);PASCAL_SUBTRAP(DisposeTrackMedia, 0xAAAA, 0x0061, QuickTime);


OSErr C_BeginMediaEdits(Media theMedia);PASCAL_SUBTRAP(BeginMediaEdits, 0xAAAA, 0x0072, QuickTime);


OSErr C_EndMediaEdits(Media theMedia);PASCAL_SUBTRAP(EndMediaEdits, 0xAAAA, 0x0073, QuickTime);


OSErr C_AddMediaSample(Media theMedia, Handle dataIn, LONGINT inOffset, ULONGINT size, TimeValue durationPerSample, SampleDescriptionHandle sampleDescriptionH, LONGINT numberOfSamples, INTEGER sampleFlags, GUEST<TimeValue> *sampleTime);PASCAL_SUBTRAP(AddMediaSample, 0xAAAA, 0x007C, QuickTime);


OSErr C_InsertMediaIntoTrack(Track theTrack, TimeValue trackStart, TimeValue mediaTime, TimeValue mediaDuration, Fixed mediaRate);PASCAL_SUBTRAP(InsertMediaIntoTrack, 0xAAAA, 0x0183, QuickTime);


void C_MediaTimeToSampleNum(Media theMedia, TimeValue time, GUEST<LONGINT> *sampleNum, GUEST<TimeValue> *sampleTime, GUEST<TimeValue> *sampleDuration);PASCAL_SUBTRAP(MediaTimeToSampleNum, 0xAAAA, 0x007B, QuickTime);


void C_SampleNumToMediaTime(Media theMedia, LONGINT logicalSampleNum, GUEST<TimeValue> *sampleTime, GUEST<TimeValue> *sampleDuration);PASCAL_SUBTRAP(SampleNumToMediaTime, 0xAAAA, 0x007A, QuickTime);


OSErr C_GetMediaSample(Media theMedia, Handle dataOut, LONGINT maxSizeToGrow, GUEST<LONGINT> *size, TimeValue time, GUEST<TimeValue> *sampleTime, GUEST<TimeValue> *durationPerSample, SampleDescriptionHandle sampleDescriptionH, GUEST<LONGINT> *sampleDescriptionIndex, LONGINT maxNumberOfSamples, GUEST<LONGINT> *numberOfSamples, GUEST<INTEGER> *sampleFlags);PASCAL_SUBTRAP(GetMediaSample, 0xAAAA, 0x007E, QuickTime);


OSErr C_NewMovieFromHandle(GUEST<Movie> *theMovie, Handle h, INTEGER newMovieFlags, Boolean *dataRefWasChanged);PASCAL_SUBTRAP(NewMovieFromHandle, 0xAAAA, 0x00F1, QuickTime);


OSErr C_NewMovieFromDataFork(GUEST<Movie> *theMovie, INTEGER fRefNum, LONGINT fileOffset, INTEGER newMovieFlags, Boolean *dataRefWasChanged);PASCAL_SUBTRAP(NewMovieFromDataFork, 0xAAAA, 0x01B3, QuickTime);


Movie C_NewMovie(LONGINT flags);PASCAL_SUBTRAP(NewMovie, 0xAAAA, 0x0187, QuickTime);


OSErr C_CreateMovieFile(const FSSpec *fileSpec, OSType creator, INTEGER scriptTag, LONGINT createMovieFileFlags, GUEST<INTEGER> *resRefNum, GUEST<Movie> *newmovie);PASCAL_SUBTRAP(CreateMovieFile, 0xAAAA, 0x0191, QuickTime);


OSErr C_DeleteMovieFile(const FSSpec *fileSpec);PASCAL_SUBTRAP(DeleteMovieFile, 0xAAAA, 0x0175, QuickTime);


OSErr C_AddMovieResource(Movie theMovie, INTEGER resRefNum, GUEST<INTEGER> *resId, ConstStr255Param resName);PASCAL_SUBTRAP(AddMovieResource, 0xAAAA, 0x00D7, QuickTime);


OSErr C_UpdateMovieResource(Movie theMovie, INTEGER resRefNum, INTEGER resId, ConstStr255Param resName);PASCAL_SUBTRAP(UpdateMovieResource, 0xAAAA, 0x00D8, QuickTime);


void C_FlattenMovie(Movie theMovie, LONGINT movieFlattenFlags, const FSSpec *theFile, OSType creator, INTEGER scriptTag, LONGINT createMovieFileFlags, GUEST<INTEGER> *resId, ConstStr255Param resName);PASCAL_SUBTRAP(FlattenMovie, 0xAAAA, 0x019B, QuickTime);


Movie C_FlattenMovieData(Movie theMovie, LONGINT movieFlattenFlags, const FSSpec *theFile, OSType creator, INTEGER scriptTag, LONGINT createMovieFileFlags);PASCAL_SUBTRAP(FlattenMovieData, 0xAAAA, 0x019C, QuickTime);


OSErr C_PutMovieIntoHandle(Movie theMovie, Handle publicMovie);PASCAL_SUBTRAP(PutMovieIntoHandle, 0xAAAA, 0x0022, QuickTime);


UserData C_GetMovieUserData(Movie theMovie);PASCAL_SUBTRAP(GetMovieUserData, 0xAAAA, 0x003E, QuickTime);


OSErr C_GetUserData(UserData theUserData, Handle data, OSType udType, LONGINT index);PASCAL_SUBTRAP(GetUserData, 0xAAAA, 0x009E, QuickTime);


INTEGER C_CountUserDataType(UserData theUserData, OSType udType);PASCAL_SUBTRAP(CountUserDataType, 0xAAAA, 0x014B, QuickTime);


LONGINT C_GetNextUserDataType(UserData theUserData, OSType udType);PASCAL_SUBTRAP(GetNextUserDataType, 0xAAAA, 0x01A5, QuickTime);


OSErr C_GetUserDataText(UserData theUserData, Handle data, OSType udType, LONGINT index, INTEGER itlRegionTag);PASCAL_SUBTRAP(GetUserDataText, 0xAAAA, 0x014D, QuickTime);


ULONGINT C_GetMovieCreationTime(Movie theMovie);PASCAL_SUBTRAP(GetMovieCreationTime, 0xAAAA, 0x0026, QuickTime);


ULONGINT C_GetMovieModificationTime(Movie theMovie);PASCAL_SUBTRAP(GetMovieModificationTime, 0xAAAA, 0x0027, QuickTime);


ComponentResult C_GetMovieStatus(Movie theMovie, GUEST<Track> *firstProblemTrack);PASCAL_SUBTRAP(GetMovieStatus, 0xAAAA, 0x0173, QuickTime);


ComponentResult C_GetTrackStatus(Track theTrack);PASCAL_SUBTRAP(GetTrackStatus, 0xAAAA, 0x0172, QuickTime);


} /* namespace Executor */