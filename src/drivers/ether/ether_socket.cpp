/*
 *  ether_socket.cpp - Unix socket ethernet driver
 *
 *  Connects to a net-bridge process via Unix domain socket.
 *  Ethernet frames are exchanged with 4-byte big-endian length prefix.
 *  The bridge handles TCP/IP, NAT, DNS, and HTTPS proxy.
 *
 *  If the bridge binary is found, it is auto-launched on init.
 *
 *  Architecture:
 *    Mac ethernet frames → Unix socket → net-bridge (smoltcp + NAT) → real network
 *    Real network → net-bridge → Unix socket → EtherInterrupt → Mac
 */

#include "sysdeps.h"
#include "platform.h"
#include "ether_socket.h"
#include "cpu_emulation.h"
#include "main.h"
#include "ether.h"
#include "ether_defs.h"
#include "uae_wrapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <vector>
#include <unistd.h>
#include <climits>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>

#define DEBUG 0
#include "debug.h"

// ---- Internal state ----

static std::string s_sock_path;
static int s_sock_fd = -1;
static pid_t s_bridge_pid = -1;
static std::atomic<bool> s_running{false};
static std::thread s_rx_thread;

// Mac's ethernet address (must match net-bridge's MAC_ADDR)
static uint8_t s_mac_addr[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x01};

// Queue of frames to deliver to the Mac
static std::mutex s_rx_mutex;
static std::queue<std::vector<uint8_t>> s_rx_queue;

// ---- Wire protocol helpers ----

static bool send_frame(int fd, const uint8_t *data, int len)
{
	uint8_t header[4];
	uint32_t net_len = htonl((uint32_t)len);
	memcpy(header, &net_len, 4);

	if (write(fd, header, 4) != 4) return false;
	if (write(fd, data, len) != len) return false;
	return true;
}

static int recv_frame(int fd, uint8_t *buf, int buf_size)
{
	uint8_t header[4];
	ssize_t n = read(fd, header, 4);
	if (n != 4) return -1;

	uint32_t net_len;
	memcpy(&net_len, header, 4);
	int frame_len = (int)ntohl(net_len);

	if (frame_len <= 0 || frame_len > buf_size) return -1;

	int total = 0;
	while (total < frame_len) {
		n = read(fd, buf + total, frame_len - total);
		if (n <= 0) return -1;
		total += (int)n;
	}
	return frame_len;
}

// ---- Bridge process management ----

// Return the directory of the running mac-phoenix executable (via
// /proc/self/exe on Linux, dladdr elsewhere). Empty string on failure.
static std::string exe_dir()
{
#ifdef __linux__
	char buf[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = 0;
		std::string p(buf);
		size_t slash = p.rfind('/');
		if (slash != std::string::npos) return p.substr(0, slash);
	}
#endif
	return "";
}

static bool is_executable_file(const std::string& p)
{
	struct stat st;
	if (stat(p.c_str(), &st) != 0) return false;
	if (!S_ISREG(st.st_mode)) return false;
	return access(p.c_str(), X_OK) == 0;
}

static std::string find_bridge_binary()
{
	// Single canonical location: right next to the mac-phoenix binary.
	// CMake drops a symlink to the Rust release artifact there; installed
	// bundles ship the two side by side. No CWD-dependent search paths.
	std::string exe = exe_dir();
	if (!exe.empty()) {
		std::string p = exe + "/net-bridge";
		if (is_executable_file(p)) return p;
	}
	// System fallbacks for installed packages (.deb / .rpm → /usr/bin,
	// `make install` without prefix → /usr/local/bin).
	if (is_executable_file("/usr/local/bin/net-bridge")) {
		return "/usr/local/bin/net-bridge";
	}
	if (is_executable_file("/usr/bin/net-bridge")) {
		return "/usr/bin/net-bridge";
	}
	return "";
}

