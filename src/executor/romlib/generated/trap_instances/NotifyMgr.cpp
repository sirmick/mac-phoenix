#define INSTANTIATE_TRAPS_NotifyMgr
#include <NotifyMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void NotifyMgr() {}
}
