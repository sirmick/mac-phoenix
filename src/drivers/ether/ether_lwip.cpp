/*
 *  ether_lwip.cpp - lwIP-based userland NAT networking (SLiRP mode)
 *
 *  Provides a virtual ethernet interface backed by lwIP's TCP/IP stack.
 *  The Mac gets a static IP (10.0.2.15), with NAT to the host network.
 *  TCP/UDP connections are proxied through real host sockets.
 *  No privileges required.
 *
 *  Architecture:
 *    Mac ethernet frames -> lwIP mac_netif -> IP stack processes ARP/TCP/UDP
 *    -> TCP/UDP proxy opens real host sockets -> bidirectional data relay
 *    -> lwIP generates response frames -> delivered to Mac via EtherInterrupt
 *
 *  Threading model:
 *    All lwIP API calls happen on the network thread (NO_SYS=1).
 *    CPU thread only enqueues TX frames via mutex-protected queue.
 *    RX frames are queued and delivered via EtherInterrupt on CPU thread.
 */

#include "sysdeps.h"
#include "platform.h"
#include "ether_lwip.h"
#include "cpu_emulation.h"
#include "main.h"
#include "ether.h"
#include "ether_defs.h"
#include "uae_wrapper.h"
#include "lwip_nat.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "netif/ethernet.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <vector>
#include <cstdlib>

#include "lwip_nat.h"  // g_debug_network

#define DEBUG 0
#include "debug.h"

// ---- Internal state ----

static std::mutex s_mutex;
static std::atomic<bool> s_running{false};
static std::thread s_net_thread;

// lwIP network interface (represents the gateway side of the virtual ethernet)
static struct netif s_mac_netif;

// Queue of frames to deliver to the Mac (written by lwIP, read by EtherInterrupt)
struct PendingFrame {
	std::vector<uint8_t> data;
};
static std::queue<PendingFrame> s_rx_queue;

// Queue of frames from Mac to feed into lwIP (written by CPU thread, read by net thread)
static std::queue<PendingFrame> s_tx_queue;

// Mac's ethernet address (the guest Mac)
static uint8_t s_mac_addr[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x01};

// Gateway's ethernet address (lwIP's netif)
static uint8_t s_gw_mac_addr[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x02};

// ---- Frame delivery to Mac ----

// Called from EtherInterrupt context to deliver queued frames to the Mac
static void lwip_deliver_frames()
{
	if (!s_running) return;
	std::lock_guard<std::mutex> lock(s_mutex);
	while (!s_rx_queue.empty()) {
		auto& frame = s_rx_queue.front();
		if (frame.data.size() >= 14 && ether_data) {
			EthernetPacket pkt;
			Host2Mac_memcpy(pkt.addr(), frame.data.data(), frame.data.size());
			ether_udp_read(pkt.addr(), frame.data.size(), nullptr);
		}
		s_rx_queue.pop();
	}
}

// Enqueue a frame for delivery to the Mac (called from lwIP on net thread)
static void lwip_enqueue_rx(const uint8_t* data, size_t len)
{
	if (!s_running) return;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_rx_queue.push(PendingFrame{std::vector<uint8_t>(data, data + len)});
	}
	SetInterruptFlag(INTFLAG_ETHER);
	TriggerInterrupt();
}

// ---- lwIP netif callbacks ----

// Called by lwIP when it has a frame to send to the Mac
static err_t mac_netif_linkoutput(struct netif *netif, struct pbuf *p)
{
	(void)netif;

	uint8_t buf[1518];
	uint16_t len = pbuf_copy_partial(p, buf, sizeof(buf), 0);
	if (len < 14) return ERR_BUF;

	D(bug("[lwIP] netif TX %u bytes to Mac, type=%04x\n", len,
		(buf[12] << 8) | buf[13]));

	lwip_enqueue_rx(buf, len);
	return ERR_OK;
}

// Initialize the lwIP netif
static err_t mac_netif_init(struct netif *netif)
{
	netif->name[0] = 'm';
	netif->name[1] = 'c';
	netif->hwaddr_len = 6;
	memcpy(netif->hwaddr, s_gw_mac_addr, 6);
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	netif->linkoutput = mac_netif_linkoutput;
	netif->output = etharp_output;
	return ERR_OK;
}

// ---- Network thread ----

static void lwip_net_thread()
{
	D(bug("[lwIP] Network thread started\n"));
	while (s_running) {
		// 1. Drain TX queue: copy frames out under lock, then feed to lwIP unlocked
		//    (lwIP callbacks like linkoutput may re-enter lwip_enqueue_rx which needs the mutex)
		std::vector<PendingFrame> tx_batch;
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			while (!s_tx_queue.empty()) {
				tx_batch.push_back(std::move(s_tx_queue.front()));
				s_tx_queue.pop();
			}
		}
		for (auto& frame : tx_batch) {
			if (g_debug_network && frame.data.size() >= 14) {
				uint16_t ethertype = (frame.data[12] << 8) | frame.data[13];
				const char *type_name = (ethertype == 0x0800) ? "IPv4" :
				                        (ethertype == 0x0806) ? "ARP" : "???";
				fprintf(stderr, "[lwIP] Mac TX: %s (%zu bytes)\n",
					type_name, frame.data.size());
			}
			struct pbuf *p = pbuf_alloc(PBUF_RAW, frame.data.size(), PBUF_RAM);
			if (p) {
				memcpy(p->payload, frame.data.data(), frame.data.size());
				s_mac_netif.input(p, &s_mac_netif);
			}
		}

		// 2. Poll host proxy sockets (TCP, UDP, ICMP)
		lwip_nat_poll();

		// 3. Process lwIP timers (ARP, TCP retransmit, etc.)
		sys_check_timeouts();

		// 4. Short sleep to avoid busy-spinning when idle
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	D(bug("[lwIP] Network thread stopped\n"));
}