static bool launch_bridge()
{
	std::string binary = find_bridge_binary();
	if (binary.empty()) {
		fprintf(stderr, "[Socket] net-bridge binary not found, expecting external bridge\n");
		return false;
	}

	// Remove stale socket
	unlink(s_sock_path.c_str());

	pid_t pid = fork();
	if (pid < 0) {
		perror("[Socket] fork");
		return false;
	}

	if (pid == 0) {
		const char *argv[] = {
			"net-bridge",
			"--socket", s_sock_path.c_str(),
			nullptr,
		};
		execvp(binary.c_str(), const_cast<char * const *>(argv));
		perror("[Socket] exec net-bridge");
		_exit(1);
	}

	s_bridge_pid = pid;
	fprintf(stderr, "[Socket] Launched net-bridge (pid=%d): %s --socket %s\n",
		pid, binary.c_str(), s_sock_path.c_str());

	// Wait for socket to appear (up to 5 seconds)
	for (int i = 0; i < 50; i++) {
		if (access(s_sock_path.c_str(), F_OK) == 0) {
			return true;
		}
		usleep(100000);  // 100ms
	}

	fprintf(stderr, "[Socket] Timeout waiting for bridge socket\n");
	kill(s_bridge_pid, SIGTERM);
	waitpid(s_bridge_pid, nullptr, 0);
	s_bridge_pid = -1;
	return false;
}

static void stop_bridge()
{
	if (s_bridge_pid > 0) {
		pid_t pid = s_bridge_pid;
		s_bridge_pid = -1;
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		fprintf(stderr, "[Socket] Bridge process stopped\n");
	}
}

// ---- Frame delivery (called from EtherInterrupt context) ----

static void socket_deliver_frames()
{
	std::lock_guard<std::mutex> lock(s_rx_mutex);
	while (!s_rx_queue.empty()) {
		auto &frame = s_rx_queue.front();
		if (frame.size() >= 14 && ether_data) {
			EthernetPacket pkt;
			Host2Mac_memcpy(pkt.addr(), frame.data(), frame.size());
			ether_udp_read(pkt.addr(), frame.size(), nullptr);
		}
		s_rx_queue.pop();
	}
}

// ---- Receive thread ----

static void socket_rx_thread()
{
	fprintf(stderr, "[Socket] Receive thread started\n");
	uint8_t buf[1518];

	while (s_running) {
		struct pollfd pfd = {s_sock_fd, POLLIN, 0};
		int ret = poll(&pfd, 1, 100);
		if (ret <= 0) continue;

		int len = recv_frame(s_sock_fd, buf, sizeof(buf));
		if (len < 14) {
			if (len < 0) {
				fprintf(stderr, "[Socket] Connection to bridge lost\n");
				s_running = false;
				break;
			}
			continue;
		}

		D(bug("[Socket] RX %d bytes, type=%04x\n", len, (buf[12] << 8) | buf[13]));

		{
			std::lock_guard<std::mutex> lock(s_rx_mutex);
			s_rx_queue.push(std::vector<uint8_t>(buf, buf + len));
		}
		SetInterruptFlag(INTFLAG_ETHER);
		TriggerInterrupt();
	}
	fprintf(stderr, "[Socket] Receive thread stopped\n");
}

// ---- Platform driver interface ----

static bool ether_socket_init(void)
{
	fprintf(stderr, "[Socket] Initializing Unix socket networking\n");

	// Set Mac ethernet address
	memcpy(ether_addr, s_mac_addr, 6);

	// Always launch bridge (cleans up stale sockets)
	if (!launch_bridge()) {
		// Bridge launch failed — try connecting to an existing one
		if (access(s_sock_path.c_str(), F_OK) != 0) {
			fprintf(stderr, "[Socket] Cannot start bridge and socket %s not found\n",
				s_sock_path.c_str());
			return false;
		}
	}

	// Connect to Unix socket (retry briefly for bridge startup)
	s_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s_sock_fd < 0) {
		perror("[Socket] socket(AF_UNIX)");
		return false;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, s_sock_path.c_str(), sizeof(addr.sun_path) - 1);

	if (connect(s_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("[Socket] connect");
		close(s_sock_fd);
		s_sock_fd = -1;
		return false;
	}

	fprintf(stderr, "[Socket] Connected to bridge at %s\n", s_sock_path.c_str());

	// Start receive thread
	s_running = true;
	s_rx_thread = std::thread(socket_rx_thread);

	// Register atexit handler for clean shutdown
	static bool atexit_registered = false;
	if (!atexit_registered) {
		atexit([]() {
			s_running = false;
			if (s_rx_thread.joinable())
				s_rx_thread.join();
			if (s_sock_fd >= 0) {
				close(s_sock_fd);
				s_sock_fd = -1;
			}
			stop_bridge();
		});
		atexit_registered = true;
	}

	fprintf(stderr, "[Socket] Networking ready (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
		s_mac_addr[0], s_mac_addr[1], s_mac_addr[2],
		s_mac_addr[3], s_mac_addr[4], s_mac_addr[5]);
	return true;
}

