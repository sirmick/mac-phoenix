/*****************************************************************************
 *                                                                           *
 *                                QuickTime.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h      LONGINT, OSErr, Fixed, Boolean, INTEGER,              *
 *                     StringPtr, Rect                                       *
 *     QuickDraw.h     CGrafPtr                                              *
 *     CQuickDraw.h    GDHandle                                              *
 *     Components.h    ComponentInstance                                     *
 *     FileMgr.h       FSSpec                                                *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "Components.h"
#include "FileMgr.h"

#define MODULE_NAME QuickTime
#include <base/api-module.h>


namespace Executor {

struct MovieRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(MovieRecord) == 4);void describeStruct(const MovieRecord&, std::ostream&);

typedef MovieRecord *Movie;

typedef LONGINT TimeValue;

DISPATCHER_TRAP(QuickTime, 0xAAAA, D0W);


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


} /* namespace Executor */