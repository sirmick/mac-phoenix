#define INSTANTIATE_TRAPS_DriverServices
#include <DriverServices.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void DriverServices() {}
}
