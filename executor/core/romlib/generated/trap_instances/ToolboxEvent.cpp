#define INSTANTIATE_TRAPS_ToolboxEvent
#include <ToolboxEvent.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ToolboxEvent() {}
}
