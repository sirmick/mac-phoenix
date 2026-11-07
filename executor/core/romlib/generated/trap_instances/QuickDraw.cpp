#define INSTANTIATE_TRAPS_QuickDraw
#include <QuickDraw.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void QuickDraw() {}
}
