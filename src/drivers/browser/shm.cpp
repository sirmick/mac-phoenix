/*
 *  shm.cpp — host side of the BrowserShm handshake. See shm.h.
 */
#include "shm.h"
#include "ring.h"

#define BR_HOST 1
#include "MacBrowser.h"
#include "memory_access.h"
#include "emulator_config.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>

namespace browser {

namespace {

std::thread g_thread;
std::atomic<bool>      g_running{false};
std::atomic<BrowserShm*> g_shm{nullptr};

/* Resolve the host filesystem path of <bridge_dir>/browser_shm.txt.
 * Returns empty string if the bridge isn't configured. */
std::string handshake_path()
{
    const auto& cfg = config::EmulatorConfig::instance();
    if (cfg.bridge_dir.empty()) return {};
    return cfg.bridge_dir + "/" + BR_HANDSHAKE_FILE;
}

/* Parse 8 hex chars (with optional 0x prefix and trailing whitespace)
 * into a Mac address. Returns 0 on parse failure. */
uint32_t parse_hex_addr(const std::string& s)
{
    const char* p = s.c_str();
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    char* end = nullptr;
    unsigned long v = strtoul(p, &end, 16);
    if (end == p) return 0;
    if (v > 0xFFFFFFFFul) return 0;
    return (uint32_t)v;
}

/* Read the file into a string. Returns empty on missing/error. */
std::string slurp(const std::string& path)
{
    std::ifstream f(path);
    if (!f.good()) return {};
    std::string out;
    std::getline(f, out);
    return out;
}

/* Validate handshake by mapping the published Mac address to a host
 * pointer and reading magic + version. Returns the pointer on success
 * or nullptr on any failure. */
BrowserShm* try_resolve(uint32_t mac_addr)
{
    if (mac_addr == 0) return nullptr;
    /* Bound check: BrowserShm needs sizeof(BrowserShm) bytes contiguous,
     * so the high end must also resolve. Probe both endpoints. */
    uint8_t* base = Mac2HostAddr(mac_addr);
    uint8_t* end  = Mac2HostAddr(mac_addr + (uint32_t)sizeof(BrowserShm) - 1);
    if (!base || !end) {
        fprintf(stderr, "[BrowserShm] handshake: addr 0x%08x doesn't map "
                "to host RAM (mac_addr+sizeof out of range)\n", mac_addr);
        return nullptr;
    }
    BrowserShm* shm = (BrowserShm*)base;
    uint32_t magic   = br_u32_load(&shm->magic);
    uint32_t version = br_u32_load(&shm->version);
    if (magic != BR_MAGIC) {
        fprintf(stderr, "[BrowserShm] handshake: bad magic at 0x%08x "
                "(got 0x%08x, want 0x%08x) — guest may not have written "
                "the header yet, retrying\n",
                mac_addr, magic, BR_MAGIC);
        return nullptr;
    }
    if (version != BR_VERSION) {
        fprintf(stderr, "[BrowserShm] handshake: version mismatch "
                "(guest=%u, host=%u) — refusing to publish\n",
                version, BR_VERSION);
        return nullptr;
    }
    fprintf(stderr, "[BrowserShm] handshake complete: mac=0x%08x host=%p "
            "magic=OK version=%u sizeof=%zu\n",
            mac_addr, (void*)shm, version, sizeof(BrowserShm));
    return shm;
}

void run()
{
    std::string path = handshake_path();
    if (path.empty()) {
        fprintf(stderr, "[BrowserShm] bridge_dir not configured — handshake "
                "watcher disabled. Pass --bridge alongside --browser.\n");
        return;
    }
    fprintf(stderr, "[BrowserShm] watching %s for guest handshake\n",
            path.c_str());

    while (g_running.load(std::memory_order_acquire)) {
        std::string contents = slurp(path);
        if (!contents.empty()) {
            uint32_t mac_addr = parse_hex_addr(contents);
            BrowserShm* shm = try_resolve(mac_addr);
            if (shm) {
                g_shm.store(shm, std::memory_order_release);
                /* Stop polling once handshake completes. The guest's
                 * BrowserShm pointer is stable for the lifetime of
                 * Browser.app — if Browser.app quits we'll need a
                 * separate teardown signal (M2+). */
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

}  // namespace

void shm_init()
{
    if (g_running.exchange(true)) return;
    g_thread = std::thread(run);
}

void shm_stop()
{
    if (!g_running.exchange(false)) return;
    if (g_thread.joinable()) g_thread.join();
    g_shm.store(nullptr, std::memory_order_release);
}

BrowserShm* shm_get()
{
    return g_shm.load(std::memory_order_acquire);
}

/* ── Ring helpers ───────────────────────────────────────────────── */

namespace {
/* Multi-producer serialization for h2g (host worker threads converge
 * on a single writer). g2h has no equivalent — the host is the sole
 * consumer of g2h, no mutex needed. */
std::mutex g_h2g_mtx;
}

bool send_event(uint16_t type, const void* payload, uint16_t len)
{
    BrowserShm* shm = shm_get();
    if (!shm) return false;
    std::lock_guard<std::mutex> lk(g_h2g_mtx);
    return ring_push(&shm->h2g, type, payload, len);
}

bool read_command(uint16_t* type, void* buf, uint16_t buf_capacity,
                  uint16_t* out_len)
{
    BrowserShm* shm = shm_get();
    if (!shm) return false;
    return ring_pop(&shm->g2h, type, buf, buf_capacity, out_len);
}

}  // namespace browser
