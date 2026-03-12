/*
 *  lwip_nat.cpp - NAT proxy for lwIP networking
 *
 *  Intercepts IP packets destined for external hosts and proxies them
 *  through real host sockets. Handles TCP, UDP, and ICMP.
 *
 *  All functions are called exclusively from the network thread.
 *  No thread safety concerns within this file.
 */

#include "lwip_nat.h"

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/inet_chksum.h"
#include "lwip/etharp.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/icmp.h"
#include "lwip/sys.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/errqueue.h>
#include <netinet/ip.h>
#include <fstream>
#include <string>

bool g_debug_network = false;

// Gateway IP (lwIP's own address)
static ip_addr_t s_gw_ip;
static struct netif *s_netif = nullptr;

// Host's real DNS server (read from /etc/resolv.conf)
static uint32_t s_host_dns_ip = 0;  // network byte order

// Virtual DNS address the Mac uses (must be the gateway so ARP resolves)
#define NAT_DNS_IP  "10.0.2.1"

// ---- DNS resolver lookup ----

static uint32_t resolve_host_dns(void)
{
	std::ifstream f("/etc/resolv.conf");
	std::string line;
	while (std::getline(f, line)) {
		if (line.compare(0, 10, "nameserver") == 0) {
			size_t pos = line.find_first_not_of(" \t", 10);
			if (pos != std::string::npos) {
				std::string ip = line.substr(pos);
				// Skip localhost entries (systemd-resolved stub)
				if (ip == "127.0.0.53" || ip == "127.0.0.1")
					continue;
				struct in_addr addr;
				if (inet_aton(ip.c_str(), &addr))
					return addr.s_addr;
			}
		}
	}
	// Fallback: try Google DNS
	struct in_addr fallback;
	inet_aton("8.8.8.8", &fallback);
	return fallback.s_addr;
}

// ---- Helper: set fd non-blocking ----

static void set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ---- ICMP Time Exceeded generator (for traceroute) ----

// Send ICMP Time Exceeded (type 11, code 0) back to Mac
// Payload: original IP header + first 8 bytes of original data (per RFC 792)
static void send_icmp_time_exceeded(struct pbuf *orig_p, const ip_addr_t *src_ip)
{
	// Extract original IP header + 8 bytes of payload
	uint8_t orig_data[28 + 8];  // max IP header (60) + 8, but we use 20+8 typically
	uint16_t ip_hdr_len = 20;  // assume standard header
	if (orig_p->len >= 1) {
		uint8_t ihl;
		pbuf_copy_partial(orig_p, &ihl, 1, 0);
		ip_hdr_len = (ihl & 0x0F) * 4;
	}
	uint16_t copy_len = ip_hdr_len + 8;
	if (copy_len > sizeof(orig_data)) copy_len = sizeof(orig_data);
	if (copy_len > orig_p->tot_len) copy_len = orig_p->tot_len;
	pbuf_copy_partial(orig_p, orig_data, copy_len, 0);

	// Build ICMP Time Exceeded message: type(1) + code(1) + checksum(2) + unused(4) + data
	uint16_t icmp_len = 8 + copy_len;
	uint8_t icmp_msg[8 + 60 + 8];
	memset(icmp_msg, 0, sizeof(icmp_msg));
	icmp_msg[0] = 11;   // Type: Time Exceeded
	icmp_msg[1] = 0;    // Code: TTL exceeded in transit
	// checksum at [2..3], computed below
	// unused at [4..7] = 0
	memcpy(&icmp_msg[8], orig_data, copy_len);

	// Compute ICMP checksum
	uint32_t cksum = 0;
	for (uint16_t i = 0; i < icmp_len; i += 2) {
		uint16_t word = (icmp_msg[i] << 8);
		if (i + 1 < icmp_len) word |= icmp_msg[i + 1];
		cksum += word;
	}
	while (cksum >> 16)
		cksum = (cksum & 0xFFFF) + (cksum >> 16);
	uint16_t icmp_cksum = ~(uint16_t)cksum;
	icmp_msg[2] = (icmp_cksum >> 8) & 0xFF;
	icmp_msg[3] = icmp_cksum & 0xFF;

	// Send via raw PCB from the gateway IP
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, icmp_len, PBUF_RAM);
	if (!p) return;
	memcpy(p->payload, icmp_msg, icmp_len);

	struct raw_pcb *tmp = raw_new(IP_PROTO_ICMP);
	if (tmp) {
		raw_bind(tmp, &s_gw_ip);  // Source = gateway (10.0.2.1)
		raw_sendto(tmp, p, src_ip);  // Dest = Mac
		raw_remove(tmp);
	}
	pbuf_free(p);

	if (g_debug_network) {
		char src_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &src_ip->addr, src_str, sizeof(src_str));
		fprintf(stderr, "[lwIP NAT] ICMP Time Exceeded -> %s (TTL expired at gateway)\n", src_str);
	}
}

// ---- TCP Proxy ----

struct TcpProxy {
	struct tcp_pcb *pcb;        // lwIP PCB (Mac-facing)
	int host_fd;                // Host socket
	ip_addr_t orig_dst_ip;     // Original destination IP
	uint16_t orig_dst_port;    // Original destination port
	bool host_connected;
	std::vector<uint8_t> pending_to_host;  // Buffered before connect completes
	bool closing;
	TcpProxy *next;
};

static TcpProxy *s_tcp_proxies = nullptr;

static void tcp_proxy_destroy(TcpProxy *proxy)
{
	// Unlink from list
	TcpProxy **pp = &s_tcp_proxies;
	while (*pp) {
		if (*pp == proxy) {
			*pp = proxy->next;
			break;
		}
		pp = &(*pp)->next;
	}

	if (proxy->pcb) {
		tcp_arg(proxy->pcb, nullptr);
		tcp_recv(proxy->pcb, nullptr);
		tcp_err(proxy->pcb, nullptr);
		tcp_sent(proxy->pcb, nullptr);
		tcp_abort(proxy->pcb);
	}
	if (proxy->host_fd >= 0)
		close(proxy->host_fd);
	delete proxy;
}

// Forward data from host socket to lwIP (Mac)
static void tcp_proxy_host_to_mac(TcpProxy *proxy)
{
	if (!proxy->pcb || proxy->closing) return;

	uint16_t space = tcp_sndbuf(proxy->pcb);
	if (space == 0) return;

	uint8_t buf[4096];
	int to_read = (space < sizeof(buf)) ? space : sizeof(buf);
	ssize_t n = recv(proxy->host_fd, buf, to_read, 0);

	if (n > 0) {
		err_t err = tcp_write(proxy->pcb, buf, n, TCP_WRITE_FLAG_COPY);
		if (err == ERR_OK) {
			tcp_output(proxy->pcb);
		} else {
			fprintf(stderr, "[lwIP NAT] tcp_write error: %d\n", err);
			proxy->closing = true;
		}
	} else if (n == 0) {
		// Host closed connection
		if (proxy->pcb) {
			tcp_shutdown(proxy->pcb, 0, 1);  // half-close write side
		}
		proxy->closing = true;
	} else if (errno != EAGAIN && errno != EWOULDBLOCK) {
		proxy->closing = true;
	}
}

