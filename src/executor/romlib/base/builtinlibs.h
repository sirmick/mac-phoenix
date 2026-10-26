#pragma once

#include <CodeFragments.h>
#include <syn68k_public.h>
#include <functional>

class PowerCore;

namespace Executor
{
    namespace builtinlibs
    {
        uint32_t handleSC(PowerCore& cpu);

        void addPPCEntrypoint(const char *library, const char *function, std::function<uint32_t (PowerCore&)> code);

        /* MacPhoenix: the same routine for CFM-68K code (docs/executor/
           CFM68K.md): an 8-byte transition vector whose code is a callback;
           `code` gets the call as made, return address on top of the
           stack, and answers where to continue. A later call for the same
           library and name replaces the routine. */
        void addCFM68KEntrypoint(const char *library, const char *function,
                                 std::function<syn68k_addr_t (syn68k_addr_t)> code);

        /* A data export for CFM-68K fragments (CallComponentUPP): the
           import binds to `address` itself. */
        void addCFM68KData(const char *library, const char *name, void *address);

        /* A routine whose CFM-68K entry can't be generated from its C++
           signature (CallUniversalProc: variadic). */
        void overrideCFM68KEntrypoint(const char *function,
                                      std::function<syn68k_addr_t (syn68k_addr_t)> code);

        /* Whether a library routine pops its own arguments (Pascal) or
           leaves them to its caller (C, variadic). */
        bool cfm68kCalleePops(const char *function);

        /* InterfaceLib's glue routines (cfm68k_glue.cpp). */
        void addGlueEntrypoints();

        ConnectionID getBuiltinLib(ConstStr255Param libname, OSType arch = "pwpc"_4);

        Ptr makeUndefinedSymbolStub(const char *name, OSType arch = "pwpc"_4);
    }
}
