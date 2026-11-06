#define INSTANTIATE_TRAPS_SpeechManager
#include <SpeechManager.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void SpeechManager() {}
}
