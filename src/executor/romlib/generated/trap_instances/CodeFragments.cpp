#define INSTANTIATE_TRAPS_CodeFragments
#include <CodeFragments.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void CodeFragments() {}
}
