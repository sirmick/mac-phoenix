#define INSTANTIATE_TRAPS_DialogMgr8
#include <DialogMgr8.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void DialogMgr8() {}
}
