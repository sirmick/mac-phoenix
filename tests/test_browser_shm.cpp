/*
 * test_browser_shm — protocol unit test for the BrowserShm SPSC rings.
 *
 * Standalone: compiles ring.cpp directly, allocates BrowserShm on the
 * heap, and exercises ring_push/ring_pop in every direction. No
 * emulator, no Mac, no handshake. Locks the SPSC contract so the host
 * and guest implementations can't drift.
 *
 * Tests cover:
 *   - empty / not-empty
 *   - single push/pop round trip
 *   - varying payload sizes (0, 1, 4, 17, 1024)
 *   - fill until full, then drain
 *   - wraparound (push messages until write_idx wraps; verify pops
 *     correctly chase the WRAP sentinel)
 *   - interleaved push/pop (consumer drains while producer pushes)
 *   - bidirectional (h2g and g2h independent)
 *   - oversized payload rejected
 *   - reserved BR_MSG_WRAP type rejected as a real message
 */
#include "drivers/browser/ring.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int failures = 0;

#define CHECK(cond, what) do {                                  \
    if (!(cond)) {                                              \
        failures++;                                             \
        printf("  FAIL  %s   (%s:%d)\n", what, __FILE__, __LINE__); \
    } else {                                                    \
        printf("  ok    %s\n", what);                           \
    }                                                           \
} while (0)

static std::unique_ptr<BrowserShm> fresh_shm()
{
    auto shm = std::make_unique<BrowserShm>();
    memset(shm.get(), 0, sizeof(BrowserShm));
    /* Both rings start empty: write_idx == read_idx == 0. */
    return shm;
}

static void test_empty_ring()
{
    auto shm = fresh_shm();
    uint16_t type = 99, len = 99;
    uint8_t buf[16];
    bool ok = browser::ring_pop(&shm->h2g, &type, buf, sizeof(buf), &len);
    CHECK(!ok, "empty ring pop returns false");
}

static void test_round_trip_simple()
{
    auto shm = fresh_shm();
    const char payload[] = "hello";
    bool ok = browser::ring_push(&shm->h2g, BR_EV_STATUS,
                                 payload, (uint16_t)sizeof(payload));
    CHECK(ok, "push hello succeeds");

    uint16_t type = 0, len = 0;
    char out[32] = {};
    ok = browser::ring_pop(&shm->h2g, &type, out, sizeof(out), &len);
    CHECK(ok, "pop hello succeeds");
    CHECK(type == BR_EV_STATUS, "type round-trips");
    CHECK(len == sizeof(payload), "len round-trips");
    CHECK(memcmp(out, payload, sizeof(payload)) == 0, "payload bytes match");

    /* Ring should now be empty again. */
    ok = browser::ring_pop(&shm->h2g, &type, out, sizeof(out), &len);
    CHECK(!ok, "second pop on drained ring returns false");
}

static void test_varying_sizes()
{
    auto shm = fresh_shm();
    const uint16_t sizes[] = { 0, 1, 4, 17, 1024 };
    /* Push & pop each in isolation so we don't accidentally test
     * fill-to-full here. */
    for (uint16_t sz : sizes) {
        std::vector<uint8_t> in(sz);
        for (uint16_t i = 0; i < sz; i++) in[i] = (uint8_t)(i ^ 0x5A);

        bool ok = browser::ring_push(&shm->h2g, BR_EV_DOWNLOAD,
                                     in.data(), sz);
        CHECK(ok, "push varying-size succeeds");

        std::vector<uint8_t> out(sz + 64, 0xCC);
        uint16_t type = 0, len = 0;
        ok = browser::ring_pop(&shm->h2g, &type, out.data(),
                               (uint16_t)out.size(), &len);
        CHECK(ok, "pop varying-size succeeds");
        CHECK(type == BR_EV_DOWNLOAD, "varying-size type round-trips");
        CHECK(len == sz, "varying-size len round-trips");
        CHECK(memcmp(out.data(), in.data(), sz) == 0,
              "varying-size payload round-trips");
    }
}

