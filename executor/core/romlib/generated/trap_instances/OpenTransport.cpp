#define INSTANTIATE_TRAPS_OpenTransport
#include <OpenTransport.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void OpenTransport() {}
}
