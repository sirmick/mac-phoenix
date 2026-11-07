#pragma once

#if defined(_WIN32) && !defined(WIN32)
#define WIN32 /* evil hackage needed to make SDL happy */
#endif

#include <rsys/macros.h>
#include <base/functions.h>
#include <base/traps.h>
#include <base/mactype.h>

#include <ExMacTypes.h>
#include <error/error.h>
#include <base/lowglobals.h>