static void test_fill_until_full()
{
    auto shm = fresh_shm();

    /* Each entry is 4 hdr + ALIGN(payload). For payload=12 → 16 bytes
     * total. With BR_RING_SIZE=65536 and our 4-byte free-margin, we
     * should fit (65536-4)/16 = 4095 entries, give or take. */
    const uint16_t payload_len = 12;
    uint8_t payload[12];
    for (int i = 0; i < 12; i++) payload[i] = (uint8_t)i;

    int pushed = 0;
    while (browser::ring_push(&shm->h2g, BR_EV_FRAME,
                              payload, payload_len)) {
        pushed++;
        if (pushed > 100000) {
            CHECK(false, "ring failed to report full (runaway loop)");
            return;
        }
    }
    CHECK(pushed > 4000, "filled ring with thousands of entries");

    /* Drain everything and verify count + payload. */
    int popped = 0;
    uint16_t type = 0, len = 0;
    uint8_t out[16];
    while (browser::ring_pop(&shm->h2g, &type, out, sizeof(out), &len)) {
        if (type != BR_EV_FRAME || len != payload_len ||
            memcmp(out, payload, payload_len) != 0) {
            CHECK(false, "fill-until-full payload corrupted");
            return;
        }
        popped++;
    }
    CHECK(popped == pushed, "popped count == pushed count");
}

static void test_wraparound()
{
    auto shm = fresh_shm();

    /* Strategy: push N small messages and pop them all. Repeat several
     * times so the producer's write_idx wraps past BR_RING_SIZE and
     * the WRAP sentinel exercises. After each round, write_idx and
     * read_idx should advance together; eventually they pass 65536. */
    const uint16_t payload_len = 100;  /* 4+ALIGN(100) = 104 bytes/msg */
    std::vector<uint8_t> payload(payload_len);
    for (uint16_t i = 0; i < payload_len; i++) {
        payload[i] = (uint8_t)(i ^ 0xA5);
    }

    /* 65536 / 104 ≈ 630 → run 5000 round trips to wrap ~8 times. */
    int round_trips = 5000;
    for (int n = 0; n < round_trips; n++) {
        bool ok = browser::ring_push(&shm->h2g, BR_EV_STATUS,
                                     payload.data(), payload_len);
        if (!ok) {
            CHECK(false, "push failed mid-wraparound");
            return;
        }
        uint16_t type = 0, len = 0;
        std::vector<uint8_t> out(payload_len + 16, 0xDE);
        ok = browser::ring_pop(&shm->h2g, &type, out.data(),
                               (uint16_t)out.size(), &len);
        if (!ok || len != payload_len ||
            memcmp(out.data(), payload.data(), payload_len) != 0) {
            CHECK(false, "pop failed mid-wraparound");
            return;
        }
    }
    CHECK(true, "5000 round-trips through wraparound");
}

static void test_interleaved()
{
    auto shm = fresh_shm();

    /* Push 5, pop 3, push 5, pop 7, ... so the producer is sometimes
     * way ahead of the consumer. Verifies wraparound mid-batch. */
    const uint16_t payload_len = 50;
    std::vector<uint8_t> payload(payload_len);
    for (uint16_t i = 0; i < payload_len; i++) payload[i] = (uint8_t)i;

    int pushed = 0, popped = 0;
    int batches = 1000;
    for (int b = 0; b < batches; b++) {
        int push_n = 5;
        int pop_n  = (b % 7 == 0) ? 7 : 3;

        for (int i = 0; i < push_n; i++) {
            bool ok = browser::ring_push(&shm->h2g, BR_EV_PAGE,
                                         payload.data(), payload_len);
            if (ok) pushed++;
            else break;
        }
        for (int i = 0; i < pop_n; i++) {
            uint16_t type = 0, len = 0;
            std::vector<uint8_t> out(payload_len);
            bool ok = browser::ring_pop(&shm->h2g, &type, out.data(),
                                        (uint16_t)out.size(), &len);
            if (!ok) break;
            if (type != BR_EV_PAGE || len != payload_len ||
                memcmp(out.data(), payload.data(), payload_len) != 0) {
                CHECK(false, "interleaved payload corrupted");
                return;
            }
            popped++;
        }
    }
    /* Drain whatever is left. */
    while (true) {
        uint16_t type = 0, len = 0;
        std::vector<uint8_t> out(payload_len);
        bool ok = browser::ring_pop(&shm->h2g, &type, out.data(),
                                    (uint16_t)out.size(), &len);
        if (!ok) break;
        popped++;
    }
    CHECK(pushed == popped, "interleaved push/pop conserve count");
}

