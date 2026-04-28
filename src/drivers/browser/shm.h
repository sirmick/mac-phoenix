/*
 *  shm.h — host side of the BrowserShm handshake.
 *
 *  Browser.app allocates BrowserShm out of its own application heap
 *  (NewPtrClear) and writes the buffer's Mac address as ASCII hex into
 *  Host:MacPhoenix:browser_shm.txt on the ExtFS share. This module
 *  watches that file, parses the address, validates magic + version,
 *  translates Mac → host via Mac2HostAddr(), and exposes the resulting
 *  host pointer to other host modules (the gradient writer for M1, the
 *  full BrowserModule for M3+).
 *
 *  Lifecycle: shm_init() launches a background poller thread, shm_stop()
 *  joins it, shm_get() returns the host pointer once published (or null
 *  before the handshake completes). Idempotent.
 */
#ifndef DRIVERS_BROWSER_SHM_H
#define DRIVERS_BROWSER_SHM_H

#include <cstdint>

struct BrowserShm;

namespace browser {

void shm_init();
void shm_stop();

/* Host pointer to the guest-allocated BrowserShm, or nullptr until the
 * handshake completes. Reads use std::memory_order_acquire. */
BrowserShm* shm_get();

/* ── Ring helpers ─────────────────────────────────────────────────
 *
 * Push one event into the h2g ring (host → guest). Multiple host
 * threads serialize internally via a mutex; the guest consumer is
 * single-threaded (its VBL handler / main loop). Returns false if
 * the ring is full or the handshake hasn't completed; the caller is
 * responsible for the application-level policy on backpressure
 * (drop, coalesce, retry next tick).
 *
 *   type     one of BR_EV_* (must be != BR_MSG_WRAP)
 *   payload  may be nullptr if len == 0
 *   len      payload bytes; 0 ≤ len ≤ BR_MSG_MAX_PAYLOAD
 */
bool send_event(uint16_t type, const void* payload, uint16_t len);

/* Pop one command from the g2h ring (guest → host). On success,
 * fills *type and *out_len, copies the payload (up to buf_capacity
 * bytes) into buf. *out_len reflects the true payload length even
 * if larger than buf_capacity (then bytes past buf_capacity are
 * lost — caller should provide a sufficient buffer). Returns false
 * if the ring is empty or handshake hasn't completed.
 */
bool read_command(uint16_t* type, void* buf, uint16_t buf_capacity,
                  uint16_t* out_len);

/* Poll the BrLogSlot in BrowserShm. If the guest published a new line
 * since the last call, prints it to stderr with a [BrowserGuest level]
 * tag. Cheap to call every tick; emits "(dropped N lines)" if seq
 * jumped by more than 1 between calls. Filters by the BROWSER_LOG_LEVEL
 * env var ("dbg" / "inf" / "wrn" / "err"; default = "inf"). */
void poll_log();

}  // namespace browser

#endif  // DRIVERS_BROWSER_SHM_H
