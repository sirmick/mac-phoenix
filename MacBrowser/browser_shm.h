/*
 *  browser_shm.h — guest-side SPSC ring helpers for BrowserShm.
 *
 *  Mirrors the host implementation (src/drivers/browser/ring.cpp) so
 *  the protocol is identical on both sides. Caller passes a BrRing
 *  pointer it already owns (typically &gShm->h2g for receiving events
 *  or &gShm->g2h for sending commands).
 *
 *  Single-producer / single-consumer per ring:
 *    h2g — guest is the consumer (br_h2g_pop)
 *    g2h — guest is the producer (br_g2h_push)
 *
 *  No fences needed: m68k has TSO-ish ordering, and there's no
 *  preemption between ring operations and producer-side index commits
 *  in the cooperative-multitasking guest. The host already issues
 *  __sync_synchronize on its side, which is the only barrier that
 *  matters.
 */
#ifndef BROWSER_SHM_H
#define BROWSER_SHM_H

#include "MacBrowser.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Push one command into a g2h-style ring. Returns 0 on success,
 * non-zero on full / oversized / reserved-type. */
int br_ring_push(BrRing *ring, uint16_t type,
                 const void *payload, uint16_t len);

/* Pop one event from an h2g-style ring. On success returns 0, fills
 * *out_type and *out_len, copies up to buf_capacity payload bytes
 * into buf. Returns 1 on empty. *out_len reports the true payload
 * length even if it exceeds buf_capacity. */
int br_ring_pop(BrRing *ring, uint16_t *out_type,
                void *buf, uint16_t buf_capacity, uint16_t *out_len);

/* Write one debug line into BrowserShm.log. Lossy: if the host hasn't
 * polled before the next call, the previous line is overwritten and
 * the host will report it as a drop. Truncates messages longer than
 * BR_LOG_BUFSZ. Cheap to call unconditionally; host filters by
 * BROWSER_LOG_LEVEL.
 *
 * `slot` is a pointer to BrowserShm.log (caller passes &gShm->log).
 * Passing the slot rather than reaching for a global keeps the helper
 * self-contained. */
void br_log(struct BrLogSlot *slot, uint8_t level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif  /* BROWSER_SHM_H */
