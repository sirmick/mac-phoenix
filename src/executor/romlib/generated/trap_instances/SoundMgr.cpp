#define INSTANTIATE_TRAPS_SoundMgr
#include <SoundMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void SoundMgr() {}
}
