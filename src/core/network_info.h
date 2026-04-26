/*
 *  network_info.h - Consolidated network/bridge status file in ExtFS.
 */

#ifndef MAC_PHOENIX_CORE_NETWORK_INFO_H
#define MAC_PHOENIX_CORE_NETWORK_INFO_H

namespace config { struct EmulatorConfig; }

namespace core {

// Writes <extfs[0]>/MacPhoenix/NetworkInfo.txt and, when MITM is enabled,
// copies the MITM root CA into the same subfolder with Finder-Info
// sidecars so it shows up with the right icon in the guest. No-op when
// ExtFS isn't configured.
void write_network_info(const config::EmulatorConfig &cfg);

} // namespace core

#endif // MAC_PHOENIX_CORE_NETWORK_INFO_H
