/*
 * registry.cpp - the cores built into this binary.
 *
 * Each core lives in its own translation unit with its own headers (UAE
 * and MAME's Musashi both define m68k_execute and friends) and exposes
 * one factory here. "musashi" (the vendored v3.32, retired 2026-10-11) is
 * a name for mame-68k now.
 *
 * The MAME cores are always here. UAE is its own library (cpu_core_uae),
 * linked only into binaries that run the ROM machine; it joins the list
 * when the ROM machine calls register_uae(), so Executor never sees it.
 */
#include "cpu_core.h"

namespace cpu {

std::unique_ptr<Core> create_mame_68k(const Config &, GuestMemory &, Host &);  /* src/cpu/mame */
std::unique_ptr<Core> create_mame_ppc(const Config &, GuestMemory &, Host &);  /* src/cpu/mame */

namespace {
CoreFactory uae_factory;
}

void add_uae(CoreFactory factory)
{
    uae_factory = factory;
}

std::vector<std::string> available(Arch arch)
{
    std::vector<std::string> names;
    if(arch == Arch::M68K)
    {
        if(uae_factory)
            names.push_back("uae");
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
    if(name == "uae" && uae_factory)
        return uae_factory(config, memory, host);
    return nullptr;
}

}  // namespace cpu
