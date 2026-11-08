#define INSTANTIATE_TRAPS_ImageCompression
#include <ImageCompression.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ImageCompression() {}
}