static void ether_socket_exit(void)
{
	fprintf(stderr, "[Socket] Shutting down\n");
	s_running = false;

	if (s_rx_thread.joinable())
		s_rx_thread.join();

	if (s_sock_fd >= 0) {
		close(s_sock_fd);
		s_sock_fd = -1;
	}

	stop_bridge();

	{
		std::lock_guard<std::mutex> lock(s_rx_mutex);
		while (!s_rx_queue.empty()) s_rx_queue.pop();
	}

	fprintf(stderr, "[Socket] Shutdown complete\n");
}

static void ether_socket_reset(void)
{
	std::lock_guard<std::mutex> lock(s_rx_mutex);
	while (!s_rx_queue.empty()) s_rx_queue.pop();
}

static int16_t ether_socket_add_multicast(uint32_t pb) { (void)pb; return 0; }
static int16_t ether_socket_del_multicast(uint32_t pb) { (void)pb; return 0; }

static int16_t ether_socket_attach_ph(uint16_t type, uint32_t handler)
{
	D(bug("[Socket] AttachPH type=%04x handler=%08x\n", type, handler));
	ether_register_protocol(type, handler);
	return 0;
}

static int16_t ether_socket_detach_ph(uint16_t type)
{
	D(bug("[Socket] DetachPH type=%04x\n", type));
	ether_unregister_protocol(type);
	return 0;
}

static int16_t ether_socket_write(uint32_t wds)
{
	if (s_sock_fd < 0) return excessCollsns;

	uint8_t packet[1514];
	int len = ether_wds_to_buffer(wds, packet);
	if (len < 14) return eLenErr;

	D(bug("[Socket] TX %d bytes, type=%04x\n", len, (packet[12] << 8) | packet[13]));

	if (!send_frame(s_sock_fd, packet, len)) {
		D(bug("[Socket] send_frame failed\n"));
		return excessCollsns;
	}

	return 0;
}

static bool ether_socket_start_udp_thread(int socket_fd) { (void)socket_fd; return false; }
static void ether_socket_stop_udp_thread(void) {}

// ---- Public C entry points (used by PPC NDRV path) ----

extern "C" bool ether_socket_send_raw(const uint8_t *frame, uint32_t len)
{
	if (s_sock_fd < 0 || !frame || len < 14)
		return false;
	return send_frame(s_sock_fd, frame, (int)len);
}

extern "C" void ether_socket_drain_rx(ether_socket_frame_handler_t handler)
{
	if (!handler)
		return;
	std::lock_guard<std::mutex> lock(s_rx_mutex);
	while (!s_rx_queue.empty()) {
		auto &frame = s_rx_queue.front();
		if (frame.size() >= 14)
			handler(frame.data(), (uint32_t)frame.size());
		s_rx_queue.pop();
	}
}

extern "C" void ether_socket_get_mac(uint8_t mac[6])
{
	memcpy(mac, s_mac_addr, 6);
}

// ---- Registration ----

void ether_socket_register(const char *sock_path)
{
	s_sock_path = sock_path ? sock_path : "/tmp/mac-ether.sock";

	g_platform.ether_init = ether_socket_init;
	g_platform.ether_exit = ether_socket_exit;
	g_platform.ether_reset = ether_socket_reset;
	g_platform.ether_add_multicast = ether_socket_add_multicast;
	g_platform.ether_del_multicast = ether_socket_del_multicast;
	g_platform.ether_attach_ph = ether_socket_attach_ph;
	g_platform.ether_detach_ph = ether_socket_detach_ph;
	g_platform.ether_write = ether_socket_write;
	g_platform.ether_start_udp_thread = ether_socket_start_udp_thread;
	g_platform.ether_stop_udp_thread = ether_socket_stop_udp_thread;
	g_platform.ether_interrupt = socket_deliver_frames;

	fprintf(stderr, "[Network] Unix socket driver registered (path: %s)\n",
		s_sock_path.c_str());
}
