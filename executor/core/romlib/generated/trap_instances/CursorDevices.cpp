#define INSTANTIATE_TRAPS_CursorDevices
#include <CursorDevices.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void CursorDevices() {}
}
