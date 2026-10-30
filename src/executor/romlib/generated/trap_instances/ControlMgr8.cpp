#define INSTANTIATE_TRAPS_ControlMgr8
#include <ControlMgr8.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ControlMgr8() {}
}
