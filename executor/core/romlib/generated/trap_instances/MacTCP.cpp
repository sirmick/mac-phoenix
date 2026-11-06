#define INSTANTIATE_TRAPS_MacTCP
#include <MacTCP.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void MacTCP() {}
}
