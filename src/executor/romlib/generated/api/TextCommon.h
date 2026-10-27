/*****************************************************************************
 *                                                                           *
 *                                TextCommon.h                               *
 *                                ============                               *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>

#define MODULE_NAME TextCommon
#include <base/api-module.h>


namespace Executor {

enum {kTextEncodingMacRoman = 0,
kTextEncodingUnknown = 65535,
};

enum {kTextEncodingDefaultVariant = 0,
};

enum {kTextEncodingDefaultFormat = 0,
};

} /* namespace Executor */