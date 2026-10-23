#define INSTANTIATE_TRAPS_ExMacTypes
#include <ExMacTypes.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ExMacTypes() {}
}
