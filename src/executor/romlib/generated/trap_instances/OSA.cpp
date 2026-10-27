#define INSTANTIATE_TRAPS_OSA
#include <OSA.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void OSA() {}
}
