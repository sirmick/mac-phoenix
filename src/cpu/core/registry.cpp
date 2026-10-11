/*
 * registry.cpp - the cores built into this binary.
 *
 * Each core lives in its own translation unit with its own headers (UAE
 * and MAME's Musashi both define m68k_execute and friends) and exposes
 * one factory here. "musashi" (the vendored v3.32, retired 2026-10-11) is
 * a name for mame-68k now.
 */
#include "cpu_core.h"

namespace cpu {

std::unique_ptr<Core> create_mame_68k(const Config &, GuestMemory &, Host &);  /* src/cpu/mame */
std::unique_ptr<Core> create_mame_ppc(const Config &, GuestMemory &, Host &);  /* src/cpu/mame */
#ifdef CPU_CORE_HAVE_UAE
std::unique_ptr<Core> create_uae(const Config &, GuestMemory &, Host &);
#endif

std::vector<std::string> available(Arch arch)
{
    std::vector<std::string> names;
    if(arch == Arch::M68K)
    {
#ifdef CPU_CORE_HAVE_UAE
        names.push_back("uae");
#endif
        names.push_back("mame-68k");
    }
    if(arch == Arch::PPC)
        names.push_back("mame-ppc");
    return names;
}

std::unique_ptr<Core> create(const std::string &name, const Config &config,
                             GuestMemory &memory, Host &host)
{
    if(name == "mame-68k" || name == "musashi")
        return create_mame_68k(config, memory, host);
    if(name == "mame-ppc")
        return create_mame_ppc(config, memory, host);
#ifdef CPU_CORE_HAVE_UAE
    if(name == "uae")
        return create_uae(config, memory, host);
#endif
    return nullptr;
}

}  // namespace cpu
