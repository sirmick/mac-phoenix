/*
 *  ether_lwip.cpp - lwIP-based userland NAT networking (SLiRP mode)
 *
 *  Provides a virtual ethernet interface backed by lwIP's TCP/IP stack.
 *  The Mac gets an IP via DHCP (10.0.2.15), with NAT to the host network.
 *  TCP/UDP connections are proxied through real host sockets.
 *  No privileges required.
 *
 *  Architecture:
 *    Mac ethernet frames → lwIP mac_netif → IP stack processes ARP/TCP/UDP
 *    → TCP/UDP proxy opens real host sockets → bidirectional data relay
 *    → lwIP generates response frames → delivered to Mac via EtherInterrupt
 */

#include "sysdeps.h"
#include "platform.h"
#include "ether_lwip.h"
#include "cpu_emulation.h"
#include "main.h"
#include "ether.h"
#include "ether_defs.h"
#include "uae_wrapper.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <vector>

#define DEBUG 0
#include "debug.h"

// TODO: Include lwIP headers once vendored
// #include "lwip/init.h"
// #include "lwip/netif.h"
// #include "lwip/tcp.h"
// #include "lwip/udp.h"
// #include "lwip/timeouts.h"
// #include "lwip/etharp.h"

// ---- Internal state ----

static std::mutex s_mutex;
static std::atomic<bool> s_running{false};
static std::thread s_net_thread;

// Queue of frames to deliver to the Mac (written by lwIP, read by EtherInterrupt)
struct PendingFrame {
	std::vector<uint8_t> data;
};
static std::queue<PendingFrame> s_rx_queue;

// Mac's ethernet address
static uint8_t s_mac_addr[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x01};  // locally administered

// ---- Frame delivery to Mac ----

// Called from EtherInterrupt context to deliver queued frames to the Mac
static void lwip_deliver_frames()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	while (!s_rx_queue.empty()) {
		auto& frame = s_rx_queue.front();
		if (frame.data.size() >= 14 && ether_data) {
			// Allocate packet buffer in Mac memory
			EthernetPacket pkt;
			Host2Mac_memcpy(pkt.addr(), frame.data.data(), frame.data.size());
			ether_udp_read(pkt.addr(), frame.data.size(), nullptr);
		}
		s_rx_queue.pop();
	}
}

// Enqueue a frame for delivery to the Mac
static void lwip_enqueue_rx(const uint8_t* data, size_t len)
{
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_rx_queue.push(PendingFrame{std::vector<uint8_t>(data, data + len)});
	}
	SetInterruptFlag(INTFLAG_ETHER);
	TriggerInterrupt();
}

// ---- Network thread ----

static void lwip_net_thread()
{
	fprintf(stderr, "[lwIP] Network thread started\n");
	while (s_running) {
		// TODO: Poll host sockets for proxy connections
		// TODO: sys_check_timeouts() for lwIP timers
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	fprintf(stderr, "[lwIP] Network thread stopped\n");
}

// ---- Platform driver interface ----

static bool ether_lwip_init(void)
{
	fprintf(stderr, "[lwIP] Initializing lwIP networking\n");

	// Set Mac ethernet address
	memcpy(ether_addr, s_mac_addr, 6);

	// TODO: Initialize lwIP stack
	// lwip_init();
	// Set up mac_netif with IP 10.0.2.1 (gateway)
	// Start DHCP server to assign 10.0.2.15 to Mac
	// Register TCP/UDP listeners for proxy

	// Start network thread
	s_running = true;
	s_net_thread = std::thread(lwip_net_thread);

	fprintf(stderr, "[lwIP] lwIP networking ready (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
		s_mac_addr[0], s_mac_addr[1], s_mac_addr[2],
		s_mac_addr[3], s_mac_addr[4], s_mac_addr[5]);

	return true;
}

static void ether_lwip_exit(void)
{
	fprintf(stderr, "[lwIP] Shutting down\n");
	s_running = false;
	if (s_net_thread.joinable())
		s_net_thread.join();

	// TODO: Tear down lwIP, close proxy sockets
	// netif_remove(&mac_netif);

	std::lock_guard<std::mutex> lock(s_mutex);
	while (!s_rx_queue.empty()) s_rx_queue.pop();
}

static void ether_lwip_reset(void)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	while (!s_rx_queue.empty()) s_rx_queue.pop();
	// TODO: Reset lwIP connection tracking
}

static int16_t ether_lwip_add_multicast(uint32_t pb)
{
	// TODO: Track multicast groups for DHCP discovery
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
	(void)type;
	(void)handler;
	return 0;
}

static int16_t ether_lwip_detach_ph(uint16_t type)
{
	D(bug("[lwIP] DetachPH type=%04x\n", type));
	(void)type;
	return 0;
}

static int16_t ether_lwip_write(uint32_t wds)
{
	// Linearize WDS into a flat ethernet frame
	uint8_t packet[1514];
	int len = ether_wds_to_buffer(wds, packet);
	if (len < 14) return eLenErr;

	D(bug("[lwIP] TX %d bytes, type=%04x\n", len,
		(packet[12] << 8) | packet[13]));

	// TODO: Feed frame into lwIP
	// std::lock_guard<std::mutex> lock(s_mutex);
	// struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
	// memcpy(p->payload, packet, len);
	// mac_netif.input(p, &mac_netif);

	return 0;
}

static bool ether_lwip_start_udp_thread(int socket_fd)
{
	(void)socket_fd;
	return false;  // Not used in lwIP mode
}

static void ether_lwip_stop_udp_thread(void)
{
	// Not used in lwIP mode
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
