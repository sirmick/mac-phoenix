#include <base/common.h>
#include <base/builtinlibs.h>
#include <base/cpu.h>
#include <base/traps.h>
#include <OSUtil.h>
#include <MemoryMgr.h>
#include <rsys/cfm.h>
#include <PEFBinaryFormat.h>
#include <unordered_map>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <iostream>
#include <cassert>
#include <functional>
#include <algorithm>
#include <cstring>

#include <PowerCore.h>
#include <clib/clib.h>

using namespace Executor;

namespace
{
    std::unordered_map<std::string, std::vector<map_entry_t>> builtins;

    struct PPCCallback
    {
        GUEST_STRUCT;
        GUEST<GUEST<uint32_t>*> codePtr;
        GUEST<uint32_t> rtoc;
        GUEST<uint32_t> sc;
        const char* name;
        std::function<uint32_t (PowerCore&)> implementation;
    };

    PPCCallback callbacks[4096];
    int nCallbacks = 0;

    /* MacPhoenix: CFM-68K transition vectors (docs/executor/CFM68K.md): the
       code address, then what the callee loads into A5 (nothing here). In
       static storage, which the guest can address. */
    struct CFM68KVector
    {
        GUEST_STRUCT;
        GUEST<uint32_t> code;
        GUEST<uint32_t> a5;
    };

    struct CFM68KRoutine
    {
        const char *name;
        std::function<syn68k_addr_t (syn68k_addr_t)> implementation;
    };

    CFM68KVector vectors68k[4096];
    CFM68KRoutine routines68k[std::size(vectors68k)];
    int nVectors68k = 0;

    std::unordered_map<std::string, std::vector<map_entry_t>> builtins68k;

    syn68k_addr_t call_routine68k(syn68k_addr_t addr, void *arg)
    {
        CFM68KRoutine& r = *(CFM68KRoutine *)arg;
        if(r.implementation)
        {
            /* The Toolbox finds QuickDraw's globals through A5; CFM-68K code
               has its fragment's data there. A library routine keeps D3-D7
               and A2-A6, as the caller expects; a routine that runs 68k code
               (a callback, a patch) needn't have. */
            uint32_t saved[10] = { EM_D3, EM_D4, EM_D5, EM_D6, EM_D7,
                                   EM_A2, EM_A3, EM_A4, EM_A5, EM_A6 };
            EM_A5 = guest_cast<uint32_t>(LM(CurrentA5));
            syn68k_addr_t ret = r.implementation(addr);
            EM_D3 = saved[0]; EM_D4 = saved[1]; EM_D5 = saved[2];
            EM_D6 = saved[3]; EM_D7 = saved[4];
            EM_A2 = saved[5]; EM_A3 = saved[6]; EM_A4 = saved[7];
            EM_A5 = saved[8]; EM_A6 = saved[9];
            return ret;
        }
        /* An import nothing exports: the caller pops its arguments, so the
           call can be answered. */
        traps::log_unimplemented(std::string("CFM-68K ") + r.name);
        EM_D0 = EM_A0 = 0;
        return POPADDR();
    }

    CFM68KVector *new_vector68k(const char *name, std::function<syn68k_addr_t (syn68k_addr_t)> code)
    {
        assert(nVectors68k < (int)std::size(vectors68k));
        int i = nVectors68k++;
        routines68k[i] = { name, std::move(code) };
        vectors68k[i].code = callback_install(&call_routine68k, &routines68k[i]);
        vectors68k[i].a5 = 0;
        return &vectors68k[i];
    }

    /* InterfaceLib's variadic routines: the caller pops their arguments.
       A routine with fixed arguments pops its own in CFM-68K, C ones
       (c2pstr) as much as Pascal ones (seen in the callers' code). */
    const char *const c_routines68k[] = {
        "CallUniversalProc", "CallOSTrapUniversalProc",
        "AEBuild", "AEBuildDesc", "AEBuildParameters", "AEBuildAppleEvent",
    };

    /* Apple's other libraries a CFM-68K application imports from, answered
       from InterfaceLib's routines when no file provides them. */
    const char *const interfacelib_aliases68k[] = {
        "DragLib", "ThreadsLib", "AppearanceLib", "WindowsLib", "ControlsLib",
        "MenusLib", "DialogsLib", "PrivateInterfaceLib", "ObjectSupportLib",
    };
}