// lwIP callback: data received from Mac
static err_t tcp_proxy_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	TcpProxy *proxy = (TcpProxy *)arg;
	if (!proxy) {
		if (p) pbuf_free(p);
		return ERR_ABRT;
	}

	if (!p || err != ERR_OK) {
		// Mac closed connection
		if (proxy->host_fd >= 0) {
			shutdown(proxy->host_fd, SHUT_WR);
		}
		proxy->closing = true;
		if (p) pbuf_free(p);
		return ERR_OK;
	}

	// Forward to host socket
	uint16_t total = p->tot_len;
	uint8_t buf[65535];
	uint16_t copied = pbuf_copy_partial(p, buf, total, 0);
	tcp_recved(pcb, total);
	pbuf_free(p);

	if (!proxy->host_connected) {
		// Buffer until connect completes (cap at 256KB to prevent runaway)
		if (proxy->pending_to_host.size() < 256 * 1024) {
			proxy->pending_to_host.insert(proxy->pending_to_host.end(), buf, buf + copied);
		}
		return ERR_OK;
	}

	ssize_t sent = send(proxy->host_fd, buf, copied, MSG_NOSIGNAL);
	if (sent < 0 && errno != EAGAIN) {
		proxy->closing = true;
	}
	return ERR_OK;
}

// lwIP callback: error on PCB
static void tcp_proxy_err(void *arg, err_t err)
{
	TcpProxy *proxy = (TcpProxy *)arg;
	if (!proxy) return;
	(void)err;

	// PCB is already freed by lwIP when this is called
	proxy->pcb = nullptr;
	proxy->closing = true;
}

// lwIP callback: sent data acknowledged
static err_t tcp_proxy_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	(void)arg;
	(void)pcb;
	(void)len;
	return ERR_OK;
}

// Create TCP proxy for a new connection
static TcpProxy *tcp_proxy_create(struct tcp_pcb *pcb, const ip_addr_t *dst_ip, uint16_t dst_port)
{
	// Open host socket
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "[lwIP NAT] TCP socket() failed: %s\n", strerror(errno));
		return nullptr;
	}
	set_nonblocking(fd);

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = dst_ip->addr;
	sa.sin_port = htons(dst_port);

	int ret = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	bool connected = (ret == 0);
	if (ret < 0 && errno != EINPROGRESS) {
		fprintf(stderr, "[lwIP NAT] TCP connect failed: %s\n", strerror(errno));
		close(fd);
		return nullptr;
	}

	TcpProxy *proxy = new TcpProxy();
	proxy->pcb = pcb;
	proxy->host_fd = fd;
	proxy->orig_dst_ip = *dst_ip;
	proxy->orig_dst_port = dst_port;
	proxy->host_connected = connected;
	proxy->closing = false;
	proxy->next = s_tcp_proxies;
	s_tcp_proxies = proxy;

	tcp_arg(pcb, proxy);
	tcp_recv(pcb, tcp_proxy_recv);
	tcp_err(pcb, tcp_proxy_err);
	tcp_sent(pcb, tcp_proxy_sent);

	if (g_debug_network) {
		char dst_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &dst_ip->addr, dst_str, sizeof(dst_str));
		fprintf(stderr, "[lwIP NAT] TCP connect -> %s:%u (fd=%d)\n", dst_str, dst_port, fd);
	}

	return proxy;
}

// ---- UDP Proxy ----

struct UdpProxy {
	int host_fd;
	ip_addr_t mac_src_ip;
	uint16_t mac_src_port;
	ip_addr_t orig_dst_ip;
	uint16_t orig_dst_port;
	uint8_t ttl;                // IP TTL from last packet (for traceroute)
	uint32_t last_activity;     // sys_now() timestamp
	UdpProxy *next;
};

static UdpProxy *s_udp_proxies = nullptr;
static struct udp_pcb *s_udp_catchall = nullptr;

static UdpProxy *udp_proxy_find(uint16_t src_port, const ip_addr_t *dst_ip, uint16_t dst_port)
{
	for (UdpProxy *p = s_udp_proxies; p; p = p->next) {
		if (p->mac_src_port == src_port &&
		    p->orig_dst_port == dst_port &&
		    ip_addr_cmp(&p->orig_dst_ip, dst_ip))
			return p;
	}
	return nullptr;
}

static void udp_proxy_destroy(UdpProxy *proxy)
{
	UdpProxy **pp = &s_udp_proxies;
	while (*pp) {
		if (*pp == proxy) {
			*pp = proxy->next;
			break;
		}
		pp = &(*pp)->next;
	}
	if (proxy->host_fd >= 0)
		close(proxy->host_fd);
	delete proxy;
}

// Forward reply from host back to Mac via lwIP
static void udp_proxy_host_to_mac(UdpProxy *proxy)
{
	uint8_t buf[2048];
	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	ssize_t n = recvfrom(proxy->host_fd, buf, sizeof(buf), 0,
	                     (struct sockaddr *)&from, &fromlen);
	if (n <= 0) return;
	if (g_debug_network) {
		bool is_dns = (proxy->orig_dst_port == 53);
		fprintf(stderr, "[lwIP NAT] UDP %sreply: %zd bytes -> Mac port %u\n",
			is_dns ? "DNS " : "", n, proxy->mac_src_port);
	}

	// Create pbuf and send back through lwIP to Mac
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
	if (!p) return;
	memcpy(p->payload, buf, n);

	// Reply comes from the original destination IP/port
	// Create a temporary PCB bound to the correct source port so the Mac
	// sees the reply from the right address
	struct udp_pcb *reply_pcb = udp_new();
	if (reply_pcb) {
		udp_bind(reply_pcb, &proxy->orig_dst_ip, proxy->orig_dst_port);
		udp_sendto(reply_pcb, p, &proxy->mac_src_ip, proxy->mac_src_port);
		udp_remove(reply_pcb);
	}
	pbuf_free(p);
}

