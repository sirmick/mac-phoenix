#define INSTANTIATE_TRAPS_PEFBinaryFormat
#include <PEFBinaryFormat.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void PEFBinaryFormat() {}
}
