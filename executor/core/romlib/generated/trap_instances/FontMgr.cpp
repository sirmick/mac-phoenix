#define INSTANTIATE_TRAPS_FontMgr
#include <FontMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void FontMgr() {}
}
