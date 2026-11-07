#define INSTANTIATE_TRAPS_Navigation
#include <Navigation.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Navigation() {}
}
