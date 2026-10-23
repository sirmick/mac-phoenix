#define INSTANTIATE_TRAPS_ResourceMgr
#include <ResourceMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void ResourceMgr() {}
}
