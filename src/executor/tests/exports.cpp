/* MacPhoenix: the libraries' export tables as CFM binds them, PowerPC and
   CFM-68K. Written to EXECUTOR_EXPORTS_OUT (build/executor-exports.json),
   which tools/macdecode/libraries.py reads: what Executor exports is what the
   definitions generate, expanded as the trap macros expand them (LMGet/LMSet
   from lowmem entries, Sync/Async, a trap's flag variants). */

#include "gtest/gtest.h"

#include <base/builtinlibs.h>

#include <algorithm>
#include <fstream>
#include <string>

using namespace Executor;

namespace
{
/* An implementation's own name, not an entry point of Apple's libraries. */
bool internal_name(const std::string& name)
{
    return name.find('/') != std::string::npos
        || name.rfind("INVALID_", 0) == 0
        || (name.size() > 6 && name.compare(name.size() - 6, 6, "_flags") == 0);
}

std::string json_string(const std::string& s)
{
    std::string out = "\"";
    for(char c : s)
    {
        if(c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}
}

TEST(Exports, NoInternalNames)
{
    for(OSType arch : { "pwpc"_4, "m68k"_4 })
        for(const auto& [library, names] : builtinlibs::exportTable(arch))
            for(const std::string& name : names)
                EXPECT_FALSE(internal_name(name)) << library << " exports " << name;
}

static bool exported(OSType arch, const std::string& library, const std::string& name)
{
    auto table = builtinlibs::exportTable(arch);
    auto it = table.find(library);
    return it != table.end() && std::find(it->second.begin(), it->second.end(), name) != it->second.end();
}

/* 6f9b9c02: GetWDInfo and the Comm Toolbox queue calls had working
   routines but weren't exported: code importing them by name (the 8.6
   Finder) got the unresolved-import stub, which answers 0. */
TEST(Exports, WorkingDirectoryAndCommToolbox)
{
    for(OSType arch : { "pwpc"_4, "m68k"_4 })
        for(const char *name : { "GetWDInfo", "CRMGetCRMVersion", "CRMGetHeader", "CRMInstall", "CRMRemove",
                                 "CRMSearch" })
            EXPECT_TRUE(exported(arch, "InterfaceLib", name)) << name;
}

/* 806e33c6: YieldToAnyThread is ThreadsLib's, as Apple's is; Apple's
   InterfaceLib doesn't export it (a4e71219 exported it from both and was
   reverted). */
TEST(Exports, YieldToAnyThreadFromThreadsLib)
{
    EXPECT_TRUE(exported("pwpc"_4, "ThreadsLib", "YieldToAnyThread"));
    EXPECT_FALSE(exported("pwpc"_4, "InterfaceLib", "YieldToAnyThread"));
}

/* Illustrator 8.0 imports GetComponentVersion (a library it loads) and,
   weakly but calls them unchecked, AppearanceLib's item reference
   constants: left nil, it jumped to 0 at start. */
TEST(Exports, ComponentInstanceCallsAndMenuItemRefCons)
{
    for(const char *name : { "GetComponentVersion", "ComponentFunctionImplemented", "ComponentSetTarget" })
        EXPECT_TRUE(exported("pwpc"_4, "InterfaceLib", name)) << name;
    for(const char *name : { "GetMenuItemRefCon", "SetMenuItemRefCon", "GetMenuItemRefCon2", "SetMenuItemRefCon2" })
        EXPECT_TRUE(exported("pwpc"_4, "AppearanceLib", name)) << name;
}

TEST(Exports, WriteTable)
{
#ifdef EXECUTOR_EXPORTS_OUT
    std::ofstream out(EXECUTOR_EXPORTS_OUT);
    ASSERT_TRUE(out.good());
    out << "{";
    const char *sep = "\n";
    for(auto [arch, key] : { std::pair<OSType, const char *>{ "pwpc"_4, "pwpc" }, { "m68k"_4, "m68k" } })
    {
        out << sep << "  " << json_string(key) << ": {";
        const char *libsep = "\n";
        for(const auto& [library, names] : builtinlibs::exportTable(arch))
        {
            out << libsep << "    " << json_string(library) << ": [";
            for(size_t i = 0; i < names.size(); i++)
                out << (i ? ", " : "") << json_string(names[i]);
            out << "]";
            libsep = ",\n";
        }
        out << "\n  }";
        sep = ",\n";
    }
    out << "\n}\n";
#endif
}