// Check for ICMP errors (Time Exceeded, Unreachable) on UDP proxy sockets
// Used for traceroute: intermediate routers send ICMP errors when TTL expires
static void check_udp_icmp_errors(UdpProxy *proxy)
{
	char cbuf[512];
	struct iovec iov;
	uint8_t data[1];
	iov.iov_base = data;
	iov.iov_len = sizeof(data);

	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof(cbuf);

	ssize_t n = recvmsg(proxy->host_fd, &msg, MSG_ERRQUEUE | MSG_DONTWAIT);
	if (n < 0) return;

	// Parse control messages for the ICMP error
	for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_IP || cmsg->cmsg_type != IP_RECVERR)
			continue;

		struct sock_extended_err *ee = (struct sock_extended_err *)CMSG_DATA(cmsg);
		if (ee->ee_origin != SO_EE_ORIGIN_ICMP)
			continue;

		// ee_type=11 (Time Exceeded), ee_type=3 (Dest Unreachable)
		struct sockaddr_in *from = (struct sockaddr_in *)SO_EE_OFFENDER(ee);

		if (g_debug_network) {
			char from_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &from->sin_addr, from_str, sizeof(from_str));
			fprintf(stderr, "[lwIP NAT] ICMP error from %s: type=%u code=%u\n",
				from_str, ee->ee_type, ee->ee_code);
		}

		if (ee->ee_type == 11) {
			// Time Exceeded — traceroute hop response
			// Build ICMP Time Exceeded with the offending router as source
			// We need to fabricate the original IP+UDP header that triggered it

			// Build a fake original IP header for the ICMP payload
			uint8_t orig_ip[28];  // IP(20) + UDP first 8 bytes
			memset(orig_ip, 0, sizeof(orig_ip));
			orig_ip[0] = 0x45;  // IPv4, IHL=5
			orig_ip[1] = 0;
			// total length = 20 + 8 (we only include 8 bytes of UDP)
			orig_ip[2] = 0; orig_ip[3] = 28;
			orig_ip[8] = 1;  // TTL (was 1 when it expired)
			orig_ip[9] = IP_PROTO_UDP;
			// src = Mac IP
			memcpy(&orig_ip[12], &proxy->mac_src_ip.addr, 4);
			// dst = original destination
			memcpy(&orig_ip[16], &proxy->orig_dst_ip.addr, 4);
			// UDP: src port, dst port
			orig_ip[20] = (proxy->mac_src_port >> 8) & 0xFF;
			orig_ip[21] = proxy->mac_src_port & 0xFF;
			orig_ip[22] = (proxy->orig_dst_port >> 8) & 0xFF;
			orig_ip[23] = proxy->orig_dst_port & 0xFF;

			// Build ICMP Time Exceeded: type(1) + code(1) + cksum(2) + unused(4) + data(28)
			uint16_t icmp_len = 8 + 28;
			uint8_t icmp_msg[36];
			memset(icmp_msg, 0, sizeof(icmp_msg));
			icmp_msg[0] = 11;  // Time Exceeded
			icmp_msg[1] = 0;   // TTL expired
			memcpy(&icmp_msg[8], orig_ip, 28);

			// Checksum
			uint32_t cksum = 0;
			for (uint16_t i = 0; i < icmp_len; i += 2) {
				uint16_t word = (icmp_msg[i] << 8);
				if (i + 1 < icmp_len) word |= icmp_msg[i + 1];
				cksum += word;
			}
			while (cksum >> 16)
				cksum = (cksum & 0xFFFF) + (cksum >> 16);
			uint16_t icmp_cksum = ~(uint16_t)cksum;
			icmp_msg[2] = (icmp_cksum >> 8) & 0xFF;
			icmp_msg[3] = icmp_cksum & 0xFF;

			// Send from the intermediate router's IP to the Mac
			struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, icmp_len, PBUF_RAM);
			if (p) {
				memcpy(p->payload, icmp_msg, icmp_len);
				ip_addr_t router_ip;
				router_ip.addr = from->sin_addr.s_addr;
				struct raw_pcb *tmp = raw_new(IP_PROTO_ICMP);
				if (tmp) {
					raw_bind(tmp, &router_ip);
					raw_sendto(tmp, p, &proxy->mac_src_ip);
					raw_remove(tmp);
				}
				pbuf_free(p);
			}

			if (g_debug_network) {
				char from_str[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &from->sin_addr, from_str, sizeof(from_str));
				fprintf(stderr, "[lwIP NAT] Traceroute hop: %s (TTL exceeded) -> Mac\n", from_str);
			}
		} else if (ee->ee_type == 3) {
			// Destination Unreachable — forward to Mac
			uint8_t icmp_msg[36];
			memset(icmp_msg, 0, sizeof(icmp_msg));
			icmp_msg[0] = 3;           // Dest Unreachable
			icmp_msg[1] = ee->ee_code; // Port unreachable, etc.

			// Include original IP+UDP header
			uint8_t orig_ip[28];
			memset(orig_ip, 0, sizeof(orig_ip));
			orig_ip[0] = 0x45;
			orig_ip[2] = 0; orig_ip[3] = 28;
			orig_ip[8] = proxy->ttl;
			orig_ip[9] = IP_PROTO_UDP;
			memcpy(&orig_ip[12], &proxy->mac_src_ip.addr, 4);
			memcpy(&orig_ip[16], &proxy->orig_dst_ip.addr, 4);
			orig_ip[20] = (proxy->mac_src_port >> 8) & 0xFF;
			orig_ip[21] = proxy->mac_src_port & 0xFF;
			orig_ip[22] = (proxy->orig_dst_port >> 8) & 0xFF;
			orig_ip[23] = proxy->orig_dst_port & 0xFF;
			memcpy(&icmp_msg[8], orig_ip, 28);

			uint16_t icmp_len = 36;
			uint32_t cksum = 0;
			for (uint16_t i = 0; i < icmp_len; i += 2) {
				uint16_t word = (icmp_msg[i] << 8);
				if (i + 1 < icmp_len) word |= icmp_msg[i + 1];
				cksum += word;
			}
			while (cksum >> 16)
				cksum = (cksum & 0xFFFF) + (cksum >> 16);
			uint16_t icmp_cksum = ~(uint16_t)cksum;
			icmp_msg[2] = (icmp_cksum >> 8) & 0xFF;
			icmp_msg[3] = icmp_cksum & 0xFF;

			struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, icmp_len, PBUF_RAM);
			if (p) {
				memcpy(p->payload, icmp_msg, icmp_len);
				ip_addr_t router_ip;
				router_ip.addr = from->sin_addr.s_addr;
				struct raw_pcb *tmp = raw_new(IP_PROTO_ICMP);
				if (tmp) {
					raw_bind(tmp, &router_ip);
					raw_sendto(tmp, p, &proxy->mac_src_ip);
					raw_remove(tmp);
				}
				pbuf_free(p);
			}

			if (g_debug_network) {
				char from_str[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &from->sin_addr, from_str, sizeof(from_str));
				fprintf(stderr, "[lwIP NAT] ICMP Unreachable from %s code=%u -> Mac\n",
					from_str, ee->ee_code);
			}
		}
	}
}

// ---- ICMP Proxy ----

struct IcmpProxy {
	int host_fd;
	ip_addr_t mac_src_ip;
	ip_addr_t orig_dst_ip;
	uint16_t id;                // ICMP identifier
	uint32_t last_activity;
	IcmpProxy *next;
};

static IcmpProxy *s_icmp_proxies = nullptr;

