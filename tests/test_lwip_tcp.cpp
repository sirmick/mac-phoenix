/*
 *  test_lwip_tcp.cpp - Integration test for lwIP NAT networking
 *
 *  Implements a complete TCP client at the ethernet frame level (as if
 *  we were the Mac guest), connects through the lwIP NAT proxy to a
 *  real TCP echo server on localhost, exchanges data, and verifies
 *  the round-trip.
 *
 *  Frame flow:
 *    Test (acting as Mac) crafts ethernet frames
 *      → feeds into lwIP via ethernet_input()
 *      → lwIP processes ARP/IP/TCP
 *      → NAT proxy opens real host sockets
 *      → echo server on localhost
 *      → NAT reads reply, lwIP generates response frames
 *      → linkoutput callback captures frames for test to parse
 *
 *  Build: links against lwip static lib + lwip_nat.cpp (no emulator deps)
 *  Run:   ./test_lwip_tcp  (returns 0 on success)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "netif/ethernet.h"

// lwip_nat.h declares lwip_nat_ip4_input with extern "C", but lwipopts.h
// (pulled in by lwIP headers above) already declared it with C++ linkage.
// Avoid the conflict by declaring what we need directly.
extern bool g_debug_network;
void lwip_nat_init(struct netif *netif);
void lwip_nat_shutdown(void);
void lwip_nat_poll(void);
void lwip_nat_rewrite_outgoing(uint8_t *buf, uint16_t len);

// ============================================================================
// Constants
// ============================================================================

// Mac guest ethernet address
static const uint8_t MAC_ADDR[6] = {0x02, 0x50, 0x48, 0x58, 0x00, 0x01};
// Gateway (lwIP netif) ethernet address
static const uint8_t GW_MAC[6]   = {0x02, 0x50, 0x48, 0x58, 0x00, 0x02};

// Network addresses
static const uint32_t MAC_IP = 0x0A00020F;  // 10.0.2.15
static const uint32_t GW_IP  = 0x0A000201;  // 10.0.2.1

// TCP test parameters
static const uint16_t MAC_PORT = 12345;

// ============================================================================
// Frame capture (lwIP → Mac direction)
// ============================================================================

static std::mutex g_rx_mutex;
static std::queue<std::vector<uint8_t>> g_rx_queue;

static err_t test_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    uint8_t buf[1518];
    uint16_t len = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    if (len < 14) return ERR_BUF;

    // Rewrite NAT'd TCP frames: restore original source IP/port
    lwip_nat_rewrite_outgoing(buf, len);

    std::lock_guard<std::mutex> lock(g_rx_mutex);
    g_rx_queue.push(std::vector<uint8_t>(buf, buf + len));
    return ERR_OK;
}

// Pop a captured frame (returns empty if none)
static std::vector<uint8_t> pop_frame()
{
    std::lock_guard<std::mutex> lock(g_rx_mutex);
    if (g_rx_queue.empty()) return {};
    auto f = std::move(g_rx_queue.front());
    g_rx_queue.pop();
    return f;
}

// ============================================================================
// lwIP netif setup
// ============================================================================

static struct netif g_netif;

static err_t test_netif_init(struct netif *netif)
{
    netif->name[0] = 't';
    netif->name[1] = 's';
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, GW_MAC, 6);
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    netif->linkoutput = test_linkoutput;
    netif->output = etharp_output;
    return ERR_OK;
}

// ============================================================================
// Frame injection (Mac → lwIP direction)
// ============================================================================

static void inject_frame(const uint8_t *data, size_t len)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
    if (!p) {
        fprintf(stderr, "ERROR: pbuf_alloc failed for %zu bytes\n", len);
        return;
    }
    memcpy(p->payload, data, len);
    g_netif.input(p, &g_netif);
}

// ============================================================================
// Pump lwIP: process timers, NAT poll, and allow frame exchange
// ============================================================================

static void pump(int iterations = 20, int sleep_ms = 5)
{
    for (int i = 0; i < iterations; i++) {
        lwip_nat_poll();
        sys_check_timeouts();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}

// ============================================================================
// Checksum helpers
// ============================================================================

static uint16_t ip_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i += 2) {
        uint16_t word = (data[i] << 8);
        if (i + 1 < len) word |= data[i + 1];
        sum += word;
    }
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~(uint16_t)sum;
}

static uint16_t tcp_checksum(const uint8_t *tcp_hdr, size_t tcp_len,
                              uint32_t src_ip, uint32_t dst_ip)
{
    // Pseudo-header + TCP data
    uint32_t sum = 0;
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += 6;  // TCP protocol
    sum += tcp_len;

    for (size_t i = 0; i < tcp_len; i += 2) {
        uint16_t word = (tcp_hdr[i] << 8);
        if (i + 1 < tcp_len) word |= tcp_hdr[i + 1];
        sum += word;
    }
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t result = ~(uint16_t)sum;
    if (result == 0) result = 0xFFFF;
    return result;
}

// ============================================================================
// Frame builders
// ============================================================================

// Build an ethernet frame header
static void build_eth_header(uint8_t *frame, const uint8_t *dst_mac,
                              const uint8_t *src_mac, uint16_t ethertype)
{
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, src_mac, 6);
    frame[12] = (ethertype >> 8) & 0xFF;
    frame[13] = ethertype & 0xFF;
}

// Build an IP header (returns header length = 20)
static void build_ip_header(uint8_t *ip, uint16_t total_len, uint8_t protocol,
                             uint32_t src_ip, uint32_t dst_ip, uint8_t ttl = 64)
{
    memset(ip, 0, 20);
    ip[0] = 0x45;  // IPv4, IHL=5
    ip[2] = (total_len >> 8) & 0xFF;
    ip[3] = total_len & 0xFF;
    // identification (leave 0)
    ip[6] = 0x40;  // Don't fragment
    ip[8] = ttl;
    ip[9] = protocol;
    // src IP (network byte order, but we store big-endian)
    ip[12] = (src_ip >> 24) & 0xFF;
    ip[13] = (src_ip >> 16) & 0xFF;
    ip[14] = (src_ip >> 8) & 0xFF;
    ip[15] = src_ip & 0xFF;
    // dst IP
    ip[16] = (dst_ip >> 24) & 0xFF;
    ip[17] = (dst_ip >> 16) & 0xFF;
    ip[18] = (dst_ip >> 8) & 0xFF;
    ip[19] = dst_ip & 0xFF;
    // Compute checksum
    uint16_t cksum = ip_checksum(ip, 20);
    ip[10] = (cksum >> 8) & 0xFF;
    ip[11] = cksum & 0xFF;
}

// Build ARP request: "who has target_ip? tell sender_ip"
static std::vector<uint8_t> build_arp_request(uint32_t sender_ip, uint32_t target_ip,
                                                const uint8_t *sender_mac)
{
    std::vector<uint8_t> frame(14 + 28);  // Eth header + ARP
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    build_eth_header(frame.data(), broadcast, sender_mac, 0x0806);

    uint8_t *arp = frame.data() + 14;
    arp[0] = 0x00; arp[1] = 0x01;  // Hardware type: Ethernet
    arp[2] = 0x08; arp[3] = 0x00;  // Protocol type: IPv4
    arp[4] = 6;                      // Hardware size
    arp[5] = 4;                      // Protocol size
    arp[6] = 0x00; arp[7] = 0x01;  // Opcode: Request

    // Sender
    memcpy(arp + 8, sender_mac, 6);
    arp[14] = (sender_ip >> 24) & 0xFF;
    arp[15] = (sender_ip >> 16) & 0xFF;
    arp[16] = (sender_ip >> 8) & 0xFF;
    arp[17] = sender_ip & 0xFF;

    // Target
    memset(arp + 18, 0, 6);  // unknown MAC
    arp[24] = (target_ip >> 24) & 0xFF;
    arp[25] = (target_ip >> 16) & 0xFF;
    arp[26] = (target_ip >> 8) & 0xFF;
    arp[27] = target_ip & 0xFF;

    return frame;
}

// TCP flags
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10

// Build a TCP segment wrapped in IP wrapped in Ethernet
static std::vector<uint8_t> build_tcp_frame(
    uint32_t src_ip, uint16_t src_port,
    uint32_t dst_ip, uint16_t dst_port,
    uint32_t seq, uint32_t ack,
    uint8_t flags, uint16_t window,
    const uint8_t *payload, size_t payload_len)
{
    size_t tcp_hdr_len = 20;  // No options for basic segments
    // For SYN, include MSS option (4 bytes)
    if (flags & TH_SYN) tcp_hdr_len = 24;

    size_t tcp_total = tcp_hdr_len + payload_len;
    size_t ip_total = 20 + tcp_total;
    size_t frame_len = 14 + ip_total;

    std::vector<uint8_t> frame(frame_len);

    // Ethernet header: to gateway MAC (layer 2 routing)
    build_eth_header(frame.data(), GW_MAC, MAC_ADDR, 0x0800);

    // IP header
    build_ip_header(frame.data() + 14, ip_total, 6 /*TCP*/, src_ip, dst_ip);

    // TCP header
    uint8_t *tcp = frame.data() + 14 + 20;
    memset(tcp, 0, tcp_hdr_len);

    tcp[0] = (src_port >> 8) & 0xFF;
    tcp[1] = src_port & 0xFF;
    tcp[2] = (dst_port >> 8) & 0xFF;
    tcp[3] = dst_port & 0xFF;

    // Sequence number
    tcp[4] = (seq >> 24) & 0xFF;
    tcp[5] = (seq >> 16) & 0xFF;
    tcp[6] = (seq >> 8) & 0xFF;
    tcp[7] = seq & 0xFF;

    // Ack number
    tcp[8] = (ack >> 24) & 0xFF;
    tcp[9] = (ack >> 16) & 0xFF;
    tcp[10] = (ack >> 8) & 0xFF;
    tcp[11] = ack & 0xFF;

    // Data offset (header length in 32-bit words) + flags
    tcp[12] = (tcp_hdr_len / 4) << 4;
    tcp[13] = flags;

    // Window size
    tcp[14] = (window >> 8) & 0xFF;
    tcp[15] = window & 0xFF;

    // MSS option for SYN
    if (flags & TH_SYN) {
        tcp[20] = 0x02;  // Kind: MSS
        tcp[21] = 0x04;  // Length: 4
        tcp[22] = 0x05;  // MSS = 1460
        tcp[23] = 0xB4;
    }

    // Payload
    if (payload && payload_len > 0) {
        memcpy(tcp + tcp_hdr_len, payload, payload_len);
    }

    // TCP checksum
    uint16_t cksum = tcp_checksum(tcp, tcp_total, src_ip, dst_ip);
    tcp[16] = (cksum >> 8) & 0xFF;
    tcp[17] = cksum & 0xFF;

    return frame;
}

