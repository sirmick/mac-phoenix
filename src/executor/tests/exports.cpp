/* MacPhoenix: the libraries' export tables as CFM binds them, PowerPC and
   CFM-68K. Written to EXECUTOR_EXPORTS_OUT (build/executor-exports.json),
   which tools/macdecode/libraries.py reads: what Executor exports is what the
   definitions generate, expanded as the trap macros expand them (LMGet/LMSet
   from lowmem entries, Sync/Async, a trap's flag variants). */

#include "gtest/gtest.h"

#include <base/builtinlibs.h>

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
