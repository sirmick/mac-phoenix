#define INSTANTIATE_TRAPS_Displays
#include <Displays.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Displays() {}
}
