/*
 *  lwipopts.h - lwIP configuration for mac-phoenix
 *
 *  NO_SYS=1 polled mode. All lwIP calls confined to the network thread.
 *  Used for SLiRP-style userland NAT networking.
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* --- Rename udp_sendto to avoid symbol collision with libjuice --- */
#define udp_sendto lwip_udp_sendto

/* --- Compat: avoid htonl/htons conflict with system headers in C++ --- */
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1

/* --- Threading model --- */
#define NO_SYS                  1
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define SYS_LIGHTWEIGHT_PROT    0

/* --- Protocols --- */
#define LWIP_RAW                1
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_ICMP               1
#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_DHCP               0   /* Mac uses static IP */
#define LWIP_DNS                0   /* DNS proxied at UDP level */
#define LWIP_IGMP               0
#define LWIP_AUTOIP             0

/* --- Memory --- */
#define MEM_SIZE                (64 * 1024)
#define MEM_ALIGNMENT           4
#define MEMP_NUM_PBUF           64
#define MEMP_NUM_TCP_PCB        32
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_TCP_SEG        64
#define MEMP_NUM_UDP_PCB        16
#define MEMP_NUM_RAW_PCB        4
#define MEMP_NUM_REASSDATA      8
#define MEMP_NUM_ARP_QUEUE      16

/* --- Pbuf --- */
#define PBUF_POOL_SIZE          64
#define PBUF_POOL_BUFSIZE       1536
#define ETH_PAD_SIZE            0

/* --- TCP --- */
#define TCP_MSS                 1460
#define TCP_WND                 (8 * TCP_MSS)
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_SND_QUEUELEN        (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_QUEUE_OOSEQ         1
#define LWIP_TCP_KEEPALIVE      1
#define LWIP_TCP_TIMESTAMPS     0
#define TCP_LISTEN_BACKLOG      1

/* --- IP --- */
#define IP_FORWARD              0
#define IP_REASSEMBLY           1
#define IP_FRAG                 1
#define IP_DEFAULT_TTL          64

/* --- Checksums --- */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_ICMP       1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_TCP      1
#define CHECKSUM_CHECK_UDP      1

/* --- Hook for NAT interception --- */
struct pbuf;
struct netif;
int lwip_nat_ip4_input(struct pbuf *p, struct netif *inp);
#define LWIP_HOOK_IP4_INPUT(pbuf, input_netif) lwip_nat_ip4_input(pbuf, input_netif)

/* --- Debugging --- */
#define LWIP_DEBUG              0
#define LWIP_DBG_MIN_LEVEL      LWIP_DBG_LEVEL_ALL
#define LWIP_DBG_TYPES_ON       LWIP_DBG_ON

#define ETHARP_DEBUG            LWIP_DBG_OFF
#define NETIF_DEBUG             LWIP_DBG_OFF
#define PBUF_DEBUG              LWIP_DBG_OFF
#define IP_DEBUG                LWIP_DBG_OFF
#define TCP_DEBUG               LWIP_DBG_OFF
#define UDP_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG              LWIP_DBG_OFF
#define TCP_INPUT_DEBUG          LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG         LWIP_DBG_OFF

/* --- Stats --- */
#define LWIP_STATS              0

/* --- Misc --- */
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_HAVE_LOOPIF            0
#define LWIP_LOOPBACK_MAX_PBUFS     0

#endif /* LWIPOPTS_H */
