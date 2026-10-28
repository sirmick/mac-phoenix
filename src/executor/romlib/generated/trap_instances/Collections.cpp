#define INSTANTIATE_TRAPS_Collections
#include <Collections.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Collections() {}
}
