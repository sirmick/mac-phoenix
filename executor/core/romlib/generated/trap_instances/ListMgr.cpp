#define INSTANTIATE_TRAPS_ListMgr
#include <ListMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ListMgr() {}
}
