#include "gtest/gtest.h"

#include "compat.h"

#include <MemoryMgr.h>
#include <clib/clib.h>
#include <string.h>

/* StdCLib's stdio (romlib/clib/stdio.cpp), Executor's own: the stream
   routines the getc/putc macros fall back on. */
namespace Executor
{
int filled(uint32_t f, int32_t n);
void *data_iob();
}

using namespace Executor;

namespace
{
/* A FILE record (_cnt, _ptr, _base, _end, _size, _flag, _file) in _iob. */
uint32_t stream(int i) { return guest_cast<uint32_t>((Ptr)data_iob()) + 24 * i; }
GUEST<uint32_t> &field(uint32_t f, int off) { return *ptr_from_longint<GUEST<uint32_t> *>(f + off); }
}

/* A device's read may run the application: MPW Shell's console (ECON)
   reads its window and takes events, and quitting meanwhile closes stdin.
   The read's end finds the stream closed, its record cleared (_base 0):
   EOF, the record left so (it read the byte at host(0) and crashed). */
TEST(StdCLib, StreamClosedDuringItsRead)
{
    uint32_t f = stream(37);
    memset(ptr_from_longint<Ptr>(f), 0, 24);
    EXPECT_EQ(-1, filled(f, 5));
    for(int off = 0; off < 24; off += 4)
        EXPECT_EQ(0u, (uint32_t)field(f, off)) << "offset " << off;
}

/* ... and one still open gets its buffer's first byte, the rest counted. */
TEST(StdCLib, StreamReadFillsItsBuffer)
{
    uint32_t f = stream(37);
    Ptr buf = NewPtr(16);
    memcpy(buf, "XYZ", 3);
    memset(ptr_from_longint<Ptr>(f), 0, 24);
    uint32_t b = guest_cast<uint32_t>(buf);
    field(f, 4) = b;                                         /* _ptr */
    field(f, 8) = b;                                         /* _base */
    *ptr_from_longint<GUEST<uint16_t> *>(f + 18) = 0x0009;   /* _IOREAD | _IOMYBUF */
    EXPECT_EQ('X', filled(f, 3));
    EXPECT_EQ(b + 1, (uint32_t)field(f, 4));
    EXPECT_EQ(2u, (uint32_t)field(f, 0));
    memset(ptr_from_longint<Ptr>(f), 0, 24);
    DisposePtr(buf);
}

/* StdCLib is one state for all its clients: an import of it starts it
   afresh only while an application is being launched (or with no client),
   not when another process loads a library that imports it. The Finder on
   8.6 loading ATSUnicodeLib behind an application cleared its open streams
   (executor/tests/web/tab_stdio.mjs). */
TEST(StdCLib, OnlyAnApplicationsLaunchStartsItAfresh)
{
    uint32_t f = stream(37);
    auto flag = [&]() -> GUEST<uint16_t> & { return *ptr_from_longint<GUEST<uint16_t> *>(f + 18); };

    clib::launchingApplication = true;          /* this test's "application" */
    clib::stdclibImported();
    clib::launchingApplication = false;
    memset(ptr_from_longint<Ptr>(f), 0, 24);
    flag() = 0x0002;                            /* _IOWRT: a stream it has open */

    clib::stdclibImported();                    /* a library loaded meanwhile */
    EXPECT_EQ(0x0002, (uint16_t)flag()) << "the client's stream was cleared";

    clib::launchingApplication = true;          /* the next application */
    clib::stdclibImported();
    clib::launchingApplication = false;
    EXPECT_EQ(0, (uint16_t)flag());

    clib::stdclibProcessExit();                 /* no client again */
}
