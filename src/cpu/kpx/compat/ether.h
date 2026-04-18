/*
 *  ether.h - PPC ethernet C ABI for KPX NativeOp dispatch
 *
 *  Implementations live in compat/ppc_ether.cpp. The Mac NDRV blob
 *  (EthernetDriverFull.i, registered in name_registry_ppc.cpp) handles all
 *  DLPI/STREAMS internally, so the host only handles MAC delivery, TX/RX
 *  bridging, and a small init/term handshake.
 */

#ifndef KPX_ETHER_H
#define KPX_ETHER_H

#include "sysdeps.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types for ether_open/close/wput/rsrv (dead in FULL_DRIVER mode but
// the NativeOp dispatcher still references them).
typedef void queue_t;
typedef void mblk_t;

// AO_* — called by the NDRV via NativeOps. Argument is a Mac-side address.
void  AO_get_ethernet_address(uint32 addr);
void  AO_enable_multicast(uint32 addr);
void  AO_disable_multicast(uint32 addr);
void  AO_transmit_packet(uint32 mp);

// Ethernet interrupt handler (NativeOp).
void  EtherIRQ(void);

// Init/term handshake. theID is the NDRV's ether_dispatch_packet tvector.
int32 InitStreamModule(void *theID);
void  TerminateStreamModule(void);

// Stubs in FULL_DRIVER mode; NDRV runs DLPI inside the guest.
int32 ether_open(queue_t *rdq, void *dev, uint32 flag, uint32 sflag, void *creds);
int32 ether_close(queue_t *rdq, uint32 flag, void *creds);
int32 ether_wput(queue_t *q, mblk_t *mp);
int32 ether_rsrv(queue_t *q);

#ifdef __cplusplus
}
#endif

#endif
