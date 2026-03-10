/*
 *  ether_raw.cpp - AF_PACKET raw socket bridged networking
 *
 *  Binds the Mac's ethernet directly onto a host NIC using raw sockets.
 *  The Mac appears as a real host on the physical network with its own MAC.
 *  Requires CAP_NET_RAW or root.
 *
 *  Architecture:
 *    Mac ethernet frames → AF_PACKET socket → physical network
 *    Physical network → AF_PACKET recv → filter by MAC → EtherInterrupt → Mac
 */

#include "sysdeps.h"
#include "platform.h"
#include "ether_raw.h"
#include "cpu_emulation.h"
#include "main.h"
#include "ether.h"
#include "ether_defs.h"
#include "uae_wrapper.h"

#include <cstdio>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <poll.h>

#define DEBUG 0
#include "debug.h"

// ---- Internal state ----

static int s_raw_fd = -1;
static int s_if_index = 0;
static std::string s_if_name;
static std::atomic<bool> s_running{false};
static std::thread s_rx_thread;

// Mac's ethernet address (locally administered, distinct from host NIC)
static uint8_t s_mac_addr[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x02};

// Queue of frames to deliver to the Mac (written by RX thread, read by EtherInterrupt)
static std::mutex s_rx_mutex;
static std::queue<std::vector<uint8_t>> s_rx_queue;

// ---- Frame delivery (called from EtherInterrupt context) ----

static void raw_deliver_frames()
{
	std::lock_guard<std::mutex> lock(s_rx_mutex);
	while (!s_rx_queue.empty()) {
		auto& frame = s_rx_queue.front();
		if (frame.size() >= 14 && ether_data) {
			EthernetPacket pkt;
			Host2Mac_memcpy(pkt.addr(), frame.data(), frame.size());
			ether_udp_read(pkt.addr(), frame.size(), nullptr);
		}
		s_rx_queue.pop();
	}
}

// ---- Receive thread ----

static void raw_rx_thread()
{
	fprintf(stderr, "[Raw] Receive thread started on %s\n", s_if_name.c_str());
	uint8_t buf[1518];

	while (s_running) {
		struct pollfd pfd = {s_raw_fd, POLLIN, 0};
		int ret = poll(&pfd, 1, 100);  // 100ms timeout
		if (ret <= 0) continue;

		ssize_t len = recv(s_raw_fd, buf, sizeof(buf), 0);
		if (len < 14) continue;

		// Filter: only accept frames destined to our MAC, broadcast, or multicast
		bool is_ours = (memcmp(buf, s_mac_addr, 6) == 0);
		bool is_broadcast = (buf[0] == 0xff && buf[1] == 0xff && buf[2] == 0xff &&
		                     buf[3] == 0xff && buf[4] == 0xff && buf[5] == 0xff);
		bool is_multicast = (buf[0] & 0x01);

		if (!is_ours && !is_broadcast && !is_multicast) continue;

		// Drop frames we sent (check source MAC)
		if (memcmp(buf + 6, s_mac_addr, 6) == 0) continue;

		D(bug("[Raw] RX %zd bytes, type=%04x\n", len, (buf[12] << 8) | buf[13]));

		// Queue frame for delivery in emulator thread context
		{
			std::lock_guard<std::mutex> lock(s_rx_mutex);
			s_rx_queue.push(std::vector<uint8_t>(buf, buf + len));
		}
		SetInterruptFlag(INTFLAG_ETHER);
		TriggerInterrupt();
	}
	fprintf(stderr, "[Raw] Receive thread stopped\n");
}

// ---- Platform driver interface ----

