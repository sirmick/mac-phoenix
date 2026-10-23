#define INSTANTIATE_TRAPS_ShutDown
#include <ShutDown.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ShutDown() {}
}