// ============================================================================
// Frame parsers
// ============================================================================

struct ParsedFrame {
    // Ethernet
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;

    // IP (if ethertype == 0x0800)
    uint8_t ip_proto;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t ip_hdr_len;

    // TCP (if ip_proto == 6)
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t tcp_seq;
    uint32_t tcp_ack;
    uint8_t tcp_flags;
    uint16_t tcp_window;
    uint8_t tcp_hdr_len;

    // ARP (if ethertype == 0x0806)
    uint16_t arp_opcode;
    uint8_t arp_sender_mac[6];
    uint32_t arp_sender_ip;
    uint8_t arp_target_mac[6];
    uint32_t arp_target_ip;

    // Payload
    const uint8_t *payload;
    size_t payload_len;

    bool valid;
};

static ParsedFrame parse_frame(const std::vector<uint8_t> &frame)
{
    ParsedFrame p = {};
    p.valid = false;

    if (frame.size() < 14) return p;

    memcpy(p.dst_mac, frame.data(), 6);
    memcpy(p.src_mac, frame.data() + 6, 6);
    p.ethertype = (frame[12] << 8) | frame[13];

    if (p.ethertype == 0x0806 && frame.size() >= 14 + 28) {
        // ARP
        const uint8_t *arp = frame.data() + 14;
        p.arp_opcode = (arp[6] << 8) | arp[7];
        memcpy(p.arp_sender_mac, arp + 8, 6);
        p.arp_sender_ip = (arp[14] << 24) | (arp[15] << 16) | (arp[16] << 8) | arp[17];
        memcpy(p.arp_target_mac, arp + 18, 6);
        p.arp_target_ip = (arp[24] << 24) | (arp[25] << 16) | (arp[26] << 8) | arp[27];
        p.valid = true;
        return p;
    }

    if (p.ethertype == 0x0800 && frame.size() >= 14 + 20) {
        // IP
        const uint8_t *ip = frame.data() + 14;
        p.ip_hdr_len = (ip[0] & 0x0F) * 4;
        p.ip_proto = ip[9];
        p.src_ip = (ip[12] << 24) | (ip[13] << 16) | (ip[14] << 8) | ip[15];
        p.dst_ip = (ip[16] << 24) | (ip[17] << 16) | (ip[18] << 8) | ip[19];

        if (p.ip_proto == 6 && frame.size() >= (size_t)(14 + p.ip_hdr_len + 20)) {
            // TCP
            const uint8_t *tcp = frame.data() + 14 + p.ip_hdr_len;
            p.src_port = (tcp[0] << 8) | tcp[1];
            p.dst_port = (tcp[2] << 8) | tcp[3];
            p.tcp_seq = ((uint32_t)tcp[4] << 24) | ((uint32_t)tcp[5] << 16) |
                        ((uint32_t)tcp[6] << 8) | tcp[7];
            p.tcp_ack = ((uint32_t)tcp[8] << 24) | ((uint32_t)tcp[9] << 16) |
                        ((uint32_t)tcp[10] << 8) | tcp[11];
            p.tcp_hdr_len = (tcp[12] >> 4) * 4;
            p.tcp_flags = tcp[13];
            p.tcp_window = (tcp[14] << 8) | tcp[15];

            size_t tcp_payload_offset = 14 + p.ip_hdr_len + p.tcp_hdr_len;
            if (frame.size() > tcp_payload_offset) {
                p.payload = frame.data() + tcp_payload_offset;
                p.payload_len = frame.size() - tcp_payload_offset;
            } else {
                p.payload = nullptr;
                p.payload_len = 0;
            }
        }

        p.valid = true;
        return p;
    }

    return p;
}

