/*
 *  lwip_build.c - Single compilation unit for lwIP
 *
 *  Includes all required lwIP source files as a unity build.
 *  This avoids meson subproject sandbox issues.
 */

/* Core */
#include "core/init.c"
#include "core/def.c"
#include "core/mem.c"
#include "core/memp.c"
#include "core/pbuf.c"
#include "core/netif.c"
#include "core/ip.c"
#include "core/raw.c"
#include "core/tcp.c"
#include "core/tcp_in.c"
#include "core/tcp_out.c"
#include "core/udp.c"
#include "core/inet_chksum.c"
#include "core/timeouts.c"
#include "core/sys.c"
#include "core/stats.c"

/* IPv4 */
#include "core/ipv4/etharp.c"
#include "core/ipv4/ip4.c"
#include "core/ipv4/ip4_addr.c"
#include "core/ipv4/ip4_frag.c"
#include "core/ipv4/icmp.c"

/* Netif */
#include "netif/ethernet.c"