static void icmp_proxy_host_to_mac(IcmpProxy *proxy)
{
	uint8_t buf[2048];
	ssize_t n = recv(proxy->host_fd, buf, sizeof(buf), 0);
	if (n < 8) return;  // Need at least ICMP header

	// Linux SOCK_DGRAM/IPPROTO_ICMP returns the ICMP payload with the kernel's
	// own id substituted. Restore the original ICMP id so the Mac matches it.
	buf[4] = (proxy->id >> 8) & 0xFF;
	buf[5] = proxy->id & 0xFF;

	// Recompute ICMP checksum (covers entire ICMP message)
	buf[2] = 0;
	buf[3] = 0;
	uint32_t cksum = 0;
	for (ssize_t i = 0; i < n; i += 2) {
		uint16_t word = (buf[i] << 8);
		if (i + 1 < n) word |= buf[i + 1];
		cksum += word;
	}
	while (cksum >> 16)
		cksum = (cksum & 0xFFFF) + (cksum >> 16);
	uint16_t icmp_cksum = ~(uint16_t)cksum;
	buf[2] = (icmp_cksum >> 8) & 0xFF;
	buf[3] = icmp_cksum & 0xFF;

	if (g_debug_network) {
		char dst_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &proxy->orig_dst_ip.addr, dst_str, sizeof(dst_str));
		uint16_t seq = (buf[6] << 8) | buf[7];
		fprintf(stderr, "[lwIP NAT] ICMP echo reply <- %s id=%u seq=%u (%zd bytes)\n",
			dst_str, proxy->id, seq, n);
	}

	// Build pbuf and send via raw IP back to Mac
	// Source must be the original ping destination (e.g. 8.8.8.8)
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
	if (!p) return;
	memcpy(p->payload, buf, n);

	struct raw_pcb *tmp = raw_new(IP_PROTO_ICMP);
	if (tmp) {
		raw_bind(tmp, &proxy->orig_dst_ip);  // Source = original target
		raw_sendto(tmp, p, &proxy->mac_src_ip);  // Dest = Mac
		raw_remove(tmp);
	}
	pbuf_free(p);
}

// ---- NAT table for IP header rewriting ----
// Maps (protocol, src_port) -> original destination

struct NatEntry {
	uint8_t proto;          // IPPROTO_TCP or IPPROTO_UDP
	uint16_t src_port;
	ip_addr_t orig_dst_ip;
	uint16_t orig_dst_port;
	ip_addr_t orig_src_ip;
	uint8_t ttl;            // Original IP TTL (for traceroute)
	NatEntry *next;
};

static NatEntry *s_nat_table = nullptr;

static NatEntry *nat_find(uint8_t proto, uint16_t src_port)
{
	for (NatEntry *e = s_nat_table; e; e = e->next) {
		if (e->proto == proto && e->src_port == src_port)
			return e;
	}
	return nullptr;
}

static NatEntry *nat_add(uint8_t proto, uint16_t src_port,
                         const ip_addr_t *dst_ip, uint16_t dst_port,
                         const ip_addr_t *src_ip, uint8_t ip_ttl = 64)
{
	NatEntry *e = new NatEntry();
	e->proto = proto;
	e->src_port = src_port;
	e->orig_dst_ip = *dst_ip;
	e->orig_dst_port = dst_port;
	e->orig_src_ip = *src_ip;
	e->ttl = ip_ttl;
	e->next = s_nat_table;
	s_nat_table = e;
	return e;
}

static void nat_remove(uint8_t proto, uint16_t src_port)
{
	NatEntry **pp = &s_nat_table;
	while (*pp) {
		if ((*pp)->proto == proto && (*pp)->src_port == src_port) {
			NatEntry *e = *pp;
			*pp = e->next;
			delete e;
			return;
		}
		pp = &(*pp)->next;
	}
}

// ---- TCP listener for NAT'd connections ----

static struct tcp_pcb *s_tcp_listen_pcb = nullptr;

static err_t tcp_nat_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	(void)arg;
	if (err != ERR_OK || !newpcb) return ERR_ABRT;

	// Look up original destination from NAT table
	NatEntry *entry = nat_find(IP_PROTO_TCP, newpcb->remote_port);
	if (!entry) {
		fprintf(stderr, "[lwIP NAT] TCP accept but no NAT entry for port %u\n",
			newpcb->remote_port);
		tcp_abort(newpcb);
		return ERR_ABRT;
	}

	ip_addr_t dst_ip = entry->orig_dst_ip;
	uint16_t dst_port = entry->orig_dst_port;

	TcpProxy *proxy = tcp_proxy_create(newpcb, &dst_ip, dst_port);
	if (!proxy) {
		tcp_abort(newpcb);
		return ERR_ABRT;
	}

	return ERR_OK;
}

// ---- UDP recv callback for NAT ----

static void udp_nat_recv(void *arg, struct udp_pcb * /*pcb*/, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port)
{
	(void)arg;
	if (!p) return;

	// Look up original destination from NAT table
	NatEntry *entry = nat_find(IP_PROTO_UDP, port);
	ip_addr_t dst_ip;
	uint16_t dst_port;

	if (entry) {
		dst_ip = entry->orig_dst_ip;
		dst_port = entry->orig_dst_port;
	} else {
		if (g_debug_network)
			fprintf(stderr, "[lwIP NAT] UDP dropped: no NAT entry for port %u\n", port);
		pbuf_free(p);
		return;
	}

	// Check for DNS and redirect to host's real DNS
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;

	ip_addr_t dns_ip;
	inet_pton(AF_INET, NAT_DNS_IP, &dns_ip.addr);
	if (ip_addr_cmp(&dst_ip, &dns_ip) && dst_port == 53) {
		sa.sin_addr.s_addr = s_host_dns_ip;
	} else {
		sa.sin_addr.s_addr = dst_ip.addr;
	}
	sa.sin_port = htons(dst_port);

	// Look up TTL from NAT entry (for traceroute)
	uint8_t ip_ttl = entry->ttl;

	// Find or create proxy
	UdpProxy *proxy = udp_proxy_find(port, &dst_ip, dst_port);
	if (!proxy) {
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0) {
			pbuf_free(p);
			return;
		}
		set_nonblocking(fd);

		// Enable ICMP error reporting (for traceroute TTL exceeded)
		int val = 1;
		setsockopt(fd, SOL_IP, IP_RECVERR, &val, sizeof(val));

		proxy = new UdpProxy();
		proxy->host_fd = fd;
		proxy->mac_src_ip = *addr;
		proxy->mac_src_port = port;
		proxy->orig_dst_ip = dst_ip;
		proxy->orig_dst_port = dst_port;
		proxy->ttl = ip_ttl;
		proxy->last_activity = 0;
		proxy->next = s_udp_proxies;
		s_udp_proxies = proxy;
	}

	// Set TTL on outgoing packet (decremented by 1 for the gateway hop)
	int send_ttl = (ip_ttl > 1) ? (ip_ttl - 1) : 1;
	setsockopt(proxy->host_fd, IPPROTO_IP, IP_TTL, &send_ttl, sizeof(send_ttl));
	proxy->ttl = ip_ttl;

	// Copy pbuf to linear buffer and send to host
	uint8_t buf[2048];
	uint16_t len = pbuf_copy_partial(p, buf, p->tot_len, 0);
	pbuf_free(p);

	ssize_t sent = sendto(proxy->host_fd, buf, len, 0, (struct sockaddr *)&sa, sizeof(sa));
	if (g_debug_network) {
		char sa_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &sa.sin_addr, sa_str, sizeof(sa_str));
		bool is_dns = (ntohs(sa.sin_port) == 53);
		fprintf(stderr, "[lwIP NAT] UDP %s-> %s:%u (%u bytes)%s\n",
			is_dns ? "DNS " : "",
			sa_str, ntohs(sa.sin_port), len,
			sent < 0 ? " FAILED" : "");
		if (sent < 0)
			fprintf(stderr, "[lwIP NAT] sendto error: %s\n", strerror(errno));
	}
}

