#define INSTANTIATE_TRAPS_ProcessMgr
#include <ProcessMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ProcessMgr() {}
}
