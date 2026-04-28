/*
 *  browser_shm.c — guest-side SPSC ring helpers. See browser_shm.h.
 *
 *  Native big-endian m68k. The br_*_load/store macros in MacBrowser.h
 *  are no-ops on the guest, so we just dereference directly through
 *  them. Layout, sizing, and wrap discipline match host ring.cpp byte
 *  for byte — see tests/test_browser_shm.cpp for the protocol unit
 *  test that locks both sides.
 */
#include "browser_shm.h"
#include <string.h>

static uint32_t ring_free_bytes(uint32_t write_idx, uint32_t read_idx)
{
    /* (read - write - 4) mod RING_SIZE — the -4 keeps empty/full
     * distinguishable AND guarantees room for a WRAP header. */
    return (read_idx + BR_RING_SIZE - write_idx - 4u) % BR_RING_SIZE;
}

static void ring_write_hdr(BrRing *ring, uint32_t off,
                           uint16_t type, uint16_t len)
{
    br_u16_store((uint16_t *)&ring->data[off + 0], type);
    br_u16_store((uint16_t *)&ring->data[off + 2], len);
}

static void ring_read_hdr(const BrRing *ring, uint32_t off,
                          uint16_t *type, uint16_t *len)
{
    *type = br_u16_load((const uint16_t *)&ring->data[off + 0]);
    *len  = br_u16_load((const uint16_t *)&ring->data[off + 2]);
}

int br_ring_push(BrRing *ring, uint16_t type,
                 const void *payload, uint16_t len)
{
    if (type == BR_MSG_WRAP) return -1;             /* reserved */
    if (len > BR_MSG_MAX_PAYLOAD) return -2;

    uint32_t write_idx = br_u32_load(&ring->write_idx);
    uint32_t read_idx  = br_u32_load(&ring->read_idx);

    uint32_t aligned_len = (uint32_t)BR_MSG_ALIGN(len);
    uint32_t needed      = BR_MSG_HDR_BYTES + aligned_len;

    int wrap = (write_idx + needed > BR_RING_SIZE);
    /* When wrapping, the WRAP sentinel + any unused tail bytes count
     * as ring real estate consumed — see ring.cpp for the bug story. */
    uint32_t total_needed = wrap ? ((BR_RING_SIZE - write_idx) + needed)
                                 : needed;
    if (total_needed > ring_free_bytes(write_idx, read_idx)) {
        return 1;  /* ring full */
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
    return 0;
}

int br_ring_pop(BrRing *ring, uint16_t *out_type,
                void *buf, uint16_t buf_capacity, uint16_t *out_len)
{
    uint32_t write_idx = br_u32_load(&ring->write_idx);
    BR_FENCE_ACQUIRE();
    uint32_t read_idx  = br_u32_load(&ring->read_idx);

    if (read_idx == write_idx) return 1;  /* empty */

    uint16_t type, len;
    ring_read_hdr(ring, read_idx, &type, &len);
    if (type == BR_MSG_WRAP) {
        read_idx = 0;
        if (read_idx == write_idx) {
            BR_FENCE_RELEASE();
            br_u32_store(&ring->read_idx, read_idx);
            return 1;
        }
        ring_read_hdr(ring, read_idx, &type, &len);
        if (type == BR_MSG_WRAP) return 1;  /* defensive */
    }

    *out_type = type;
    *out_len  = len;
    uint16_t to_copy = (len < buf_capacity) ? len : buf_capacity;
    if (to_copy > 0 && buf) {
        memcpy(buf, &ring->data[read_idx + BR_MSG_HDR_BYTES], to_copy);
    }

    uint32_t aligned_len = (uint32_t)BR_MSG_ALIGN(len);
    read_idx += BR_MSG_HDR_BYTES + aligned_len;
    if (read_idx == BR_RING_SIZE) read_idx = 0;

    BR_FENCE_RELEASE();
    br_u32_store(&ring->read_idx, read_idx);
    return 0;
}
