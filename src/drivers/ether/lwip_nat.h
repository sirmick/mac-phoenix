/*
 *  lwip_nat.h - NAT proxy for lwIP networking
 *
 *  Intercepts non-local IP packets and proxies TCP/UDP/ICMP through
 *  real host sockets. All functions must be called from the network thread.
 */

#ifndef LWIP_NAT_H
#define LWIP_NAT_H

#include <stdint.h>

struct pbuf;
struct netif;

// Enable verbose network debug logging (set from --debug-network)
extern bool g_debug_network;

// Initialize NAT proxy (call after lwip_init)
void lwip_nat_init(struct netif *netif);

// Shutdown NAT proxy
void lwip_nat_shutdown(void);

// IP4 input hook - intercepts non-local packets for NAT
// Returns 1 if packet was consumed, 0 to let lwIP handle it
// Must be extern "C" because lwIP calls it from C code
#ifdef __cplusplus
extern "C"
#endif
int lwip_nat_ip4_input(struct pbuf *p, struct netif *inp);

// Poll host sockets for incoming data (call from network thread loop)
void lwip_nat_poll(void);

#endif /* LWIP_NAT_H */