// ---- DHCP server ----
// Minimal DHCP server: responds to DISCOVER with OFFER, REQUEST with ACK
// Assigns 10.0.2.15 with gateway 10.0.2.1, DNS 10.0.2.3, subnet 255.255.255.0

// Mac's ethernet address (must match ether_lwip.cpp)
static const uint8_t s_mac_client_addr[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x01};
static const uint8_t s_gw_mac_addr[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x02};

// DHCP message types
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

// Returns 1 if this was a DHCP packet we handled, 0 otherwise
static int handle_dhcp(struct pbuf *p, uint16_t ip_hdr_len)
{
	// Need at least IP + UDP(8) + BOOTP(236) + magic cookie(4)
	if (p->tot_len < ip_hdr_len + 8 + 236 + 4)
		return 0;

	uint8_t pkt[1500];
	uint16_t pkt_len = pbuf_copy_partial(p, pkt, sizeof(pkt), 0);

	// Check UDP ports: src=68 (client), dst=67 (server)
	uint16_t src_port = (pkt[ip_hdr_len] << 8) | pkt[ip_hdr_len + 1];
	uint16_t dst_port = (pkt[ip_hdr_len + 2] << 8) | pkt[ip_hdr_len + 3];
	if (src_port != 68 || dst_port != 67)
		return 0;

	uint16_t bootp_off = ip_hdr_len + 8;  // UDP header is 8 bytes

	// Check BOOTP magic cookie at offset 236
	if (pkt_len < bootp_off + 240)
		return 0;
	if (pkt[bootp_off + 236] != 99 || pkt[bootp_off + 237] != 130 ||
	    pkt[bootp_off + 238] != 83 || pkt[bootp_off + 239] != 99)
		return 0;

	// Find DHCP message type (option 53)
	uint8_t msg_type = 0;
	uint16_t opt_off = bootp_off + 240;
	while (opt_off + 2 <= pkt_len) {
		uint8_t opt = pkt[opt_off];
		if (opt == 0xFF) break;  // End
		if (opt == 0) { opt_off++; continue; }  // Pad
		uint8_t opt_len = pkt[opt_off + 1];
		if (opt == 53 && opt_len >= 1)
			msg_type = pkt[opt_off + 2];
		opt_off += 2 + opt_len;
	}

	if (msg_type != DHCP_DISCOVER && msg_type != DHCP_REQUEST)
		return 0;

	uint8_t reply_type = (msg_type == DHCP_DISCOVER) ? DHCP_OFFER : DHCP_ACK;
	fprintf(stderr, "[lwIP DHCP] %s -> sending %s\n",
		msg_type == DHCP_DISCOVER ? "DISCOVER" : "REQUEST",
		reply_type == DHCP_OFFER ? "OFFER" : "ACK");

	// Extract client's transaction ID (xid) from BOOTP header
	uint32_t xid;
	memcpy(&xid, &pkt[bootp_off + 4], 4);

	// Extract client's MAC from BOOTP chaddr field
	uint8_t client_mac[6];
	memcpy(client_mac, &pkt[bootp_off + 28], 6);

	// Build DHCP reply
	// Ethernet(14) + IP(20) + UDP(8) + BOOTP(236) + options
	uint8_t reply[600];
	memset(reply, 0, sizeof(reply));
	uint16_t roff = 0;

	// Ethernet header
	memcpy(&reply[0], client_mac, 6);        // dst MAC
	memcpy(&reply[6], s_gw_mac_addr, 6);     // src MAC
	reply[12] = 0x08; reply[13] = 0x00;      // ethertype IP
	roff = 14;

	// IP header (filled in later for checksum)
	uint16_t ip_off = roff;
	roff += 20;

	// UDP header (filled in later for length)
	uint16_t udp_off = roff;
	roff += 8;

	// BOOTP reply
	uint16_t bootp_start = roff;
	reply[roff++] = 2;    // op: BOOTREPLY
	reply[roff++] = 1;    // htype: ethernet
	reply[roff++] = 6;    // hlen: 6
	reply[roff++] = 0;    // hops
	memcpy(&reply[roff], &xid, 4); roff += 4;  // xid
	roff += 2;  // secs = 0
	roff += 2;  // flags = 0

	roff += 4;  // ciaddr = 0
	// yiaddr: 10.0.2.15
	reply[roff++] = 10; reply[roff++] = 0; reply[roff++] = 2; reply[roff++] = 15;
	// siaddr: 10.0.2.1 (server)
	reply[roff++] = 10; reply[roff++] = 0; reply[roff++] = 2; reply[roff++] = 1;
	roff += 4;  // giaddr = 0

	// chaddr (client MAC + padding to 16 bytes)
	memcpy(&reply[roff], client_mac, 6); roff += 16;
	roff += 64;  // sname
	roff += 128; // file

	// DHCP magic cookie
	reply[roff++] = 99; reply[roff++] = 130; reply[roff++] = 83; reply[roff++] = 99;

	// Option 53: DHCP message type
	reply[roff++] = 53; reply[roff++] = 1; reply[roff++] = reply_type;

	// Option 54: Server identifier (10.0.2.1)
	reply[roff++] = 54; reply[roff++] = 4;
	reply[roff++] = 10; reply[roff++] = 0; reply[roff++] = 2; reply[roff++] = 1;

	// Option 51: Lease time (86400 seconds = 1 day)
	reply[roff++] = 51; reply[roff++] = 4;
	reply[roff++] = 0; reply[roff++] = 1; reply[roff++] = 0x51; reply[roff++] = 0x80;

	// Option 1: Subnet mask
	reply[roff++] = 1; reply[roff++] = 4;
	reply[roff++] = 255; reply[roff++] = 255; reply[roff++] = 255; reply[roff++] = 0;

	// Option 3: Router (gateway)
	reply[roff++] = 3; reply[roff++] = 4;
	reply[roff++] = 10; reply[roff++] = 0; reply[roff++] = 2; reply[roff++] = 1;

	// Option 6: DNS server (use gateway IP so ARP resolves)
	reply[roff++] = 6; reply[roff++] = 4;
	reply[roff++] = 10; reply[roff++] = 0; reply[roff++] = 2; reply[roff++] = 1;

	// End option
	reply[roff++] = 0xFF;

	// Pad to minimum BOOTP size (236 + 64 options minimum)
	while (roff < bootp_start + 300) reply[roff++] = 0;

	// Fill in UDP header
	uint16_t udp_len = roff - udp_off;
	reply[udp_off] = 0; reply[udp_off + 1] = 67;      // src port 67
	reply[udp_off + 2] = 0; reply[udp_off + 3] = 68;  // dst port 68
	reply[udp_off + 4] = udp_len >> 8; reply[udp_off + 5] = udp_len & 0xFF;
	reply[udp_off + 6] = 0; reply[udp_off + 7] = 0;   // checksum (0 = disabled)

	// Fill in IP header
	uint16_t ip_total_len = roff - ip_off;
	reply[ip_off] = 0x45;  // version 4, IHL 5
	reply[ip_off + 1] = 0; // DSCP/ECN
	reply[ip_off + 2] = ip_total_len >> 8; reply[ip_off + 3] = ip_total_len & 0xFF;
	reply[ip_off + 4] = 0; reply[ip_off + 5] = 0;  // identification
	reply[ip_off + 6] = 0; reply[ip_off + 7] = 0;  // flags/fragment
	reply[ip_off + 8] = 64;  // TTL
	reply[ip_off + 9] = 17;  // protocol: UDP
	reply[ip_off + 10] = 0; reply[ip_off + 11] = 0; // checksum (computed below)
	// src: 10.0.2.1
	reply[ip_off + 12] = 10; reply[ip_off + 13] = 0; reply[ip_off + 14] = 2; reply[ip_off + 15] = 1;
	// dst: 255.255.255.255 (broadcast)
	reply[ip_off + 16] = 255; reply[ip_off + 17] = 255; reply[ip_off + 18] = 255; reply[ip_off + 19] = 255;

	// Compute IP checksum
	uint32_t cksum = 0;
	for (int i = 0; i < 20; i += 2)
		cksum += (reply[ip_off + i] << 8) | reply[ip_off + i + 1];
	while (cksum >> 16)
		cksum = (cksum & 0xFFFF) + (cksum >> 16);
	uint16_t ip_cksum = ~cksum;
	reply[ip_off + 10] = ip_cksum >> 8;
	reply[ip_off + 11] = ip_cksum & 0xFF;

	// Send directly to the Mac via the netif's linkoutput
	struct pbuf *rp = pbuf_alloc(PBUF_RAW, roff, PBUF_RAM);
	if (rp) {
		memcpy(rp->payload, reply, roff);
		s_netif->linkoutput(s_netif, rp);
		pbuf_free(rp);
	}

	pbuf_free(p);
	return 1;  // Consumed
}

