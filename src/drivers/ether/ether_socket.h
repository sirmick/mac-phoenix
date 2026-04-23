/*
 *  ether_socket.h - Unix socket ethernet driver
 *
 *  Connects to a net-bridge process via Unix domain socket.
 *  Ethernet frames are exchanged with 4-byte big-endian length prefix.
 */

#ifndef ETHER_SOCKET_H
#define ETHER_SOCKET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register Unix socket ethernet driver with g_platform.
// sock_path: path to the Unix domain socket to connect to.
void ether_socket_register(const char *sock_path);

// Enable MITM TLS proxy mode. Passed to the auto-launched net-bridge as
// --mitm-tls, --mitm-ports, --mitm-ca-dir. Call before ether_socket_register.
// Pass nullptr for `ports`/`ca_dir` to use net-bridge defaults.
void ether_socket_set_mitm(bool enabled, const char *ports, const char *ca_dir);

// Push a flat ethernet frame to the bridge. Returns true on success.
// Used by the PPC NDRV TX path (AO_transmit_packet → flatten mblk → here).
bool ether_socket_send_raw(const uint8_t *frame, uint32_t len);

// Drain the RX queue, calling `handler` for each pending frame.
// Used by the PPC NDRV RX path (EtherIRQ → drain → ppc_ether_deliver_frame).
typedef void (*ether_socket_frame_handler_t)(const uint8_t *frame, uint32_t len);
void ether_socket_drain_rx(ether_socket_frame_handler_t handler);

// Copy the driver's MAC address into `mac` (6 bytes).
void ether_socket_get_mac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif
