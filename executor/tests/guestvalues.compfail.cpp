#include <base/mactype.h>
#include <base/byteswap.h>
using namespace Executor;

#ifdef COMPILEFAIL_rawHostOrder64_1
void test()
{
    GUEST<uint64_t> x {};
    x.raw_host_order();
}
#endif
#ifdef COMPILEFAIL_rawHostOrder64_2
void test()
{
    GUEST<uint64_t> x {};
    x.raw_host_order(42);
}
#endif

#ifdef COMPILEFAIL_fail1
void test()
{
    GUEST<void*> x {};
    GUEST<int> y {};
    y & x;
}
#endif