#define INSTANTIATE_TRAPS_MemoryManagerLib
#include <MemoryManagerLib.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void MemoryManagerLib() {}
}
