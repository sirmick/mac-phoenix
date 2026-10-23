#define INSTANTIATE_TRAPS_PrintMgr
#include <PrintMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void PrintMgr() {}
}
