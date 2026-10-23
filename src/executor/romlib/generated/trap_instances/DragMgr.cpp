#define INSTANTIATE_TRAPS_DragMgr
#include <DragMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void DragMgr() {}
}
