#define INSTANTIATE_TRAPS_TextEdit
#include <TextEdit.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void TextEdit() {}
}
