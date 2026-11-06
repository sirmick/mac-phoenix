#define INSTANTIATE_TRAPS_OpenTptInternet
#include <OpenTptInternet.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void OpenTptInternet() {}
}