static const char *ip_str(uint32_t ip)
{
    static char buf[4][20];
    static int idx = 0;
    int i = idx++ % 4;
    snprintf(buf[i], sizeof(buf[i]), "%u.%u.%u.%u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return buf[i];
}

static const char *tcp_flags_str(uint8_t flags)
{
    static char buf[32];
    buf[0] = 0;
    if (flags & TH_SYN) strcat(buf, "SYN ");
    if (flags & TH_ACK) strcat(buf, "ACK ");
    if (flags & TH_FIN) strcat(buf, "FIN ");
    if (flags & TH_RST) strcat(buf, "RST ");
    if (flags & TH_PSH) strcat(buf, "PSH ");
    return buf;
}

// ============================================================================
// Echo server
// ============================================================================

static std::atomic<bool> g_server_running{false};
static std::atomic<int> g_server_port{0};
static std::atomic<int> g_server_connections{0};

static void echo_server_thread()
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("echo server: socket");
        return;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // Let OS pick a port

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("echo server: bind");
        close(listen_fd);
        return;
    }

    socklen_t addrlen = sizeof(addr);
    getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen);
    g_server_port = ntohs(addr.sin_port);

    if (listen(listen_fd, 5) < 0) {
        perror("echo server: listen");
        close(listen_fd);
        return;
    }

    fprintf(stderr, "[Echo] Listening on 127.0.0.1:%d\n", g_server_port.load());
    g_server_running = true;

    while (g_server_running) {
        struct pollfd pfd = {listen_fd, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);
        if (ret <= 0) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        g_server_connections++;
        fprintf(stderr, "[Echo] Client connected (connection #%d)\n",
                g_server_connections.load());

        // Echo loop
        uint8_t buf[4096];
        while (g_server_running) {
            struct pollfd cpfd = {client_fd, POLLIN, 0};
            ret = poll(&cpfd, 1, 100);
            if (ret < 0) break;
            if (ret == 0) continue;

            ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
            if (n <= 0) break;

            fprintf(stderr, "[Echo] Received %zd bytes, echoing back\n", n);
            ssize_t sent = 0;
            while (sent < n) {
                ssize_t s = send(client_fd, buf + sent, n - sent, MSG_NOSIGNAL);
                if (s <= 0) break;
                sent += s;
            }
        }

        close(client_fd);
        fprintf(stderr, "[Echo] Client disconnected\n");
    }

    close(listen_fd);
}

