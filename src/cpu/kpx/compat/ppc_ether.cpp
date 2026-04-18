/*
 *  ppc_ether.cpp - Slim PPC ethernet glue (USE_ETHER_FULL_DRIVER mode)
 *
 *  The Mac NDRV blob (EthernetDriverFull.i) handles all DLPI/STREAMS
 *  internally. We only need to:
 *   - Stash the NDRV's ether_dispatch_packet tvector at InitStreamModule.
 *   - Hand the Mac its MAC address on AO_get_ethernet_address.
 *   - Walk the mblk_t chain in Mac memory on AO_transmit_packet, build a
 *     flat ethernet frame, push it to the bridge via ether_socket_send_raw.
 *   - On EtherIRQ (called by the NDRV from the Mac side after our rx thread
 *     trips INTFLAG_ETHER), drain ether_socket's rx queue, copy each frame
 *     into pre-reserved Mac scratch memory, and CallMacOS2 into the NDRV's
 *     dispatch tvector.
 */

#include "sysdeps.h"
#include "ether.h"
#include "kpx_cpu_emulation.h"
#include "thunks.h"
#include "../../../drivers/ether/ether_socket.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <atomic>

// call_macos2 is declared in compat/sysdeps.h with C++ linkage.

// --- State ---

static std::atomic<uint32_t> s_dispatch_tvect{0};
static uint32 s_rx_buf_mac = 0;     // SheepMem-reserved RX staging buffer (Mac addr)
static const uint32 RX_BUF_SIZE = 1518;

// --- Init / Term ---

extern "C" int32 InitStreamModule(void *theID)
{
    uint32_t tvect = (uint32_t)(uintptr_t)theID;
    s_dispatch_tvect.store(tvect);

    if (s_rx_buf_mac == 0)
        s_rx_buf_mac = SheepMem::Reserve(RX_BUF_SIZE);

    fprintf(stderr, "[ppc-ether] InitStreamModule: dispatch tvect=%08x rx_buf=%08x\n",
            tvect, s_rx_buf_mac);
    return 1;  // non-zero = success
}

extern "C" void TerminateStreamModule(void)
{
    fprintf(stderr, "[ppc-ether] TerminateStreamModule\n");
    s_dispatch_tvect.store(0);
}

// --- AO_* (called via NativeOps from the NDRV) ---

extern "C" void AO_get_ethernet_address(uint32 addr)
{
    uint8_t mac[6];
    ether_socket_get_mac(mac);
    Host2Mac_memcpy(addr, mac, 6);
}

extern "C" void AO_enable_multicast(uint32 /*addr*/) { }
extern "C" void AO_disable_multicast(uint32 /*addr*/) { }

extern "C" void AO_transmit_packet(uint32 mp)
{
    // Walk mblk_t chain in Mac memory:
    //   mp + 8  = b_cont (next mblk in chain)
    //   mp + 12 = b_rptr (Mac addr of data start)
    //   mp + 16 = b_wptr (Mac addr of data end)
    // Source: legacy ether_msgb_to_buffer in include/ether.h.
    uint8_t buf[1518];
    uint32_t total = 0;
    uint32_t cur = mp;
    while (cur && total < sizeof(buf)) {
        uint32_t rptr = ReadMacInt32(cur + 12);
        uint32_t wptr = ReadMacInt32(cur + 16);
        uint32_t seg  = wptr - rptr;
        if (seg > sizeof(buf) - total) seg = sizeof(buf) - total;
        Mac2Host_memcpy(buf + total, rptr, seg);
        total += seg;
        cur = ReadMacInt32(cur + 8);
    }

    if (total < 14)
        return;

    ether_socket_send_raw(buf, total);
}

// --- RX dispatch (called from EtherIRQ via ether_socket_drain_rx) ---

static void deliver_one(const uint8_t *frame, uint32_t len)
{
    uint32_t tvect = s_dispatch_tvect.load();
    if (tvect == 0 || s_rx_buf_mac == 0)
        return;
    if (len > RX_BUF_SIZE)
        len = RX_BUF_SIZE;
    Host2Mac_memcpy(s_rx_buf_mac, frame, len);
    call_macos2(tvect, s_rx_buf_mac, len);
}

extern "C" void EtherIRQ(void)
{
    ether_socket_drain_rx(deliver_one);
}

// --- Stubs (dead in FULL_DRIVER mode; NDRV runs DLPI inside the guest) ---

extern "C" int32 ether_open(queue_t *, void *, uint32, uint32, void *) { return 0; }
extern "C" int32 ether_close(queue_t *, uint32, void *) { return 0; }
extern "C" int32 ether_wput(queue_t *, mblk_t *) { return 0; }
extern "C" int32 ether_rsrv(queue_t *) { return 0; }
