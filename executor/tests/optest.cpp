#include <base/mactype.h>
#include <base/byteswap.h>
using namespace Executor;

void bar(GUEST<short>& y);

struct Foo
{
    GUEST_STRUCT;
    GUEST<short> x;
    GUEST<short> y;
    char z;
};

auto foo(GUEST<int32>& x)
{
    //GUEST<short> x = 6;

    //bar(--x);
    //return x++;

    //static Foo s{ 23, 42 };

    //return &s;
    //return Foo{ toGuest(guestvalues::GuestWrapper(23)), toGuest(42), toGuest('a') };


    return ptr_from_longint<char*>(x);
}