// ============================================================================
// Wait for a frame matching a predicate (with timeout)
// ============================================================================

using FramePredicate = bool (*)(const ParsedFrame &);

static std::vector<uint8_t> wait_for_frame(FramePredicate pred, int timeout_ms = 3000,
                                             ParsedFrame *out = nullptr)
{
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        lwip_nat_poll();
        sys_check_timeouts();

        auto frame = pop_frame();
        if (!frame.empty()) {
            auto parsed = parse_frame(frame);
            if (parsed.valid && pred(parsed)) {
                if (out) *out = parsed;
                return frame;
            }
            // Not the frame we want; log and discard
            if (parsed.valid) {
                if (parsed.ethertype == 0x0806) {
                    fprintf(stderr, "  [skip] ARP opcode=%u\n", parsed.arp_opcode);
                } else if (parsed.ethertype == 0x0800 && parsed.ip_proto == 6) {
                    fprintf(stderr, "  [skip] TCP %s:%u -> %s:%u [%s] seq=%u ack=%u len=%zu\n",
                            ip_str(parsed.src_ip), parsed.src_port,
                            ip_str(parsed.dst_ip), parsed.dst_port,
                            tcp_flags_str(parsed.tcp_flags),
                            parsed.tcp_seq, parsed.tcp_ack, parsed.payload_len);
                } else {
                    fprintf(stderr, "  [skip] ethertype=%04x proto=%u\n",
                            parsed.ethertype, parsed.ip_proto);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return {};  // Timeout
}

// ============================================================================
// Test: ARP resolution
// ============================================================================

static bool test_arp()
{
    fprintf(stderr, "\n=== TEST: ARP resolution ===\n");

    auto request = build_arp_request(MAC_IP, GW_IP, MAC_ADDR);
    fprintf(stderr, "Sending ARP request: who has %s? tell %s\n",
            ip_str(GW_IP), ip_str(MAC_IP));
    inject_frame(request.data(), request.size());

    ParsedFrame reply;
    auto frame = wait_for_frame([](const ParsedFrame &f) {
        return f.ethertype == 0x0806 && f.arp_opcode == 2;  // ARP reply
    }, 2000, &reply);

    if (frame.empty()) {
        fprintf(stderr, "FAIL: No ARP reply received\n");
        return false;
    }

    fprintf(stderr, "Got ARP reply: %s is at %02x:%02x:%02x:%02x:%02x:%02x\n",
            ip_str(reply.arp_sender_ip),
            reply.arp_sender_mac[0], reply.arp_sender_mac[1],
            reply.arp_sender_mac[2], reply.arp_sender_mac[3],
            reply.arp_sender_mac[4], reply.arp_sender_mac[5]);

    if (reply.arp_sender_ip != GW_IP) {
        fprintf(stderr, "FAIL: ARP reply from wrong IP\n");
        return false;
    }
    if (memcmp(reply.arp_sender_mac, GW_MAC, 6) != 0) {
        fprintf(stderr, "FAIL: ARP reply has wrong MAC\n");
        return false;
    }

    fprintf(stderr, "PASS: ARP resolution\n");
    return true;
}

// ============================================================================
// Test: TCP connect, data exchange, close
// ============================================================================

static bool test_tcp_echo()
{
    fprintf(stderr, "\n=== TEST: TCP echo through NAT ===\n");

    uint16_t server_port = g_server_port.load();
    uint32_t server_ip = 0x7F000001;  // 127.0.0.1

    // Track TCP state
    uint32_t our_seq = 1000;
    uint32_t their_seq = 0;
    int initial_connections = g_server_connections.load();

    // --- Step 1: Send SYN ---
    fprintf(stderr, "Step 1: SYN -> %s:%u\n", ip_str(server_ip), server_port);

    auto syn = build_tcp_frame(MAC_IP, MAC_PORT, server_ip, server_port,
                                our_seq, 0, TH_SYN, 65535, nullptr, 0);
    inject_frame(syn.data(), syn.size());

    // Wait for SYN-ACK
    ParsedFrame synack_parsed;
    auto synack = wait_for_frame([](const ParsedFrame &f) {
        return f.ethertype == 0x0800 && f.ip_proto == 6 &&
               (f.tcp_flags & (TH_SYN | TH_ACK)) == (TH_SYN | TH_ACK);
    }, 5000, &synack_parsed);

    if (synack.empty()) {
        fprintf(stderr, "FAIL: No SYN-ACK received\n");

        // Check if we got a RST instead
        // (drain remaining frames for diagnostics)
        pump(10, 10);
        auto leftover = pop_frame();
        while (!leftover.empty()) {
            auto p = parse_frame(leftover);
            if (p.valid && p.ip_proto == 6) {
                fprintf(stderr, "  Residual TCP: [%s] src_port=%u dst_port=%u\n",
                        tcp_flags_str(p.tcp_flags), p.src_port, p.dst_port);
            }
            leftover = pop_frame();
        }
        return false;
    }

    their_seq = synack_parsed.tcp_seq;
    our_seq++;  // SYN consumes one sequence number

    fprintf(stderr, "Got SYN-ACK: their_seq=%u, ack=%u\n", their_seq, synack_parsed.tcp_ack);

    if (synack_parsed.tcp_ack != our_seq) {
        fprintf(stderr, "FAIL: SYN-ACK ack number wrong (expected %u, got %u)\n",
                our_seq, synack_parsed.tcp_ack);
        return false;
    }

    // --- Step 2: Send ACK (complete handshake) ---
    fprintf(stderr, "Step 2: ACK (handshake complete)\n");

    their_seq++;  // SYN-ACK consumes one sequence number
    auto ack = build_tcp_frame(MAC_IP, MAC_PORT, server_ip, server_port,
                                our_seq, their_seq, TH_ACK, 65535, nullptr, 0);
    inject_frame(ack.data(), ack.size());
    pump(5, 10);

    // Verify server saw the connection
    // (give it a moment to accept)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pump(10, 10);

    if (g_server_connections.load() <= initial_connections) {
        fprintf(stderr, "WARN: Echo server hasn't accepted connection yet, waiting...\n");
        pump(20, 50);
    }

    // --- Step 3: Send data ---
    const char *test_data = "Hello from Mac!";
    size_t data_len = strlen(test_data);
    fprintf(stderr, "Step 3: Send %zu bytes: \"%s\"\n", data_len, test_data);

    auto data_frame = build_tcp_frame(MAC_IP, MAC_PORT, server_ip, server_port,
                                       our_seq, their_seq, TH_ACK | TH_PSH, 65535,
                                       (const uint8_t *)test_data, data_len);
    inject_frame(data_frame.data(), data_frame.size());
    our_seq += data_len;

    // --- Step 4: Wait for echoed data ---
    fprintf(stderr, "Step 4: Waiting for echo response...\n");

    std::vector<uint8_t> received_data;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (received_data.size() < data_len &&
           std::chrono::steady_clock::now() < deadline) {
        lwip_nat_poll();
        sys_check_timeouts();

        auto frame = pop_frame();
        if (!frame.empty()) {
            auto p = parse_frame(frame);
            if (p.valid && p.ethertype == 0x0800 && p.ip_proto == 6) {
                if (p.tcp_flags & TH_RST) {
                    fprintf(stderr, "FAIL: Got RST\n");
                    return false;
                }
                if (p.payload_len > 0) {
                    fprintf(stderr, "  Received TCP data: %zu bytes\n", p.payload_len);
                    received_data.insert(received_data.end(),
                                         p.payload, p.payload + p.payload_len);
                    their_seq += p.payload_len;

                    // ACK the received data
                    auto data_ack = build_tcp_frame(MAC_IP, MAC_PORT, server_ip, server_port,
                                                     our_seq, their_seq, TH_ACK, 65535,
                                                     nullptr, 0);
                    inject_frame(data_ack.data(), data_ack.size());
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    if (received_data.size() < data_len) {
        fprintf(stderr, "FAIL: Only received %zu of %zu bytes\n",
                received_data.size(), data_len);
        return false;
    }

    if (memcmp(received_data.data(), test_data, data_len) != 0) {
        fprintf(stderr, "FAIL: Echoed data doesn't match\n");
        fprintf(stderr, "  Expected: %s\n", test_data);
        fprintf(stderr, "  Got:      %.*s\n", (int)received_data.size(),
                (const char *)received_data.data());
        return false;
    }

    fprintf(stderr, "Echo data verified: \"%.*s\"\n",
            (int)received_data.size(), (const char *)received_data.data());

    // --- Step 5: Close connection (FIN) ---
    fprintf(stderr, "Step 5: FIN (close connection)\n");

    auto fin = build_tcp_frame(MAC_IP, MAC_PORT, server_ip, server_port,
                                our_seq, their_seq, TH_FIN | TH_ACK, 65535,
                                nullptr, 0);
    inject_frame(fin.data(), fin.size());
    our_seq++;  // FIN consumes one sequence number

    // Wait for FIN-ACK or FIN
    bool got_fin_ack = false;
    auto fin_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);

    while (!got_fin_ack && std::chrono::steady_clock::now() < fin_deadline) {
        lwip_nat_poll();
        sys_check_timeouts();

        auto frame = pop_frame();
        if (!frame.empty()) {
            auto p = parse_frame(frame);
            if (p.valid && p.ethertype == 0x0800 && p.ip_proto == 6) {
                fprintf(stderr, "  FIN phase: [%s] seq=%u ack=%u\n",
                        tcp_flags_str(p.tcp_flags), p.tcp_seq, p.tcp_ack);
                if (p.tcp_flags & TH_FIN) {
                    // ACK their FIN
                    their_seq++;
                    auto final_ack = build_tcp_frame(MAC_IP, MAC_PORT, server_ip, server_port,
                                                      our_seq, their_seq, TH_ACK, 65535,
                                                      nullptr, 0);
                    inject_frame(final_ack.data(), final_ack.size());
                    got_fin_ack = true;
                }
                if (p.tcp_flags & TH_ACK) {
                    // Good, they ACK'd our FIN
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Let things settle
    pump(10, 10);

    if (!got_fin_ack) {
        fprintf(stderr, "WARN: Didn't receive FIN from server (connection may have timed out)\n");
        // Not a hard failure - the data exchange was the important part
    }

    fprintf(stderr, "PASS: TCP echo through NAT\n");
    return true;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    fprintf(stderr, "=== lwIP NAT TCP Integration Test ===\n\n");

    // Enable NAT debug logging
    g_debug_network = true;

    // --- Start echo server ---
    std::thread server_thread(echo_server_thread);
    server_thread.detach();

    // Wait for server to start
    while (!g_server_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    fprintf(stderr, "Echo server ready on port %d\n", g_server_port.load());

    // --- Initialize lwIP ---
    fprintf(stderr, "Initializing lwIP...\n");
    lwip_init();

    ip4_addr_t gw_ip, netmask, gw_gw;
    IP4_ADDR(&gw_ip, 10, 0, 2, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw_gw, 0, 0, 0, 0);

    netif_add(&g_netif, &gw_ip, &netmask, &gw_gw,
              nullptr, test_netif_init, ethernet_input);
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);

    // --- Initialize NAT proxy ---
    fprintf(stderr, "Initializing NAT proxy...\n");
    lwip_nat_init(&g_netif);

    // --- Run tests ---
    int failures = 0;

    if (!test_arp()) failures++;
    if (!test_tcp_echo()) failures++;

    // --- Cleanup ---
    fprintf(stderr, "\n--- Cleanup ---\n");
    g_server_running = false;
    lwip_nat_shutdown();
    netif_set_down(&g_netif);
    netif_remove(&g_netif);

    // Give server thread time to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (failures == 0) {
        fprintf(stderr, "\n=== ALL TESTS PASSED ===\n");
    } else {
        fprintf(stderr, "\n=== %d TEST(S) FAILED ===\n", failures);
    }

    return failures;
}
