#define INSTANTIATE_TRAPS_IntlUtil
#include <IntlUtil.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void IntlUtil() {}
}