static bool ether_raw_init(void)
{
	fprintf(stderr, "[Raw] Initializing AF_PACKET networking on %s\n", s_if_name.c_str());

	// Set Mac ethernet address
	memcpy(ether_addr, s_mac_addr, 6);

	// Open raw socket
	s_raw_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (s_raw_fd < 0) {
		perror("[Raw] socket(AF_PACKET)");
		fprintf(stderr, "[Raw] Requires CAP_NET_RAW or root\n");
		return false;
	}

	// Get interface index
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, s_if_name.c_str(), IFNAMSIZ - 1);
	if (ioctl(s_raw_fd, SIOCGIFINDEX, &ifr) < 0) {
		perror("[Raw] ioctl(SIOCGIFINDEX)");
		close(s_raw_fd);
		s_raw_fd = -1;
		return false;
	}
	s_if_index = ifr.ifr_ifindex;

	// Bind to interface
	struct sockaddr_ll sll;
	memset(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_protocol = htons(ETH_P_ALL);
	sll.sll_ifindex = s_if_index;
	if (bind(s_raw_fd, (struct sockaddr*)&sll, sizeof(sll)) < 0) {
		perror("[Raw] bind");
		close(s_raw_fd);
		s_raw_fd = -1;
		return false;
	}

	// Enable promiscuous mode (to receive frames for our non-host MAC)
	struct packet_mreq mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.mr_ifindex = s_if_index;
	mreq.mr_type = PACKET_MR_PROMISC;
	if (setsockopt(s_raw_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
	               &mreq, sizeof(mreq)) < 0) {
		perror("[Raw] setsockopt(PACKET_MR_PROMISC)");
		// Non-fatal: will still receive broadcast/multicast
	}

	// Start receive thread
	s_running = true;
	s_rx_thread = std::thread(raw_rx_thread);

	fprintf(stderr, "[Raw] AF_PACKET networking ready on %s (ifindex=%d, MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
		s_if_name.c_str(), s_if_index,
		s_mac_addr[0], s_mac_addr[1], s_mac_addr[2],
		s_mac_addr[3], s_mac_addr[4], s_mac_addr[5]);

	return true;
}

static void ether_raw_exit(void)
{
	fprintf(stderr, "[Raw] Shutting down\n");
	s_running = false;
	if (s_rx_thread.joinable())
		s_rx_thread.join();
	if (s_raw_fd >= 0) {
		close(s_raw_fd);
		s_raw_fd = -1;
	}
}

static void ether_raw_reset(void)
{
	// Nothing to reset for raw sockets
}

static int16_t ether_raw_add_multicast(uint32_t pb)
{
	// TODO: Add multicast group to socket via PACKET_MR_MULTICAST
	(void)pb;
	return 0;
}

static int16_t ether_raw_del_multicast(uint32_t pb)
{
	(void)pb;
	return 0;
}

static int16_t ether_raw_attach_ph(uint16_t type, uint32_t handler)
{
	D(bug("[Raw] AttachPH type=%04x handler=%08x\n", type, handler));
	(void)type;
	(void)handler;
	return 0;
}

static int16_t ether_raw_detach_ph(uint16_t type)
{
	D(bug("[Raw] DetachPH type=%04x\n", type));
	(void)type;
	return 0;
}

static int16_t ether_raw_write(uint32_t wds)
{
	if (s_raw_fd < 0) return excessCollsns;

	// Linearize WDS into a flat ethernet frame
	uint8_t packet[1514];
	int len = ether_wds_to_buffer(wds, packet);
	if (len < 14) return eLenErr;

	D(bug("[Raw] TX %d bytes, type=%04x\n", len, (packet[12] << 8) | packet[13]));

	// Send directly on the wire
	struct sockaddr_ll sll;
	memset(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = s_if_index;
	sll.sll_halen = 6;
	memcpy(sll.sll_addr, packet, 6);  // Destination MAC

	ssize_t sent = sendto(s_raw_fd, packet, len, 0,
	                      (struct sockaddr*)&sll, sizeof(sll));
	if (sent < 0) {
		D(bug("[Raw] sendto failed: %s\n", strerror(errno)));
		return excessCollsns;
	}

	return 0;
}

static bool ether_raw_start_udp_thread(int socket_fd)
{
	(void)socket_fd;
	return false;  // Not used in raw mode
}

static void ether_raw_stop_udp_thread(void)
{
	// Not used in raw mode
}

// ---- Registration ----

void ether_raw_register(const char* if_name)
{
	s_if_name = if_name ? if_name : "eth0";

	g_platform.ether_init = ether_raw_init;
	g_platform.ether_exit = ether_raw_exit;
	g_platform.ether_reset = ether_raw_reset;
	g_platform.ether_add_multicast = ether_raw_add_multicast;
	g_platform.ether_del_multicast = ether_raw_del_multicast;
	g_platform.ether_attach_ph = ether_raw_attach_ph;
	g_platform.ether_detach_ph = ether_raw_detach_ph;
	g_platform.ether_write = ether_raw_write;
	g_platform.ether_start_udp_thread = ether_raw_start_udp_thread;
	g_platform.ether_stop_udp_thread = ether_raw_stop_udp_thread;
	g_platform.ether_interrupt = raw_deliver_frames;

	fprintf(stderr, "[Network] Raw AF_PACKET driver registered (interface: %s)\n",
		s_if_name.c_str());
}
