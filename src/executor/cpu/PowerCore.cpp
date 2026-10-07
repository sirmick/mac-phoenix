#include "PowerCore.h"
#include <cstdio>
#include <cstdlib>

void PowerCore::execute()
{
    fprintf(stderr, "executor: PowerPC code is not supported yet (pc=%08x)\n", CIA);
    abort();
}
