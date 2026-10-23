#define INSTANTIATE_TRAPS_SegmentLdr
#include <SegmentLdr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void SegmentLdr() {}
}
