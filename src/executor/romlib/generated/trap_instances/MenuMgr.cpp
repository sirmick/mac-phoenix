#define INSTANTIATE_TRAPS_MenuMgr
#include <MenuMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void MenuMgr() {}
}
