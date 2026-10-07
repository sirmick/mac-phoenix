#include <base/builtinlibs.h>
#include <rsys/cfm.h>
#include <PEFBinaryFormat.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>
#include <cassert>
#include <functional>

#include <PowerCore.h>

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

}

uint32_t builtinlibs::handleSC(PowerCore& cpu)
{
    PPCCallback *cb = guestvalues::GuestTypeTraits<PPCCallback*>::reg_to_host(cpu.CIA - 8);
    int index = cb - callbacks;
    if(index >= 0 && index < nCallbacks)
    {
        if(cb->implementation)
            return cb->implementation(cpu);
        else
        {
            std::cerr << "Unimplemented PPC entrypoint: " << cb->name << std::endl << std::flush;
            std::abort();
        }
    }
    else
    {
        std::cerr << "Illegal PPC system call\n";
        std::abort();
    }
}

void builtinlibs::addPPCEntrypoint(const char *library, const char *function, std::function<uint32_t (PowerCore&)> code)
{
    assert(nCallbacks < std::size(callbacks));
    PPCCallback& callback = callbacks[nCallbacks++];
    callback.sc = 0x44000002;
    callback.codePtr = &callback.sc;
    callback.implementation = code;
    callback.name = function;

    builtins[library].push_back({function, &callback});
}

Ptr builtinlibs::makeUndefinedSymbolStub(const char *name)
{
    assert(nCallbacks < std::size(callbacks));
    PPCCallback& callback = callbacks[nCallbacks++];
    callback.sc = 0x44000002;
    callback.codePtr = &callback.sc;
    callback.implementation = nullptr;
    callback.name = name;
    return (Ptr) &callback;
}

ConnectionID builtinlibs::getBuiltinLib(ConstStr255Param libname)
{
    std::string name(libname + 1, libname + 1 + libname[0]);
    auto it = builtins.find(name);
    if(it == builtins.end())
        return nullptr;
    else
    {
        ConnectionID cid = ROMlib_new_connection(1);
        cid->lihp = ROMlib_build_pef_hash(it->second.data(), it->second.size());
        cid->ref_count = 1;
        return cid;
    }
}
