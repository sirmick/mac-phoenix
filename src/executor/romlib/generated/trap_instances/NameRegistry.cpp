#define INSTANTIATE_TRAPS_NameRegistry
#include <NameRegistry.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void NameRegistry() {}
}