// ---- Platform driver interface ----

static bool ether_lwip_init(void)
{
	fprintf(stderr, "[lwIP] Initializing lwIP networking\n");

	// Set Mac ethernet address (what the guest sees)
	memcpy(ether_addr, s_mac_addr, 6);

	// Initialize lwIP stack
	lwip_init();

	// Configure gateway address (lwIP's own IP)
	ip4_addr_t gw_ip, netmask, gw_gw;
	IP4_ADDR(&gw_ip, 10, 0, 2, 1);
	IP4_ADDR(&netmask, 255, 255, 255, 0);
	IP4_ADDR(&gw_gw, 0, 0, 0, 0);

	// Add the network interface
	netif_add(&s_mac_netif, &gw_ip, &netmask, &gw_gw,
	          nullptr, mac_netif_init, ethernet_input);
	netif_set_default(&s_mac_netif);
	netif_set_up(&s_mac_netif);

	// Initialize NAT proxy (TCP/UDP/ICMP proxying)
	lwip_nat_init(&s_mac_netif);

	// Register atexit handler to stop the network thread on exit()
	// (the --timeout path calls exit(0) directly from a thread)
	static bool atexit_registered = false;
	if (!atexit_registered) {
		atexit([]() {
			s_running = false;
			if (s_net_thread.joinable())
				s_net_thread.join();
		});
		atexit_registered = true;
	}

	// Start network thread
	s_running = true;
	s_net_thread = std::thread(lwip_net_thread);

	fprintf(stderr, "[lwIP] lwIP networking ready (debug=%s)\n",
		g_debug_network ? "on" : "off");
	fprintf(stderr, "[lwIP]   Gateway:  10.0.2.1  (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
		s_gw_mac_addr[0], s_gw_mac_addr[1], s_gw_mac_addr[2],
		s_gw_mac_addr[3], s_gw_mac_addr[4], s_gw_mac_addr[5]);
	fprintf(stderr, "[lwIP]   Mac guest: 10.0.2.15 (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
		s_mac_addr[0], s_mac_addr[1], s_mac_addr[2],
		s_mac_addr[3], s_mac_addr[4], s_mac_addr[5]);
	fprintf(stderr, "[lwIP]   DNS:       10.0.2.1 (gateway, proxied to host resolver)\n");

	return true;
}

static void ether_lwip_exit(void)
{
	fprintf(stderr, "[lwIP] Shutting down\n");

	// Stop accepting new frames and interrupts immediately
	s_running = false;

	// Wait for network thread to finish
	if (s_net_thread.joinable())
		s_net_thread.join();

	// Clean up NAT proxies and lwIP (safe now that thread is stopped)
	lwip_nat_shutdown();
	netif_set_down(&s_mac_netif);
	netif_remove(&s_mac_netif);

	// Drain remaining queued frames
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		while (!s_rx_queue.empty()) s_rx_queue.pop();
		while (!s_tx_queue.empty()) s_tx_queue.pop();
	}

	fprintf(stderr, "[lwIP] Shutdown complete\n");
}

static void ether_lwip_reset(void)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	while (!s_rx_queue.empty()) s_rx_queue.pop();
	while (!s_tx_queue.empty()) s_tx_queue.pop();
}

static int16_t ether_lwip_add_multicast(uint32_t pb)
{
	(void)pb;
	return 0;
}

static int16_t ether_lwip_del_multicast(uint32_t pb)
{
	(void)pb;
	return 0;
}

static int16_t ether_lwip_attach_ph(uint16_t type, uint32_t handler)
{
	D(bug("[lwIP] AttachPH type=%04x handler=%08x\n", type, handler));
	ether_register_protocol(type, handler);
	return 0;
}

static int16_t ether_lwip_detach_ph(uint16_t type)
{
	D(bug("[lwIP] DetachPH type=%04x\n", type));
	ether_unregister_protocol(type);
	return 0;
}

static int16_t ether_lwip_write(uint32_t wds)
{
	// Linearize WDS into a flat ethernet frame
	uint8_t packet[1514];
	int len = ether_wds_to_buffer(wds, packet);
	if (len < 14) return eLenErr;

	// Enqueue for processing on the network thread (no lwIP calls here)
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_tx_queue.push(PendingFrame{std::vector<uint8_t>(packet, packet + len)});
	}

	return 0;
}

static bool ether_lwip_start_udp_thread(int socket_fd)
{
	(void)socket_fd;
	return false;
}

static void ether_lwip_stop_udp_thread(void)
{
}

// ---- Registration ----

void ether_lwip_register(void)
{
	g_platform.ether_init = ether_lwip_init;
	g_platform.ether_exit = ether_lwip_exit;
	g_platform.ether_reset = ether_lwip_reset;
	g_platform.ether_add_multicast = ether_lwip_add_multicast;
	g_platform.ether_del_multicast = ether_lwip_del_multicast;
	g_platform.ether_attach_ph = ether_lwip_attach_ph;
	g_platform.ether_detach_ph = ether_lwip_detach_ph;
	g_platform.ether_write = ether_lwip_write;
	g_platform.ether_start_udp_thread = ether_lwip_start_udp_thread;
	g_platform.ether_stop_udp_thread = ether_lwip_stop_udp_thread;
	g_platform.ether_interrupt = lwip_deliver_frames;

	fprintf(stderr, "[Network] lwIP NAT driver registered\n");
}