static void test_bidirectional_independence()
{
    auto shm = fresh_shm();

    /* h2g and g2h are independent rings — work in one shouldn't leak
     * into the other. */
    const char h_payload[] = "host event";
    const char g_payload[] = "guest cmd";
    bool ok;

    ok = browser::ring_push(&shm->h2g, BR_EV_STATUS,
                            h_payload, sizeof(h_payload));
    CHECK(ok, "push to h2g");
    ok = browser::ring_push(&shm->g2h, BR_CMD_BACK,
                            g_payload, sizeof(g_payload));
    CHECK(ok, "push to g2h");

    /* Pop in opposite order — g2h first, then h2g. Each ring delivers
     * its own message intact. */
    char out[32] = {};
    uint16_t type = 0, len = 0;
    ok = browser::ring_pop(&shm->g2h, &type, out, sizeof(out), &len);
    CHECK(ok && type == BR_CMD_BACK && len == sizeof(g_payload) &&
          memcmp(out, g_payload, sizeof(g_payload)) == 0,
          "g2h pop returns guest payload");

    memset(out, 0, sizeof(out));
    ok = browser::ring_pop(&shm->h2g, &type, out, sizeof(out), &len);
    CHECK(ok && type == BR_EV_STATUS && len == sizeof(h_payload) &&
          memcmp(out, h_payload, sizeof(h_payload)) == 0,
          "h2g pop returns host payload");
}

static void test_oversized_rejected()
{
    auto shm = fresh_shm();
    /* len = MAX_PAYLOAD + 1 → ring_push must refuse. */
    std::vector<uint8_t> too_big(BR_MSG_MAX_PAYLOAD + 1, 0);
    /* but len is uint16_t, so cap at 0xFFFF; only effective if
     * BR_MSG_MAX_PAYLOAD < 0xFFFF, which it is (~65531). */
    uint16_t bogus_len = (uint16_t)(BR_MSG_MAX_PAYLOAD + 1);
    bool ok = browser::ring_push(&shm->h2g, BR_EV_FRAME,
                                 too_big.data(), bogus_len);
    CHECK(!ok, "oversized payload rejected");
}

static void test_wrap_type_rejected()
{
    auto shm = fresh_shm();
    bool ok = browser::ring_push(&shm->h2g, BR_MSG_WRAP, nullptr, 0);
    CHECK(!ok, "BR_MSG_WRAP type rejected as real message");
}

static void test_truncation_reports_true_len()
{
    auto shm = fresh_shm();
    uint8_t payload[200];
    for (int i = 0; i < 200; i++) payload[i] = (uint8_t)i;
    bool ok = browser::ring_push(&shm->h2g, BR_EV_PAGE, payload, 200);
    CHECK(ok, "push 200B for truncation test");

    /* Pop with only 50B buffer. *out_len should still report 200. */
    uint8_t small_buf[50];
    uint16_t type = 0, len = 0;
    ok = browser::ring_pop(&shm->h2g, &type, small_buf, sizeof(small_buf), &len);
    CHECK(ok, "pop with too-small buffer succeeds");
    CHECK(len == 200, "out_len reports true payload length on truncation");
    /* First 50 bytes should match. */
    bool match = true;
    for (int i = 0; i < 50; i++) {
        if (small_buf[i] != payload[i]) { match = false; break; }
    }
    CHECK(match, "truncated copy preserves prefix bytes");
}

int main()
{
    printf("BrowserShm SPSC ring protocol\n");
    test_empty_ring();
    test_round_trip_simple();
    test_varying_sizes();
    test_fill_until_full();
    test_wraparound();
    test_interleaved();
    test_bidirectional_independence();
    test_oversized_rejected();
    test_wrap_type_rejected();
    test_truncation_reports_true_len();

    if (failures > 0) {
        printf("\n%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall tests passed\n");
    return 0;
}
