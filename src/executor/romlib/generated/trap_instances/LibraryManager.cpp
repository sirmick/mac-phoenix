#define INSTANTIATE_TRAPS_LibraryManager
#include <LibraryManager.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void LibraryManager() {}
}
