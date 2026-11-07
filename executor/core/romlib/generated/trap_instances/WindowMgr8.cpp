#define INSTANTIATE_TRAPS_WindowMgr8
#include <WindowMgr8.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void WindowMgr8() {}
}
