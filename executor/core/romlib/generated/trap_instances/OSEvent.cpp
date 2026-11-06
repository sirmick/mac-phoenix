#define INSTANTIATE_TRAPS_OSEvent
#include <OSEvent.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void OSEvent() {}
}
