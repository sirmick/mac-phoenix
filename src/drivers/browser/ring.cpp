/*
 *  ring.cpp — SPSC ring helpers. See ring.h.
 */
#include "ring.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_set>

namespace browser {
namespace {

/* Host-side "this ring is dead" cache. We can't add fields to BrRing
 * (it's a fixed cross-process layout), so the poison bit lives here.
 * Guest restart allocates a fresh BrRing in new shm — its pointer
 * won't be in this set, so the new ring starts clean. */
std::mutex g_poison_mtx;
std::unordered_set<BrRing*> g_poisoned;

bool ring_is_poisoned(BrRing* r) {
    std::lock_guard<std::mutex> lk(g_poison_mtx);
    return g_poisoned.count(r) != 0;
}

void ring_poison(BrRing* r) {
    std::lock_guard<std::mutex> lk(g_poison_mtx);
    g_poisoned.insert(r);
}

/* Bytes available for new producer writes, accounting for the 4-byte
 * "reserved gap" we keep between write_idx and read_idx. */
uint32_t ring_free_bytes(uint32_t write_idx, uint32_t read_idx)
{
    /* (read - write - 4) mod RING_SIZE — the -4 keeps empty/full
     * distinguishable AND guarantees room for a WRAP header. */
    return (read_idx + BR_RING_SIZE - write_idx - 4u) % BR_RING_SIZE;
}

void ring_write_hdr(BrRing* ring, uint32_t off, uint16_t type, uint16_t len)
{
    br_u16_store((uint16_t*)&ring->data[off + 0], type);
    br_u16_store((uint16_t*)&ring->data[off + 2], len);
}

void ring_read_hdr(const BrRing* ring, uint32_t off,
                   uint16_t* type, uint16_t* len)
{
    *type = br_u16_load((const uint16_t*)&ring->data[off + 0]);
    *len  = br_u16_load((const uint16_t*)&ring->data[off + 2]);
}

}  // namespace

bool ring_push(BrRing* ring, uint16_t type, const void* payload, uint16_t len)
{
    if (type == BR_MSG_WRAP) return false;             /* reserved */
    if (len > BR_MSG_MAX_PAYLOAD) return false;

    uint32_t write_idx = br_u32_load(&ring->write_idx);
    uint32_t read_idx  = br_u32_load(&ring->read_idx);

    uint32_t aligned_len = (uint32_t)BR_MSG_ALIGN(len);
    uint32_t needed      = BR_MSG_HDR_BYTES + aligned_len;

    bool wrap = (write_idx + needed > BR_RING_SIZE);
    /* When wrapping, the WRAP sentinel + any unused tail bytes between
     * write_idx and the end of data[] are also "consumed" — the
     * consumer skips over them on its way to offset 0. So the total
     * ring real estate the push needs is `(SIZE - write_idx) + needed`
     * in the wrap case, not just `4 + needed`. Getting this wrong
     * lets us land write_idx exactly on read_idx, which is the EMPTY
     * marker — and the just-pushed payload becomes invisible. */
    uint32_t total_needed = wrap ? ((BR_RING_SIZE - write_idx) + needed)
                                 : needed;
    if (total_needed > ring_free_bytes(write_idx, read_idx)) {
        return false;  /* ring full */
    }

    if (wrap) {
        ring_write_hdr(ring, write_idx, BR_MSG_WRAP, 0);
        write_idx = 0;
    }

    ring_write_hdr(ring, write_idx, type, len);
    if (len > 0 && payload) {
        memcpy(&ring->data[write_idx + BR_MSG_HDR_BYTES], payload, len);
    }
    write_idx += needed;
    if (write_idx == BR_RING_SIZE) write_idx = 0;

    BR_FENCE_RELEASE();
    br_u32_store(&ring->write_idx, write_idx);
    return true;
}

bool ring_pop(BrRing* ring, uint16_t* out_type, void* buf,
              uint16_t buf_capacity, uint16_t* out_len)
{
    /* Once we've seen a corrupt header on this ring, stop draining.
     * Either the guest crashed mid-write or write_idx itself got
     * stomped — continuing to "dispatch" garbage floods the caller
     * with [Cmd] unknown spam. */
    if (ring_is_poisoned(ring)) return false;

    uint32_t write_idx = br_u32_load(&ring->write_idx);
    BR_FENCE_ACQUIRE();
    uint32_t read_idx  = br_u32_load(&ring->read_idx);

    if (read_idx == write_idx) return false;  /* empty */

    /* Bounds check the indices themselves. A wild write_idx would
     * otherwise let us read past data[] into the next struct field. */
    if (write_idx >= BR_RING_SIZE || read_idx >= BR_RING_SIZE ||
        (read_idx & 3u) || (write_idx & 3u)) {
        fprintf(stderr, "[Ring] poisoned: read=%u write=%u (size=%u)\n",
                read_idx, write_idx, BR_RING_SIZE);
        ring_poison(ring);
        return false;
    }

    uint16_t type, len;
    ring_read_hdr(ring, read_idx, &type, &len);
    if (type == BR_MSG_WRAP) {
        read_idx = 0;
        if (read_idx == write_idx) {
            BR_FENCE_RELEASE();
            br_u32_store(&ring->read_idx, read_idx);
            return false;
        }
        ring_read_hdr(ring, read_idx, &type, &len);
        if (type == BR_MSG_WRAP) return false;  /* defensive */
    }

    /* Validate the header before trusting it. A length > the ring's
     * payload capacity, or the residual gap between read_idx and the
     * end-of-data, can only be garbage — the producer would have
     * emitted a WRAP first. Poison rather than dispatch. */
    uint32_t aligned_len = (uint32_t)BR_MSG_ALIGN(len);
    if (len > BR_MSG_MAX_PAYLOAD ||
        read_idx + BR_MSG_HDR_BYTES + aligned_len > BR_RING_SIZE) {
        fprintf(stderr, "[Ring] poisoned: bogus header type=0x%x "
                "len=%u at read=%u\n", type, len, read_idx);
        ring_poison(ring);
        return false;
    }

    *out_type = type;
    *out_len  = len;
    uint16_t to_copy = (len < buf_capacity) ? len : buf_capacity;
    if (to_copy > 0 && buf) {
        memcpy(buf, &ring->data[read_idx + BR_MSG_HDR_BYTES], to_copy);
    }

    read_idx += BR_MSG_HDR_BYTES + aligned_len;
    if (read_idx == BR_RING_SIZE) read_idx = 0;

    BR_FENCE_RELEASE();
    br_u32_store(&ring->read_idx, read_idx);
    return true;
}

}  // namespace browser
