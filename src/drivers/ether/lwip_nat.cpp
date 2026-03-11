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
#include <fstream>
#include <string>

// Gateway IP (lwIP's own address)
static ip_addr_t s_gw_ip;
static struct netif *s_netif = nullptr;

// Host's real DNS server (read from /etc/resolv.conf)
static uint32_t s_host_dns_ip = 0;  // network byte order

// Virtual DNS address the Mac uses
#define NAT_DNS_IP  "10.0.2.3"

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
	uint8_t buf[4096];
	uint16_t copied = pbuf_copy_partial(p, buf, p->tot_len, 0);
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);

	if (!proxy->host_connected) {
		// Buffer until connect completes
		proxy->pending_to_host.insert(proxy->pending_to_host.end(), buf, buf + copied);
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

	char dst_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &dst_ip->addr, dst_str, sizeof(dst_str));
	fprintf(stderr, "[lwIP NAT] TCP proxy %s:%u\n", dst_str, dst_port);

	return proxy;
}

// ---- UDP Proxy ----

struct UdpProxy {
	int host_fd;
	ip_addr_t mac_src_ip;
	uint16_t mac_src_port;
	ip_addr_t orig_dst_ip;
	uint16_t orig_dst_port;
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

	// Create pbuf and send back through lwIP to Mac
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
	if (!p) return;
	memcpy(p->payload, buf, n);

	// Reply comes from the original destination IP/port
	udp_sendto(s_udp_catchall, p, &proxy->mac_src_ip, proxy->mac_src_port);
	pbuf_free(p);
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

	// Linux SOCK_DGRAM/IPPROTO_ICMP returns just the ICMP payload (no IP header)
	// Build an IP + ICMP packet and inject into lwIP as a response to Mac
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
	if (!p) return;
	memcpy(p->payload, buf, n);

	// Send ICMP reply back to Mac via raw IP
	// Use the gateway's raw output path
	struct raw_pcb *tmp = raw_new(IP_PROTO_ICMP);
	if (tmp) {
		raw_sendto(tmp, p, &proxy->mac_src_ip);
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
                         const ip_addr_t *src_ip)
{
	NatEntry *e = new NatEntry();
	e->proto = proto;
	e->src_port = src_port;
	e->orig_dst_ip = *dst_ip;
	e->orig_dst_port = dst_port;
	e->orig_src_ip = *src_ip;
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
	uint16_t local_port = newpcb->local_port;
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

static void udp_nat_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
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
		// No NAT entry — this shouldn't happen, drop it
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

	// Find or create proxy
	UdpProxy *proxy = udp_proxy_find(port, &dst_ip, dst_port);
	if (!proxy) {
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0) {
			pbuf_free(p);
			return;
		}
		set_nonblocking(fd);

		proxy = new UdpProxy();
		proxy->host_fd = fd;
		proxy->mac_src_ip = *addr;
		proxy->mac_src_port = port;
		proxy->orig_dst_ip = dst_ip;
		proxy->orig_dst_port = dst_port;
		proxy->last_activity = 0;
		proxy->next = s_udp_proxies;
		s_udp_proxies = proxy;
	}

	// Copy pbuf to linear buffer and send to host
	uint8_t buf[2048];
	uint16_t len = pbuf_copy_partial(p, buf, p->tot_len, 0);
	pbuf_free(p);

	sendto(proxy->host_fd, buf, len, 0, (struct sockaddr *)&sa, sizeof(sa));
}

// ---- IP4 input hook ----

int lwip_nat_ip4_input(struct pbuf *p, struct netif *inp)
{
	if (p->len < 20) return 0;  // Too short for IP header

	// Parse IP header
	struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
	ip_addr_t dst_ip;
	dst_ip.addr = iphdr->dest.addr;

	// If destined for the gateway, let lwIP handle it normally
	if (ip_addr_cmp(&dst_ip, &s_gw_ip))
		return 0;

	uint8_t proto = IPH_PROTO(iphdr);
	uint16_t ip_hdr_len = IPH_HL(iphdr) * 4;

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

		sendto(proxy->host_fd, icmp_buf, icmp_len, 0,
		       (struct sockaddr *)&sa, sizeof(sa));

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
		// We need to recalculate the full TCP checksum
		struct pbuf *tcp_p = pbuf_skip(p, ip_hdr_len, NULL);
		if (tcp_p) {
			// Zero out existing TCP checksum
			uint8_t zero[2] = {0, 0};
			pbuf_take_at(p, zero, 2, ip_hdr_len + 16);  // TCP checksum at offset 16

			uint16_t tcp_len = p->tot_len - ip_hdr_len;
			uint16_t chk = ip_chksum_pseudo(p, IP_PROTO_TCP, tcp_len,
			                                 (ip_addr_t *)&iphdr->src,
			                                 (ip_addr_t *)&iphdr->dest);
			// Write checksum back — but ip_chksum_pseudo already accounts for
			// the pseudo-header, and we need the checksum at offset 16 in the TCP header
			pbuf_take_at(p, &chk, 2, ip_hdr_len + 16);
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

		// Save NAT mapping
		NatEntry *entry = nat_find(IP_PROTO_UDP, src_port);
		if (!entry) {
			nat_add(IP_PROTO_UDP, src_port, &dst_ip, dst_port, &src_ip);
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
		struct pollfd pfd = {p->host_fd, POLLIN, 0};
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
					// Flush pending data
					if (!proxy->pending_to_host.empty()) {
						send(proxy->host_fd, proxy->pending_to_host.data(),
						     proxy->pending_to_host.size(), MSG_NOSIGNAL);
						proxy->pending_to_host.clear();
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
