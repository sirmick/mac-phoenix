#define INSTANTIATE_TRAPS_Package
#include <Package.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Package() {}
}
