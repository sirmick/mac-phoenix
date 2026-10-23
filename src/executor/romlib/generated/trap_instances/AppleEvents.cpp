#define INSTANTIATE_TRAPS_AppleEvents
#include <AppleEvents.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void AppleEvents() {}
}