uint32_t builtinlibs::handleSC(PowerCore& cpu)
{
    PPCCallback *cb = guestvalues::GuestTypeTraits<PPCCallback*>::reg_to_host(cpu.CIA - 8);
    int index = cb - callbacks;
    if(index >= 0 && index < nCallbacks)
    {
        if(cb->implementation)
        {
            /* The call site, for the trap log (--executor-logtraps): a
               PowerPC caller's return address. */
            uint32_t saved = currentTrapPC;
            currentTrapPC = cpu.lr;
            uint32_t ret = cb->implementation(cpu);
            currentTrapPC = saved;
            return ret;
        }
        /* MacPhoenix: an import nothing exports. A PowerPC caller cleans
           up after itself, so the call can be answered: 0, and logged. */
        traps::log_unimplemented(std::string("PowerPC ") + cb->name);
        cpu.r[3] = 0;
        return cpu.lr;
    }
    else
    {
        std::cerr << "Illegal PPC system call\n";
        std::abort();
    }
}

static PPCCallback& new_callback(const char *function, std::function<uint32_t (PowerCore&)> code)
{
    assert(nCallbacks < std::size(callbacks));
    PPCCallback& callback = callbacks[nCallbacks++];
    callback.sc = POWERCORE_OP_SYSCALL;
    callback.codePtr = &callback.sc;
    callback.implementation = std::move(code);
    callback.name = function;
    return callback;
}

void builtinlibs::addPPCEntrypoint(const char *library, const char *function, std::function<uint32_t (PowerCore&)> code)
{
    builtins[library].push_back({function, &new_callback(function, std::move(code))});
}

uint32_t builtinlibs::newPPCRoutine(const char *name, std::function<uint32_t (PowerCore&)> code)
{
    return ptr_to_longint(&new_callback(name, std::move(code)));
}

void builtinlibs::addPPCData(const char *library, const char *name, void *address)
{
    builtins[library].push_back({name, address});
}

namespace
{
    struct ASLMEntry
    {
        const char *name;
        bool raw;
        std::function<uint32_t (PowerCore&)> code;
    };
    std::map<std::pair<std::string, unsigned>, ASLMEntry>& aslmEntries()
    {
        static std::map<std::pair<std::string, unsigned>, ASLMEntry> map;
        return map;
    }
    std::map<std::pair<std::string, unsigned>, const char *>& aslmExpectedRaw()
    {
        static std::map<std::pair<std::string, unsigned>, const char *> map;
        return map;
    }
}

void builtinlibs::addASLMEntrypoint(const char *set, unsigned index, const char *function,
                                    std::function<uint32_t (PowerCore&)> code)
{
    aslmEntries()[{ set, index }] = { function, false, std::move(code) };
}

void builtinlibs::addASLMRawEntrypoint(const char *set, unsigned index, const char *function,
                                       std::function<uint32_t (PowerCore&)> code)
{
    aslmEntries()[{ set, index }] = { function, true, std::move(code) };
}

void builtinlibs::expectASLMRawEntrypoint(const char *set, unsigned index, const char *function)
{
    aslmExpectedRaw()[{ set, index }] = function;
}

const std::function<uint32_t (PowerCore&)> *builtinlibs::findASLMEntrypoint(const std::string& set,
                                                                             unsigned index)
{
    auto it = aslmEntries().find({ set, index });
    return it == aslmEntries().end() ? nullptr : &it->second.code;
}

void builtinlibs::checkASLMRawEntrypoints()
{
    for(auto& [key, function] : aslmExpectedRaw())
    {
        auto it = aslmEntries().find(key);
        if(it == aslmEntries().end() || !it->second.raw || strcmp(it->second.name, function))
            std::cerr << "[Executor] ASLM \"" << key.first << "\" " << key.second << ": "
                      << function << " is declared raw in the definitions but not registered\n";
    }
    for(auto& [key, entry] : aslmEntries())
        if(entry.raw && !aslmExpectedRaw().count(key))
            std::cerr << "[Executor] ASLM \"" << key.first << "\" " << key.second << ": "
                      << entry.name << " is registered raw but not declared in the definitions\n";
}

static std::unordered_map<std::string, std::vector<std::string>>& expectedRaw()
{
    static std::unordered_map<std::string, std::vector<std::string>> map;
    return map;
}

void builtinlibs::expectRawExport(const char *library, const char *function)
{
    expectedRaw()[library].push_back(function);
}

bool builtinlibs::isExpectedRawExport(const char *library, const char *function)
{
    auto it = expectedRaw().find(library);
    return it != expectedRaw().end()
        && std::find(it->second.begin(), it->second.end(), function) != it->second.end();
}

