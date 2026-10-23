#define INSTANTIATE_TRAPS_VDriver
#include <VDriver.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void VDriver() {}
}
