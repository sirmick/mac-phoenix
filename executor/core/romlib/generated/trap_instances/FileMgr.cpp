#define INSTANTIATE_TRAPS_FileMgr
#include <FileMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void FileMgr() {}
}
