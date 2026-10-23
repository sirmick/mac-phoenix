#define INSTANTIATE_TRAPS_QuickTime
#include <QuickTime.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void QuickTime() {}
}
