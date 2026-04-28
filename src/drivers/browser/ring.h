/*
 *  ring.h — internal SPSC ring helpers for the BrowserShm rings.
 *
 *  Self-contained: depends only on <cstdint> and MacBrowser.h. Used by
 *  the host shm module (drivers/browser/shm.cpp) and exercised by the
 *  protocol unit test (tests/test_browser_shm.cpp). The guest has its
 *  own mirror in tests/guest/browser/browser_shm.c.
 *
 *  The functions implement the producer/consumer protocol described in
 *  MacBrowser.h:
 *
 *    Producer:  read read_idx, check space, [emit WRAP if needed],
 *               write payload, release fence, store write_idx.
 *    Consumer:  load write_idx (acquire fence), copy payload to
 *               caller buf, release fence, store read_idx.
 *
 *  Returns false on full (push) or empty (pop). Caller decides
 *  backpressure policy.
 */
#ifndef DRIVERS_BROWSER_RING_H
#define DRIVERS_BROWSER_RING_H

#include <cstdint>

struct BrRing;

namespace browser {

bool ring_push(BrRing* ring, uint16_t type,
               const void* payload, uint16_t len);

bool ring_pop(BrRing* ring, uint16_t* out_type,
              void* buf, uint16_t buf_capacity, uint16_t* out_len);

}  // namespace browser

#endif  // DRIVERS_BROWSER_RING_H
