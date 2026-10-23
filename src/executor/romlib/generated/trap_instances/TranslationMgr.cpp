#define INSTANTIATE_TRAPS_TranslationMgr
#include <TranslationMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void TranslationMgr() {}
}
