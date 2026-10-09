/*
 * registry.cpp - the cores built into this binary.
 *
 * Each core lives in its own translation unit with its own headers (UAE
 * and Musashi both define m68k_execute and friends) and exposes one
 * factory here.
 */
#include "cpu_core.h"

namespace cpu {

std::unique_ptr<Core> create_musashi(const Config &, GuestMemory &, Host &);
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
        names.push_back("musashi");
    }
    return names;
}

std::unique_ptr<Core> create(const std::string &name, const Config &config,
                             GuestMemory &memory, Host &host)
{
    if(name == "musashi")
        return create_musashi(config, memory, host);
#ifdef CPU_CORE_HAVE_UAE
    if(name == "uae")
        return create_uae(config, memory, host);
#endif
    return nullptr;
}

}  // namespace cpu
