#define INSTANTIATE_TRAPS_CommTool
#include <CommTool.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void CommTool() {}
}
