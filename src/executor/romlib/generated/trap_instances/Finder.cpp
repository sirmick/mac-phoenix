#define INSTANTIATE_TRAPS_Finder
#include <Finder.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Finder() {}
}
