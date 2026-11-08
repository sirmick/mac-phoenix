/* Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <QuickTime.h>
#include <rsys/quicktime.h>

using namespace Executor;

/* MacPhoenix: Executor's QuickTime (route B, docs/executor/QuickTime.md).
   The Image Compression Manager (icm.cpp: Gestalt 'icmp') and the Movie
   Toolbox (movies.cpp: 'qtim') are there, and the movie controller
   (mc.cpp: the 'play' component). */
bool Executor::ROMlib_icm_present()
{
    return true;
}

bool Executor::ROMlib_movie_toolbox_present()
{
    return true;
}
