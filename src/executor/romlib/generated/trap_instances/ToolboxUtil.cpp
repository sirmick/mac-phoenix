#define INSTANTIATE_TRAPS_ToolboxUtil
#include <ToolboxUtil.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ToolboxUtil() {}
}
