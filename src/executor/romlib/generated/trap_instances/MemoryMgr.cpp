#define INSTANTIATE_TRAPS_MemoryMgr
#include <MemoryMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void MemoryMgr() {}
}
