#pragma once

#include <CodeFragments.h>
#include <syn68k_public.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

class PowerCore;

namespace Executor
{
    namespace builtinlibs
    {
        uint32_t handleSC(PowerCore& cpu);

        void addPPCEntrypoint(const char *library, const char *function, std::function<uint32_t (PowerCore&)> code);

        /* MacPhoenix: a data export for PowerPC fragments (StdCLib's _iob,
           errno): the import binds to `address`, guest-addressable storage. */
        void addPPCData(const char *library, const char *name, void *address);

        /* A data export the definitions declare (`data:`): PowerPC and
           CFM-68K, bound to what `getter` answers once the libraries are
           set up (their storage may be made then). */
        void addDataExport(const char *library, const char *name, void *(*getter)());

        /* MacPhoenix: a routine for PowerPC code that no library exports
           (a slot of an object's vtable): the guest address of its
           transition vector. */
        uint32_t newPPCRoutine(const char *name, std::function<uint32_t (PowerCore&)> code);

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

        /* A library routine the definitions declare `raw: true`
           (docs/executor/LIBRARIES.md): its library's code registers it.
           checkRawExports reports any declared but not registered, and any
           registered raw that the definitions don't declare. */
        void expectRawExport(const char *library, const char *function);
        bool isExpectedRawExport(const char *library, const char *function);
        void checkRawExports(const char *library);

        /* MacPhoenix: a library that exports one of InterfaceLib's routines
           as well (SoundLib's Sound Manager calls; `library:` on a trap's
           entry): the same entry under its name, PowerPC and CFM-68K. */
        void alsoExport(const char *library, const char *function, const char *from = "InterfaceLib");

        /* MacPhoenix: routines of ASLM function sets that PowerPC code
           binds through ASLM itself (`aslm:` without `library:` in the
           definitions), by set and index, for Executor's PowerPC binder
           (opentransport/ot_slm_ppc.cpp). A raw one (`raw: true`) is
           registered by its own code; checkASLMRawEntrypoints reports any
           declared but not registered, or registered raw and not declared. */
        void addASLMEntrypoint(const char *set, unsigned index, const char *function,
                               std::function<uint32_t (PowerCore&)> code);
        void addASLMRawEntrypoint(const char *set, unsigned index, const char *function,
                                  std::function<uint32_t (PowerCore&)> code);
        void expectASLMRawEntrypoint(const char *set, unsigned index, const char *function);
        const std::function<uint32_t (PowerCore&)> *findASLMEntrypoint(const std::string& set,
                                                                        unsigned index);
        void checkASLMRawEntrypoints();

        ConnectionID getBuiltinLib(ConstStr255Param libname, OSType arch = "pwpc"_4);

        /* MacPhoenix: every library's exports, by name, as CFM binds them
           ("pwpc" or "m68k"), the libraries' added entries included
           (tools/macdecode/libraries.py reads them, via the executor_tests
           export test). */
        std::map<std::string, std::vector<std::string>> exportTable(OSType arch);

        Ptr makeUndefinedSymbolStub(const char *name, OSType arch = "pwpc"_4);
    }
}
