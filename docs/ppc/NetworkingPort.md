# PPC Networking Port Plan

Get PPC guests on the network. The transport (Unix socket → net-bridge) is
already built in `src/drivers/ether/ether_socket.cpp` and used by 68k. PPC has
only no-op stubs in `src/cpu/kpx/compat/ether.h`, so every Mac-side ether call
on PPC is a no-op.

## Background

PPC Mac OS networks via NDRV + STREAMS/DLPI, not the 68k Device Manager.
`src/cpu/kpx/name_registry_ppc.cpp` already registers SheepShaver's
`EthernetDriverFull.i` blob under the device tree, and
`src/cpu/kpx/cpu_ppc_kpx.cpp:580-608` dispatches `NATIVE_ETHER_*` and
`NATIVE_AO_*` NativeOps to the C entry points that today are stubs.

## Big realization: FULL_DRIVER mode does most of the work in the Mac NDRV

SheepShaver compiles `ether.cpp` with `USE_ETHER_FULL_DRIVER=1` (set in
`Unix/sysdeps.h:102` unconditionally). In this mode the **Mac NDRV blob**
contains its own copy of `ether.cpp` compiled for PPC and runs the full DLPI
state machine **inside the guest**. The host only handles:

- `InitStreamModule(theID)` — store `theID` as the NDRV's `ether_dispatch_packet`
  tvector for later RX callback.
- `TerminateStreamModule()` — clear flag.
- `AO_get_ethernet_address(addr)` — write our 6-byte MAC into Mac memory.
- `AO_enable_multicast / AO_disable_multicast` — no-op for now.
- `AO_transmit_packet(mp)` — walk the mblk chain in Mac memory
  (`ether_msgb_to_buffer`), build a flat ethernet frame, hand to net-bridge.
- `EtherIRQ()` — interrupt counter.
- `ether_dispatch_packet(p, size)` — `CallMacOS2` into the stored NDRV tvector;
  invoked from our socket RX thread.

`ether_open / close / wput / rsrv` cases on the NativeOp dispatcher are **dead
in FULL_DRIVER mode** — the NDRV blob's own `Ethernet.cpp` only routes them to
NativeOps when `BUILD_ETHER_FULL_DRIVER` is *not* defined (see
`legacy/SheepShaver/src/EthernetDriver/Ethernet.cpp:263-283`). They can stay
as stubs.

This collapses the port from ~2300 lines down to ~150 lines.

## What gets ported, what doesn't

- **Skip**: the entire `legacy/SheepShaver/src/ether.cpp` DLPI state machine
  (1748 lines). Not needed in FULL mode.
- **Skip**: `Unix/ether_unix.cpp` host transport. We already have
  `src/drivers/ether/ether_socket.cpp`.
- **Reference**: `legacy/SheepShaver/src/ether.cpp` (mblk layout,
  `ether_msgb_to_buffer`) and `legacy/SheepShaver/src/Unix/ether_unix.cpp`
  (FULL-mode init/dispatch).
- **Write fresh**: `src/cpu/kpx/compat/ether.h` (public C ABI) and
  `src/cpu/kpx/compat/ppc_ether.cpp` (~150-line implementation).

## Public C ABI (`compat/ppc_ether.h`)

Drop-in replacement for `compat/ether.h`. C-linkage so the NativeOp dispatcher
in `cpu_ppc_kpx.cpp` keeps working:

```c
void  AO_get_ethernet_address(uint32 addr);
void  AO_enable_multicast(uint32 addr);
void  AO_disable_multicast(uint32 addr);
void  AO_transmit_packet(uint32 mp);
void  EtherIRQ(void);
int32 InitStreamModule(void *theID);
void  TerminateStreamModule(void);
int32 ether_open(queue_t *, void *, uint32, uint32, void *);   // stub
int32 ether_close(queue_t *, uint32, void *);                  // stub
int32 ether_wput(queue_t *, mblk_t *);                         // stub
int32 ether_rsrv(queue_t *);                                   // stub
```

Plus internal entry points:

- `ppc_ether_set_mac(const uint8_t mac[6])` — called by `ether_socket` on init.
- `ppc_ether_dispatch_frame(const uint8_t *data, uint32 len)` — called by
  `ether_socket` rx thread when on PPC.

## Transport boundary (already wired, mostly)

- **TX**: NDRV → `AO_transmit_packet(mp)` → walk Mac mblk chain → flat buffer
  → call `ether_socket_send_raw(buf, len)` (new entry point) → Unix socket.
- **RX**: net-bridge frame → `ether_socket` rx thread → if PPC,
  `ppc_ether_dispatch_frame(buf, len)` → copy to Mac scratch → `CallMacOS2` into
  stored NDRV tvect.
- **MAC**: `AO_get_ethernet_address` reads our `s_mac_addr` (set on init).

## Phased plan

1. **Public ABI** — write `src/cpu/kpx/compat/ppc_ether.h` (C-linkage).
2. **Implementation** — write `src/cpu/kpx/compat/ppc_ether.cpp` (~150 lines).
   Stores NDRV dispatch tvect, walks mblk for TX, CallMacOS2 for RX dispatch.
3. **Transport bridge** — extend `ether_socket.cpp` with:
   - `ether_socket_send_raw(buf, len)` for PPC TX.
   - A delivery callback (function pointer) so the rx thread routes frames to
     `ppc_ether_dispatch_frame` on PPC and `ether_udp_read` on 68k.
   - A way for `ppc_ether.cpp` to learn the MAC at init time.
4. **NativeOp dispatcher** — `src/cpu/kpx/cpu_ppc_kpx.cpp:577-608` now finds the
   real `AO_*` / `Init/TerminateStreamModule` symbols; `ether_open/close/
   wput/rsrv` keep their stubs (dead in FULL mode).
5. **Replace stub header** — swap `compat/ether.h` include for `compat/ppc_ether.h`
   in `cpu_ppc_kpx.cpp`. Delete `compat/ether.h`.
6. **CMake** — add `compat/ppc_ether.cpp` to KPX sources.
7. **Test** — `tests/test_guest_suite.sh --arch ppc`. Target: 18 passing on PPC,
   matching 68k.

## Risks

- Whether the NDRV's `ether_dispatch_packet` tvect actually reaches us via
  `InitStreamModule(theID)` — easy to verify with a log line on first call.
- mblk chain layout in PPC Mac memory: `mp + 8 = b_cont`, `mp + 12 = b_rptr`,
  `mp + 16 = b_wptr`. Verified from legacy `ether_msgb_to_buffer` (in `ether.h`).
- The 68k path through `ether_socket` must keep working — we add new entry
  points, do not modify existing ones.

## Status: done (Apr 2026)

18/18 guest network tests pass on PPC (DNS, UDP echo, TCP echo) with a
macOS 7.6.1 disk and the Quadra G3 ROM. 68k suite unchanged at 18/18.

One surprise during bring-up: `INTFLAG_ETHER` had drifted between
`src/common/include/main.h` (value `8`, used by `ether_socket.cpp`) and
`src/cpu/kpx/compat/main.h` (value `4`, used by `emul_op_ppc.cpp` OP_IRQ).
The RX thread set bit 8 and the PPC handler checked bit 4, so the IRQ fired
but the ether branch never ran. Fix: align both headers. See
`docs/PPCBoot.md` / memory note for the general parity rule.

## Estimate

A few hours, not days. The doc's earlier 2–3 day estimate assumed we'd port the
full DLPI state machine — FULL_DRIVER mode obviates that.