void builtinlibs::checkRawExports(const char *library)
{
    auto it = expectedRaw().find(library);
    if(it == expectedRaw().end())
        return;
    const auto& entries = builtins[library];
    for(const std::string& function : it->second)
        if(std::none_of(entries.begin(), entries.end(),
                        [&](const map_entry_t& e) { return function == e.symbol_name; }))
            std::cerr << "[Executor] " << library << ": " << function
                      << " is declared raw in the definitions but not registered\n";
}

static std::unordered_map<std::string, std::function<syn68k_addr_t (syn68k_addr_t)>>& overrides68k()
{
    static std::unordered_map<std::string, std::function<syn68k_addr_t (syn68k_addr_t)>> map;
    return map;
}

void builtinlibs::overrideCFM68KEntrypoint(const char *function,
                                           std::function<syn68k_addr_t (syn68k_addr_t)> code)
{
    /* Called from static initializers: only noted here; the entry takes it
       when it is made, or InterfaceLib gets it when first asked for. */
    overrides68k()[function] = code;
}

void builtinlibs::addCFM68KEntrypoint(const char *library, const char *function,
                                      std::function<syn68k_addr_t (syn68k_addr_t)> code)
{
    if(auto it = overrides68k().find(function); it != overrides68k().end())
        code = it->second;
    auto& entries = builtins68k[library];
    for(auto& e : entries)
        if(!strcmp(e.symbol_name, function))
        {
            int i = (CFM68KVector *)e.value - vectors68k;
            routines68k[i].implementation = std::move(code);
            return;
        }
    entries.push_back({function, new_vector68k(function, std::move(code))});
}

bool builtinlibs::cfm68kCalleePops(const char *function)
{
    for(const char *c : c_routines68k)
        if(!strcmp(c, function))
            return false;
    return true;
}

void builtinlibs::addCFM68KData(const char *library, const char *name, void *address)
{
    builtins68k[library].push_back({name, address});
}

Ptr builtinlibs::makeUndefinedSymbolStub(const char *name, OSType arch)
{
    if(arch == "m68k"_4)
        return (Ptr)new_vector68k(name, nullptr);

    assert(nCallbacks < std::size(callbacks));
    PPCCallback& callback = callbacks[nCallbacks++];
    callback.sc = POWERCORE_OP_SYSCALL;
    callback.codePtr = &callback.sc;
    callback.implementation = nullptr;
    callback.name = name;
    return (Ptr) &callback;
}

/* By library: (routine, library whose entry it is). */
static std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>& alsoExports()
{
    static std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> map;
    return map;
}

void builtinlibs::alsoExport(const char *library, const char *function, const char *from)
{
    alsoExports()[library].push_back({ function, from });
}

/* The entries another library exports as well, added to its table (PowerPC
   or CFM-68K) the first time it is asked for. */
static void addAlsoExports(std::unordered_map<std::string, std::vector<map_entry_t>>& table,
                           const std::string& library)
{
    auto it = alsoExports().find(library);
    if(it == alsoExports().end())
        return;
    static std::set<std::pair<const void *, std::string>> done;
    if(!done.insert({ &table, library }).second)
        return;
    for(const auto& [function, from_lib] : it->second)
    {
        auto& from = table[from_lib];
        for(const map_entry_t& e : from)
            if(function == e.symbol_name)
            {
                table[library].push_back(e);
                break;
            }
    }
}

/* MacPhoenix: libraries new in a later System are found only when Executor
   runs that System (or a later one), as on a Mac (docs/executor/MacOS8-9.md,
   "One Toolbox for 7, 8 and 9"; LibProbe on 7.6.1, 8.6 and 9.0.4). */
static bool libraryInThisSystem(const std::string& name)
{
    static const std::unordered_map<std::string, uint16_t> since = {
        { "Memory Manager", 0x0800 }, /* 8.6 has it, 7.6.1 hasn't */
        { "WindowsLib", 0x0800 }, /* Window Manager 2.0's calls: 8.5's */
        { "AppearanceLib", 0x0800 }, /* the Appearance Manager: 8.0's */
    };
    auto it = since.find(name);
    return it == since.end() || (uint16_t)LM(SysVersion) >= it->second;
}

namespace
{
struct data_export
{
    const char *library, *name;
    void *(*getter)();
};
std::vector<data_export>& dataExports()
{
    static std::vector<data_export> list;
    return list;
}
}

void builtinlibs::addDataExport(const char *library, const char *name, void *(*getter)())
{
    dataExports().push_back({ library, name, getter });
}

/* The entries registered on first use: StdCLib and MathLib, in ROM on a
   Power Mac (docs/executor/PPCLibs.md), StdCLib's CFM-68K halves too; then
   the data exports the definitions declare. */
