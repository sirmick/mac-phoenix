#define INSTANTIATE_TRAPS_VRetraceMgr
#include <VRetraceMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void VRetraceMgr() {}
}
