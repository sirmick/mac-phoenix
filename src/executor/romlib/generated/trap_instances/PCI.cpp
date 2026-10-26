#define INSTANTIATE_TRAPS_PCI
#include <PCI.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void PCI() {}
}
