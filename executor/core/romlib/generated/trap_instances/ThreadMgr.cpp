#define INSTANTIATE_TRAPS_ThreadMgr
#include <ThreadMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ThreadMgr() {}
}