// ---- IP4 input hook ----

int lwip_nat_ip4_input(struct pbuf *p, struct netif * /*inp*/)
{
	if (p->len < 20) return 0;

	// Parse IP header
	struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
	ip_addr_t dst_ip;
	dst_ip.addr = iphdr->dest.addr;

	uint8_t proto = IPH_PROTO(iphdr);

	if (g_debug_network) {
		char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
		const char *proto_name = (proto == IP_PROTO_TCP) ? "TCP" :
		                         (proto == IP_PROTO_UDP) ? "UDP" :
		                         (proto == IP_PROTO_ICMP) ? "ICMP" : "???";
		inet_ntop(AF_INET, &iphdr->src.addr, src_str, sizeof(src_str));
		inet_ntop(AF_INET, &iphdr->dest.addr, dst_str, sizeof(dst_str));
		fprintf(stderr, "[lwIP NAT] %s %s -> %s (%u bytes)\n",
			proto_name, src_str, dst_str, p->tot_len);
	}

	// If destined for the gateway, let lwIP handle it normally
	// EXCEPT: intercept DNS (UDP port 53) and proxy to host DNS
	if (ip_addr_cmp(&dst_ip, &s_gw_ip)) {
		if (proto == IP_PROTO_UDP) {
			uint16_t ip_hdr_len = IPH_HL(iphdr) * 4;
			if (p->tot_len >= ip_hdr_len + 8) {
				uint8_t udp_hdr_buf[8];
				pbuf_copy_partial(p, udp_hdr_buf, 8, ip_hdr_len);
				uint16_t src_port = (udp_hdr_buf[0] << 8) | udp_hdr_buf[1];
				uint16_t dst_port = (udp_hdr_buf[2] << 8) | udp_hdr_buf[3];
				if (dst_port == 53) {
					// DNS query to gateway — proxy directly to host DNS
					uint8_t buf[2048];
					uint16_t payload_len = p->tot_len - ip_hdr_len - 8;
					pbuf_copy_partial(p, buf, payload_len, ip_hdr_len + 8);

					if (g_debug_network)
						fprintf(stderr, "[lwIP NAT] DNS query intercepted (%u bytes, src_port=%u)\n",
							payload_len, src_port);

					// Forward to host DNS
					struct sockaddr_in sa;
					memset(&sa, 0, sizeof(sa));
					sa.sin_family = AF_INET;
					sa.sin_addr.s_addr = s_host_dns_ip;
					sa.sin_port = htons(53);

					// Find or create UDP proxy for this DNS exchange
					ip_addr_t src_ip;
					src_ip.addr = iphdr->src.addr;
					UdpProxy *proxy = udp_proxy_find(src_port, &dst_ip, dst_port);
					if (!proxy) {
						int fd = socket(AF_INET, SOCK_DGRAM, 0);
						if (fd < 0) {
							pbuf_free(p);
							return 1;
						}
						set_nonblocking(fd);
						proxy = new UdpProxy();
						proxy->host_fd = fd;
						proxy->mac_src_ip = src_ip;
						proxy->mac_src_port = src_port;
						proxy->orig_dst_ip = dst_ip;
						proxy->orig_dst_port = dst_port;
						proxy->last_activity = sys_now();
						proxy->next = s_udp_proxies;
						s_udp_proxies = proxy;
					}
					proxy->last_activity = sys_now();

					ssize_t sent = sendto(proxy->host_fd, buf, payload_len, 0,
						(struct sockaddr *)&sa, sizeof(sa));
					if (g_debug_network) {
						char dns_str[INET_ADDRSTRLEN];
						inet_ntop(AF_INET, &s_host_dns_ip, dns_str, sizeof(dns_str));
						fprintf(stderr, "[lwIP NAT] DNS forwarded to %s (%zd bytes)%s\n",
							dns_str, sent, sent < 0 ? " FAILED" : "");
						if (sent < 0)
							fprintf(stderr, "[lwIP NAT] DNS sendto error: %s\n", strerror(errno));
					}

					pbuf_free(p);
					return 1;  // Consumed
				}
			}
		}
		return 0;
	}

	// If broadcast, check for DHCP first, then let lwIP handle the rest
	if (dst_ip.addr == 0xFFFFFFFF || (dst_ip.addr & 0xFF000000) == 0xFF000000) {
		if (proto == IP_PROTO_UDP) {
			uint16_t ip_hdr_len = IPH_HL(iphdr) * 4;
			if (handle_dhcp(p, ip_hdr_len))
				return 1;
		}
		return 0;
	}
	uint16_t ip_hdr_len = IPH_HL(iphdr) * 4;

	// TTL check for traceroute: if TTL <= 1, send Time Exceeded
	uint8_t ttl = IPH_TTL(iphdr);
	if (ttl <= 1) {
		ip_addr_t src_ip;
		src_ip.addr = iphdr->src.addr;
		send_icmp_time_exceeded(p, &src_ip);
		pbuf_free(p);
		return 1;
	}

	if (proto == IP_PROTO_ICMP) {
		// Handle ICMP directly: proxy ping through host socket
		if (p->tot_len < ip_hdr_len + 8) {
			return 0;
		}

		// Extract ICMP header
		uint8_t icmp_buf[2048];
		uint16_t icmp_len = p->tot_len - ip_hdr_len;
		pbuf_copy_partial(p, icmp_buf, icmp_len, ip_hdr_len);

		uint8_t icmp_type = icmp_buf[0];
		if (icmp_type != 8) {  // Not echo request
			return 0;
		}

		uint16_t icmp_id = (icmp_buf[4] << 8) | icmp_buf[5];
		ip_addr_t src_ip;
		src_ip.addr = iphdr->src.addr;

		// Find or create ICMP proxy
		IcmpProxy *proxy = nullptr;
		for (IcmpProxy *ic = s_icmp_proxies; ic; ic = ic->next) {
			if (ic->id == icmp_id && ip_addr_cmp(&ic->orig_dst_ip, &dst_ip)) {
				proxy = ic;
				break;
			}
		}

		if (!proxy) {
			// Use SOCK_DGRAM + IPPROTO_ICMP for unprivileged ping
			int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
			if (fd < 0) {
				// Fallback: some systems don't allow unprivileged ICMP
				fprintf(stderr, "[lwIP NAT] ICMP socket failed: %s\n", strerror(errno));
				pbuf_free(p);
				return 1;
			}
			set_nonblocking(fd);

			proxy = new IcmpProxy();
			proxy->host_fd = fd;
			proxy->mac_src_ip = src_ip;
			proxy->orig_dst_ip = dst_ip;
			proxy->id = icmp_id;
			proxy->last_activity = 0;
			proxy->next = s_icmp_proxies;
			s_icmp_proxies = proxy;
		}

		// Send ICMP echo to host
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = dst_ip.addr;

		ssize_t sent = sendto(proxy->host_fd, icmp_buf, icmp_len, 0,
		       (struct sockaddr *)&sa, sizeof(sa));
		if (g_debug_network) {
			char dst_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &dst_ip.addr, dst_str, sizeof(dst_str));
			fprintf(stderr, "[lwIP NAT] ICMP echo request -> %s id=%u seq=%u (%u bytes)%s\n",
				dst_str, icmp_id, (icmp_buf[6] << 8) | icmp_buf[7], icmp_len,
				sent < 0 ? " FAILED" : "");
		}

		pbuf_free(p);
		return 1;  // Consumed
	}

	if (proto == IP_PROTO_TCP) {
		if (p->tot_len < ip_hdr_len + 4) return 0;

		// Extract src/dst ports from TCP header
		uint8_t tcp_hdr[4];
		pbuf_copy_partial(p, tcp_hdr, 4, ip_hdr_len);
		uint16_t src_port = (tcp_hdr[0] << 8) | tcp_hdr[1];
		uint16_t dst_port = (tcp_hdr[2] << 8) | tcp_hdr[3];

		// Save original destination in NAT table
		ip_addr_t src_ip;
		src_ip.addr = iphdr->src.addr;

		NatEntry *entry = nat_find(IP_PROTO_TCP, src_port);
		if (!entry) {
			nat_add(IP_PROTO_TCP, src_port, &dst_ip, dst_port, &src_ip);
		}

		// Rewrite destination IP to gateway so lwIP accepts the packet
		iphdr->dest.addr = s_gw_ip.addr;

		// Recalculate IP checksum
		IPH_CHKSUM_SET(iphdr, 0);
		IPH_CHKSUM_SET(iphdr, inet_chksum(iphdr, ip_hdr_len));

		// TCP checksum includes pseudo-header with dest IP — recalculate
		{
			// Zero out existing TCP checksum
			uint8_t zero[2] = {0, 0};
			pbuf_take_at(p, zero, 2, ip_hdr_len + 16);  // TCP checksum at offset 16

			// Compute checksum over TCP data using a pbuf that starts at TCP header
			uint16_t tcp_len = p->tot_len - ip_hdr_len;
			uint8_t tcp_data[65535];
			pbuf_copy_partial(p, tcp_data, tcp_len, ip_hdr_len);

			// Pseudo-header checksum
			ip_addr_t src_copy, dest_copy;
			src_copy.addr = iphdr->src.addr;
			dest_copy.addr = iphdr->dest.addr;
			uint32_t acc = 0;
			acc += (src_copy.addr >> 16) & 0xFFFF;
			acc += src_copy.addr & 0xFFFF;
			acc += (dest_copy.addr >> 16) & 0xFFFF;
			acc += dest_copy.addr & 0xFFFF;
			acc += htons(IP_PROTO_TCP);
			acc += htons(tcp_len);

			// Add TCP data
			for (uint16_t i = 0; i < tcp_len; i += 2) {
				uint16_t word = (tcp_data[i] << 8);
				if (i + 1 < tcp_len) word |= tcp_data[i + 1];
				acc += word;
			}
			while (acc >> 16)
				acc = (acc & 0xFFFF) + (acc >> 16);
			uint16_t chk = ~(uint16_t)acc;
			if (chk == 0) chk = 0xFFFF;

			// Write back in network byte order
			uint8_t chk_bytes[2] = {(uint8_t)(chk >> 8), (uint8_t)(chk & 0xFF)};
			pbuf_take_at(p, chk_bytes, 2, ip_hdr_len + 16);
		}

		return 0;  // Let lwIP handle the rewritten packet
	}

	if (proto == IP_PROTO_UDP) {
		if (p->tot_len < ip_hdr_len + 8) return 0;

		// Extract ports
		uint8_t udp_hdr[8];
		pbuf_copy_partial(p, udp_hdr, 8, ip_hdr_len);
		uint16_t src_port = (udp_hdr[0] << 8) | udp_hdr[1];
		uint16_t dst_port = (udp_hdr[2] << 8) | udp_hdr[3];

		ip_addr_t src_ip;
		src_ip.addr = iphdr->src.addr;

		// Save NAT mapping (include TTL for traceroute)
		NatEntry *entry = nat_find(IP_PROTO_UDP, src_port);
		if (!entry) {
			nat_add(IP_PROTO_UDP, src_port, &dst_ip, dst_port, &src_ip, ttl);
		} else {
			entry->ttl = ttl;  // Update TTL for each packet (traceroute increments)
		}

		// Rewrite destination IP to gateway
		iphdr->dest.addr = s_gw_ip.addr;

		// Recalculate IP checksum
		IPH_CHKSUM_SET(iphdr, 0);
		IPH_CHKSUM_SET(iphdr, inet_chksum(iphdr, ip_hdr_len));

		// Zero UDP checksum (optional in IPv4)
		uint8_t zero[2] = {0, 0};
		pbuf_take_at(p, zero, 2, ip_hdr_len + 6);  // UDP checksum at offset 6

		return 0;  // Let lwIP handle the rewritten packet
	}

	return 0;
}

