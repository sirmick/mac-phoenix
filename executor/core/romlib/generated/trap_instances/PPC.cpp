#define INSTANTIATE_TRAPS_PPC
#include <PPC.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void PPC() {}
}
