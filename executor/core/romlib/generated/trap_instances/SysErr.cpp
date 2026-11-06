#define INSTANTIATE_TRAPS_SysErr
#include <SysErr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void SysErr() {}
}
