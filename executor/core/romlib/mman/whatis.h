#pragma once

#include <cstddef>
#include <cstdint>

namespace Executor
{
/* What is at a guest address: zone, block, resource (whatis.cpp). */
void ROMlib_whatis(uint32_t guest, char *buf, size_t n);
}
