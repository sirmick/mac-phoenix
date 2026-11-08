#pragma once

/* MacPhoenix: zlib streams (RFC 1950 around RFC 1951's deflate), for
   QuickTime 3's compressed movie headers ('cmov' with 'dcom' 'zlib');
   inflate.cpp, written from the RFCs. */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Executor
{
/* The zlib stream in [p, p + n) inflated into out; false when it is
   damaged, its Adler-32 is wrong, or it would inflate beyond limit
   bytes. */
bool ROMlib_zlib_inflate(const uint8_t *p, size_t n, std::vector<uint8_t> &out, size_t limit);
}
