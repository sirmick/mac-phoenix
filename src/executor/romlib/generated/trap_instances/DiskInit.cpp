#define INSTANTIATE_TRAPS_DiskInit
#include <DiskInit.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void DiskInit() {}
}
