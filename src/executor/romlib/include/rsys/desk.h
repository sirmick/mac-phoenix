
#pragma once

#include <api/ExMacTypes.h>

namespace Executor
{
enum
{
    DESK_ACC_MIN = 12,
    DESK_ACC_MAX = 31
};

struct AppleMenuEntry
{
    Str31 name;
    void (*function)();
    
    AppleMenuEntry(const char32_t* n, void (*f)());
};

const std::vector<AppleMenuEntry>& appleMenuEntries();
}
