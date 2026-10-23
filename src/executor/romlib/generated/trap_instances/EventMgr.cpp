#define INSTANTIATE_TRAPS_EventMgr
#include <EventMgr.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void EventMgr() {}
}
