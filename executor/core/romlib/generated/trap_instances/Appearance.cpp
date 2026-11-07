#define INSTANTIATE_TRAPS_Appearance
#include <Appearance.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Appearance() {}
}