static void addLazyEntrypoints()
{
    static bool clibs_added = false;
    if(!clibs_added)
    {
        clibs_added = true;
        clib::addStdCLib();
        clib::addMathLib();
        builtinlibs::checkRawExports("StdCLib");
        builtinlibs::checkRawExports("MathLib");
    }

    static bool data_added = false;
    if(!data_added)
    {
        data_added = true;
        for(const data_export& d : dataExports())
            if(void *address = d.getter())
            {
                builtinlibs::addPPCData(d.library, d.name, address);
                builtinlibs::addCFM68KData(d.library, d.name, address);
            }
    }
}

/* CFM-68K: InterfaceLib's entries for routines whose entry is written by
   hand (overrideCFM68KEntrypoint), added to its table once. */
static void addOverrides68k()
{
    static bool added = false;
    if(added)
        return;
    added = true;
    for(auto& [function, code] : overrides68k())
    {
        auto& entries = builtins68k["InterfaceLib"];
        if(std::none_of(entries.begin(), entries.end(),
                        [&](const map_entry_t& e) { return function == e.symbol_name; }))
            entries.push_back({function.c_str(), new_vector68k(function.c_str(), code)});
    }
}

std::map<std::string, std::vector<std::string>> builtinlibs::exportTable(OSType arch)
{
    addLazyEntrypoints();
    auto& table = arch == "m68k"_4 ? builtins68k : builtins;
    if(arch == "m68k"_4)
        addOverrides68k();
    std::vector<std::string> also;
    for(const auto& [library, entries] : alsoExports())
        also.push_back(library);
    for(const std::string& library : also)
        if(library != "InterfaceLib")
            addAlsoExports(table, library);
    std::map<std::string, std::vector<std::string>> out;
    for(const auto& [library, entries] : table)
    {
        std::vector<std::string>& names = out[library];
        for(const map_entry_t& e : entries)
            names.push_back(e.symbol_name);
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
    }
    return out;
}

ConnectionID builtinlibs::getBuiltinLib(ConstStr255Param libname, OSType arch)
{
    addLazyEntrypoints();

    std::string name(libname + 1, libname + 1 + libname[0]);
    if(arch == "m68k"_4)
    {
        if(std::find(std::begin(interfacelib_aliases68k), std::end(interfacelib_aliases68k), name)
           != std::end(interfacelib_aliases68k))
        {
            /* MacPhoenix: answered from InterfaceLib's table, with what the
               library exports under its own name merged in (PrivateInterface-
               Lib's accessors: `library:` on a lowmem entry). */
            static std::set<std::string> merged;
            if(merged.insert(name).second)
            {
                addAlsoExports(builtins68k, name);
                auto& into = builtins68k["InterfaceLib"];
                for(const map_entry_t& e : builtins68k[name])
                    if(std::none_of(into.begin(), into.end(),
                                    [&](const map_entry_t& i) { return !strcmp(i.symbol_name, e.symbol_name); }))
                        into.push_back(e);
            }
            name = "InterfaceLib";
        }
        /* MPW's CFM-68K programs import StdCLib as two libraries (the
           Enabler's): NuStdCLib (string, ctype, printf, time, ...) and
           NuIntEnv (stdio's files, errno, exit, signal, PL strings). One
           StdCLib answers both; a new application's starts afresh. */
        if(name == "NuStdCLib" || name == "NuIntEnv")
        {
            name = "StdCLib";
            clib::stdclibStart(true);
        }
        /* One connection per library: its exports never move. */
        static std::unordered_map<std::string, ConnectionID> connections;
        if(name != "InterfaceLib")
            addAlsoExports(builtins68k, name);
        ConnectionID& cid = connections[name];
        if(!cid && name == "InterfaceLib")
            addOverrides68k();
        auto it = builtins68k.find(name);
        if(it == builtins68k.end())
            return nullptr;
        if(!cid)
        {
            cid = ROMlib_new_connection(1);
            cid->lihp = ROMlib_build_pef_hash(it->second.data(), it->second.size());
            cid->ref_count = 1;
        }
        return cid;
    }

    if(name != "InterfaceLib")
        addAlsoExports(builtins, name);
    if(!libraryInThisSystem(name))
        return nullptr;
    auto it = builtins.find(name);
    if(it == builtins.end())
        return nullptr;
    else
    {
        /* An application's own StdCLib: a connection per application. */
        if(name == "StdCLib")
            clib::stdclibStart();
        ConnectionID cid = ROMlib_new_connection(1);
        cid->lihp = ROMlib_build_pef_hash(it->second.data(), it->second.size());
        cid->ref_count = 1;
        return cid;
    }
}
