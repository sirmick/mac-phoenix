#define INSTANTIATE_TRAPS_MixedMode
#include <MixedMode.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void MixedMode() {}
}
