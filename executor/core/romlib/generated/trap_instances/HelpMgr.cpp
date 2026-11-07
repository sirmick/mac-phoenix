#define INSTANTIATE_TRAPS_HelpMgr
#include <HelpMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void HelpMgr() {}
}
