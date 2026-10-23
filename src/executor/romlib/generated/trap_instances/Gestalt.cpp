#define INSTANTIATE_TRAPS_Gestalt
#include <Gestalt.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Gestalt() {}
}
