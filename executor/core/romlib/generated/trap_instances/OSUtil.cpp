#define INSTANTIATE_TRAPS_OSUtil
#include <OSUtil.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void OSUtil() {}
}
