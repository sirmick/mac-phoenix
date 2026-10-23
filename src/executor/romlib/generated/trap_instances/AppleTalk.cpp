#define INSTANTIATE_TRAPS_AppleTalk
#include <AppleTalk.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void AppleTalk() {}
}
