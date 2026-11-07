#define INSTANTIATE_TRAPS_EditionMgr
#include <EditionMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void EditionMgr() {}
}
