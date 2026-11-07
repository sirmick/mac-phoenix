#define INSTANTIATE_TRAPS_BinaryDecimal
#include <BinaryDecimal.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void BinaryDecimal() {}
}
