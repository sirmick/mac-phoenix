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

/* Tracks the last log.seq we saw, so poll_log can detect new entries
 * + count drops. Reset to 0 in run() on every re-handshake — the
 * fresh BrowserShm starts seq from 0, so stale state would underflow
 * into "dropped 4 billion log lines". */
uint32_t g_last_log_seq = 0;

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

    /* Keep watching for the lifetime of the host: when MacBrowser quits
     * and is re-launched, the new instance allocates a fresh BrowserShm
     * and re-publishes here. We re-handshake on any change to the
     * handshake file — either the published address differs or the
     * mtime advances (same-address heap reuse stamps a fresh struct
     * whose ring/log/seq counters reset to zero, so the host's cached
     * seq state goes invalid even though the pointer hasn't moved). */
    uint32_t prev_addr  = 0;
    time_t   last_mtime = 0;
    while (g_running.load(std::memory_order_acquire)) {
        struct stat st;
        bool changed = (stat(path.c_str(), &st) == 0)
                     ? (st.st_mtime != last_mtime)
                     : false;
        if (changed) {
            std::string contents = slurp(path);
            uint32_t mac_addr = parse_hex_addr(contents);
            if (mac_addr != 0) {
                BrowserShm* shm = try_resolve(mac_addr);
                if (shm) {
                    BrowserShm* old = g_shm.exchange(shm,
                        std::memory_order_acq_rel);
                    last_mtime = st.st_mtime;
                    /* Reset host-side seq trackers — the new BrowserShm
                     * starts log/frame seq at 0, so a stale cache would
                     * underflow into "dropped 4 billion log lines" or
                     * cause poll_log to spam every event again. The
                     * pipeline already handles fb.seq via its own
                     * last_shm check; only the log seq lives here. */
                    g_last_log_seq = 0;
                    if (old) {
                        fprintf(stderr, "[BrowserShm] re-handshake: "
                                "%p (mac 0x%08x) → %p (mac 0x%08x)\n",
                                (void*)old, prev_addr,
                                (void*)shm, mac_addr);
                    }
                    prev_addr = mac_addr;
                }
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

namespace {

const char* level_str(uint8_t level)
{
    switch (level) {
    case BR_LOG_DBG: return "dbg";
    case BR_LOG_INF: return "inf";
    case BR_LOG_WRN: return "wrn";
    case BR_LOG_ERR: return "err";
    default:         return "?";
    }
}

uint8_t parse_log_level(const char* s)
{
    if (!s) return BR_LOG_INF;
    if (s[0] == 'd' || s[0] == 'D') return BR_LOG_DBG;
    if (s[0] == 'i' || s[0] == 'I') return BR_LOG_INF;
    if (s[0] == 'w' || s[0] == 'W') return BR_LOG_WRN;
    if (s[0] == 'e' || s[0] == 'E') return BR_LOG_ERR;
    return BR_LOG_INF;
}

uint8_t threshold()
{
    /* Cache once: getenv across each tick is wasteful. */
    static uint8_t cached = parse_log_level(getenv("BROWSER_LOG_LEVEL"));
    return cached;
}

}  // namespace

void poll_log()
{
    BrowserShm* shm = shm_get();
    if (!shm) return;

    uint32_t seq = br_u32_load(&shm->log.seq);
    if (seq == g_last_log_seq) return;

    BR_FENCE_ACQUIRE();
    uint32_t dropped = (seq - g_last_log_seq) - 1;
    if (dropped > 0) {
        fprintf(stderr, "[BrowserGuest] (dropped %u log lines)\n", dropped);
    }
    uint8_t  level = shm->log.level;
    if (level >= threshold()) {
        uint16_t len = br_u16_load(&shm->log.len);
        if (len > BR_LOG_BUFSZ) len = BR_LOG_BUFSZ;
        fprintf(stderr, "[BrowserGuest %s] %.*s\n",
                level_str(level), (int)len, shm->log.buf);
    }
    g_last_log_seq = seq;
}

}  // namespace browser
