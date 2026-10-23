#define INSTANTIATE_TRAPS_Components
#include <Components.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Components() {}
}
