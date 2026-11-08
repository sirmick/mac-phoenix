/* Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <QuickTime.h>
#include <rsys/quicktime.h>

using namespace Executor;

/* MacPhoenix: Executor's QuickTime (route B, docs/executor/QuickTime.md)
   isn't there yet: Gestalt says so ('qtim', 'icmp' unanswered), and the
   Movie Toolbox's 1998 stubs below no longer pretend to work. */
bool Executor::ROMlib_icm_present()
{
    return false;
}

bool Executor::ROMlib_movie_toolbox_present()
{
    return false;
}

/* MacPhoenix: what EnterMovies and NewMovieFromFile answer while there is
   no Movie Toolbox: an application that calls them although Gestalt has
   no 'qtim' stops there instead of using a movie that isn't. */
enum
{
    unimpErr = -4,
};

OSErr Executor::C_EnterMovies()
{
    warning_unimplemented("");
    return ROMlib_movie_toolbox_present() ? noErr : unimpErr;
}

void Executor::C_ExitMovies()
{
    warning_unimplemented("");
}

void Executor::C_MoviesTask(Movie movie, LONGINT maxmillisecs)
{
    warning_unimplemented("");
}

OSErr Executor::C_PrerollMovie(Movie movie, TimeValue time, Fixed rate)
{
    OSErr retval;

    warning_unimplemented("");
    retval = noErr;

    return retval;
}

void Executor::C_SetMovieActive(Movie movie, Boolean active)
{
    warning_unimplemented("");
}

void Executor::C_StartMovie(Movie movie)
{
    warning_unimplemented("");
}

void Executor::C_StopMovie(Movie movie)
{
    warning_unimplemented("");
}

void Executor::C_GoToBeginningOfMovie(Movie movie)
{
    warning_unimplemented("");
}

void Executor::C_SetMovieGWorld(Movie movie, CGrafPtr cgrafp, GDHandle gdh)
{
    warning_unimplemented("");
}

OSErr Executor::C_UpdateMovie(Movie movie)
{
    OSErr retval;

    warning_unimplemented("");
    retval = noErr;

    return retval;
}

void Executor::C_DisposeMovie(Movie movie)
{
    warning_unimplemented("");
}

INTEGER Executor::C_GetMovieVolume(Movie movie)
{
    INTEGER retval;

    warning_unimplemented("");
    retval = 1;

    return retval;
}

OSErr Executor::C_CloseMovieFile(INTEGER refnum)
{
    OSErr retval;

    warning_unimplemented("");
    retval = noErr;

    return retval;
}

Boolean Executor::C_IsMovieDone(Movie movie)
{
    Boolean retval;

    warning_unimplemented("");
    retval = true;

    return retval;
}

OSErr Executor::C_NewMovieFromFile(GUEST<Movie> *moviep, INTEGER refnum,
                                   GUEST<INTEGER> *residp, StringPtr resnamep,
                                   INTEGER flags, Boolean *datarefwaschangedp)
{
    warning_unimplemented("");
    if(moviep)
        *moviep = nullptr;
    return unimpErr;
}

Fixed Executor::C_GetMoviePreferredRate(Movie movie)
{
    Fixed retval;

    warning_unimplemented("");
    retval = 0;

    return retval;
}

void Executor::C_GetMovieBox(Movie movie, Rect *boxp)
{
    warning_unimplemented("");
}

void Executor::C_SetMovieBox(Movie movie, const Rect *boxp)
{
    warning_unimplemented("");
}

ComponentInstance Executor::C_NewMovieController(
    Movie movie, const Rect *movierectp, LONGINT flags)
{
    ComponentInstance retval;

    warning_unimplemented("");
    retval = 0;

    return retval;
}

void Executor::C_DisposeMovieController(ComponentInstance controller)
{
    warning_unimplemented("");
}

OSErr Executor::C_OpenMovieFile(const FSSpec *filespecp, GUEST<INTEGER> *refnump,
                                uint8_t perm)
{
    OSErr retval;

    warning_unimplemented("");
    retval = fnfErr;

    return retval;
}
