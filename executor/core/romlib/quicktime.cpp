/* Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <QuickTime.h>
#include <rsys/quicktime.h>

using namespace Executor;

/* MacPhoenix: Executor's QuickTime (route B, docs/executor/QuickTime.md).
   The Image Compression Manager (icm.cpp: Gestalt 'icmp') and the Movie
   Toolbox (movies.cpp: 'qtim') are there; the movie controller isn't yet,
   its 1998 stubs below answer no controller. */
bool Executor::ROMlib_icm_present()
{
    return true;
}

bool Executor::ROMlib_movie_toolbox_present()
{
    return true;
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
