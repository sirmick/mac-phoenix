#define INSTANTIATE_TRAPS_Serial
#include <Serial.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void Serial() {}
}
