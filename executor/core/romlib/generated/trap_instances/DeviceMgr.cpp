#define INSTANTIATE_TRAPS_DeviceMgr
#include <DeviceMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void DeviceMgr() {}
}