// ---- Init / Shutdown ----

void lwip_nat_init(struct netif *netif)
{
	s_netif = netif;
	ip_addr_set(&s_gw_ip, &netif->ip_addr);

	// Resolve host DNS
	s_host_dns_ip = resolve_host_dns();
	char dns_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &s_host_dns_ip, dns_str, sizeof(dns_str));
	fprintf(stderr, "[lwIP NAT] Host DNS: %s\n", dns_str);

	// Set up TCP listener on gateway IP (catches NAT'd connections)
	s_tcp_listen_pcb = tcp_new();
	if (s_tcp_listen_pcb) {
		tcp_bind(s_tcp_listen_pcb, &s_gw_ip, 0);  // port 0 = any port
		s_tcp_listen_pcb = tcp_listen(s_tcp_listen_pcb);
		if (s_tcp_listen_pcb) {
			tcp_accept(s_tcp_listen_pcb, tcp_nat_accept);
		}
	}

	// Set up UDP catchall
	s_udp_catchall = udp_new();
	if (s_udp_catchall) {
		udp_bind(s_udp_catchall, &s_gw_ip, 0);
		udp_recv(s_udp_catchall, udp_nat_recv, nullptr);
	}

	fprintf(stderr, "[lwIP NAT] NAT proxy initialized\n");
}

