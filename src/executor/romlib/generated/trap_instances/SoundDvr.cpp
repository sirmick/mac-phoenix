#define INSTANTIATE_TRAPS_SoundDvr
#include <SoundDvr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void SoundDvr() {}
}
