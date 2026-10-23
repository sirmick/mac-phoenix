#define INSTANTIATE_TRAPS_TextServices
#include <TextServices.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void TextServices() {}
}