void lwip_nat_shutdown(void)
{
	// Clean up TCP proxies
	while (s_tcp_proxies) {
		TcpProxy *next = s_tcp_proxies->next;
		if (s_tcp_proxies->host_fd >= 0)
			close(s_tcp_proxies->host_fd);
		if (s_tcp_proxies->pcb) {
			tcp_arg(s_tcp_proxies->pcb, nullptr);
			tcp_abort(s_tcp_proxies->pcb);
		}
		delete s_tcp_proxies;
		s_tcp_proxies = next;
	}

	// Clean up UDP proxies
	while (s_udp_proxies) {
		UdpProxy *next = s_udp_proxies->next;
		if (s_udp_proxies->host_fd >= 0)
			close(s_udp_proxies->host_fd);
		delete s_udp_proxies;
		s_udp_proxies = next;
	}

	// Clean up ICMP proxies
	while (s_icmp_proxies) {
		IcmpProxy *next = s_icmp_proxies->next;
		if (s_icmp_proxies->host_fd >= 0)
			close(s_icmp_proxies->host_fd);
		delete s_icmp_proxies;
		s_icmp_proxies = next;
	}

	// Clean up NAT table
	while (s_nat_table) {
		NatEntry *next = s_nat_table->next;
		delete s_nat_table;
		s_nat_table = next;
	}

	if (s_tcp_listen_pcb) {
		tcp_close(s_tcp_listen_pcb);
		s_tcp_listen_pcb = nullptr;
	}
	if (s_udp_catchall) {
		udp_remove(s_udp_catchall);
		s_udp_catchall = nullptr;
	}

	s_netif = nullptr;
}

// ---- Poll host sockets ----

void lwip_nat_poll(void)
{
	// Collect all host fds
	std::vector<struct pollfd> fds;
	std::vector<void *> owners;  // Tracks which proxy owns each fd
	std::vector<int> types;      // 0=tcp, 1=udp, 2=icmp

	for (TcpProxy *p = s_tcp_proxies; p; p = p->next) {
		if (p->host_fd < 0) continue;
		struct pollfd pfd;
		pfd.fd = p->host_fd;
		pfd.events = POLLIN;
		if (!p->host_connected)
			pfd.events |= POLLOUT;  // Waiting for connect
		pfd.revents = 0;
		fds.push_back(pfd);
		owners.push_back(p);
		types.push_back(0);
	}

	for (UdpProxy *p = s_udp_proxies; p; p = p->next) {
		if (p->host_fd < 0) continue;
		struct pollfd pfd = {p->host_fd, POLLIN | POLLERR, 0};
		fds.push_back(pfd);
		owners.push_back(p);
		types.push_back(1);
	}

	for (IcmpProxy *p = s_icmp_proxies; p; p = p->next) {
		if (p->host_fd < 0) continue;
		struct pollfd pfd = {p->host_fd, POLLIN, 0};
		fds.push_back(pfd);
		owners.push_back(p);
		types.push_back(2);
	}

	if (fds.empty()) return;

	int ret = poll(fds.data(), fds.size(), 0);  // Non-blocking poll
	if (ret <= 0) return;

	for (size_t i = 0; i < fds.size(); i++) {
		if (fds[i].revents == 0) continue;

		if (types[i] == 0) {
			TcpProxy *proxy = (TcpProxy *)owners[i];
			if (fds[i].revents & POLLOUT) {
				// Connect completed
				int err = 0;
				socklen_t len = sizeof(err);
				getsockopt(proxy->host_fd, SOL_SOCKET, SO_ERROR, &err, &len);
				if (err == 0) {
					proxy->host_connected = true;
					// Flush pending data (handle partial sends)
					if (!proxy->pending_to_host.empty()) {
						ssize_t sent = send(proxy->host_fd, proxy->pending_to_host.data(),
						     proxy->pending_to_host.size(), MSG_NOSIGNAL);
						if (sent > 0 && (size_t)sent < proxy->pending_to_host.size()) {
							proxy->pending_to_host.erase(
								proxy->pending_to_host.begin(),
								proxy->pending_to_host.begin() + sent);
						} else {
							proxy->pending_to_host.clear();
						}
					}
				} else {
					proxy->closing = true;
				}
			}
			if (fds[i].revents & POLLIN) {
				tcp_proxy_host_to_mac(proxy);
			}
			if (fds[i].revents & (POLLERR | POLLHUP)) {
				proxy->closing = true;
			}
		} else if (types[i] == 1) {
			UdpProxy *proxy = (UdpProxy *)owners[i];
			if (fds[i].revents & POLLIN) {
				udp_proxy_host_to_mac(proxy);
			}
			if (fds[i].revents & POLLERR) {
				check_udp_icmp_errors(proxy);
			}
		} else if (types[i] == 2) {
			IcmpProxy *proxy = (IcmpProxy *)owners[i];
			if (fds[i].revents & POLLIN) {
				icmp_proxy_host_to_mac(proxy);
			}
		}
	}

	// Clean up closing TCP proxies
	TcpProxy *tp = s_tcp_proxies;
	while (tp) {
		TcpProxy *next = tp->next;
		if (tp->closing) {
			nat_remove(IP_PROTO_TCP, tp->pcb ? tp->pcb->remote_port : 0);
			tcp_proxy_destroy(tp);
		}
		tp = next;
	}

	// Clean up old UDP proxies (60s timeout)
	u32_t now = sys_now();
	UdpProxy *up = s_udp_proxies;
	while (up) {
		UdpProxy *next = up->next;
		if (now - up->last_activity > 60000 && up->last_activity != 0) {
			nat_remove(IP_PROTO_UDP, up->mac_src_port);
			udp_proxy_destroy(up);
		}
		up = next;
	}

	// Clean up old ICMP proxies (10s timeout)
	IcmpProxy *ic = s_icmp_proxies;
	while (ic) {
		IcmpProxy *next = ic->next;
		if (now - ic->last_activity > 10000 && ic->last_activity != 0) {
			close(ic->host_fd);
			IcmpProxy **pp = &s_icmp_proxies;
			while (*pp) {
				if (*pp == ic) { *pp = ic->next; break; }
				pp = &(*pp)->next;
			}
			delete ic;
		}
		ic = next;
	}
}
