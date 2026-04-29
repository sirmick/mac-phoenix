/*
 *  network_info.h - Consolidated network/bridge status file in ExtFS.
 */

#ifndef MAC_PHOENIX_CORE_NETWORK_INFO_H
#define MAC_PHOENIX_CORE_NETWORK_INFO_H

namespace config { struct EmulatorConfig; }

namespace core {

// Writes <extfs[0]>/MacPhoenix/NetworkInfo.txt + netcfg.txt with the
// host gateway, DHCP lease, BridgeAgent status, and host/net-bridge
// build stamps. No-op when ExtFS isn't configured.
void write_network_info(const config::EmulatorConfig &cfg);

} // namespace core

#endif // MAC_PHOENIX_CORE_NETWORK_INFO_H
