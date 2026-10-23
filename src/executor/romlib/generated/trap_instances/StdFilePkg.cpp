#define INSTANTIATE_TRAPS_StdFilePkg
#include <StdFilePkg.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void StdFilePkg() {}
}
