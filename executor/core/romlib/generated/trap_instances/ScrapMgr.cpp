#define INSTANTIATE_TRAPS_ScrapMgr
#include <ScrapMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ScrapMgr() {}
}
