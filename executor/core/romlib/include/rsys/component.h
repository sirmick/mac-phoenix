#pragma once

#include <ProcessMgr.h>
#include <initializer_list>

namespace Executor
{
/* MacPhoenix Component Manager (component.cpp). */

/* Register the 'thng' resources of an open resource file, as
   RegisterComponentResourceFile does; returns how many registered.
   without_init: the file's own INIT hasn't run (QuickTime's components
   with QuickTime off): a component that wants its register message is
   left out, its code not run. */
int32_t ROMlib_register_components(INTEGER refnum, bool global, bool without_init = false);
/* MacPhoenix: a component Executor implements in C++. Its calls are the
   definitions' functions on the dispatcher `calls` (a `selector-location:
   Component` one) that name an Executor routine (COMPONENT_FUNCTION);
   open, close, register, version and can-do are answered here, anything
   else is logged and answered badComponentSelector. 'thng' resources of
   the same type are not registered after it (the first lends it its name,
   info and icon); returns its ID. */
uint32_t ROMlib_register_native_component(uint32_t type, uint32_t subtype,
                                          uint32_t manufacturer, const char *name,
                                          const char *calls, int32_t version, uint32_t flags = 0);

/* A process quit: its components go, and any component, even a global one,
   whose code was in its partition [part_lo, part_hi) (guest addresses). */
void ROMlib_components_process_exit(const ProcessSerialNumber *psn,
                                    uint32_t part_lo = 0, uint32_t part_hi = 0);

/* MacPhoenix: a call to a component instance from C++, as an application's
   glue makes it (the ICM's calls to codecs): the arguments in declaration
   order, each a word (bytes 2) or a long (4), a parameter block on the 68k
   stack. badComponentInstance for an instance that isn't one. */
struct ComponentArg
{
    uint32_t value;
    uint8_t bytes = 4;
    ComponentArg(uint32_t v) : value(v) {}
    ComponentArg(uint32_t v, uint8_t b) : value(v), bytes(b) {}
    template<typename T>
    ComponentArg(T *p) : value(p ? US_TO_SYN68K(p) : 0) {}
};
inline ComponentArg ComponentWord(int16_t v) { return ComponentArg((uint16_t)v, 2); }
int32_t ROMlib_call_component(uint32_t instance, int16_t what, std::initializer_list<ComponentArg> args);
}
