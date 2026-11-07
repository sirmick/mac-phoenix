#define INSTANTIATE_TRAPS_LowMem
#include <LowMem.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void LowMem() {}
}
