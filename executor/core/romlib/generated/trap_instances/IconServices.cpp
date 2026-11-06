#define INSTANTIATE_TRAPS_IconServices
#include <IconServices.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void IconServices() {}
}
