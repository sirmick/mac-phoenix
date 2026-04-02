/*
 *  ether.h - KPX compatibility stub for ethernet NativeOp calls
 */

#ifndef KPX_ETHER_H
#define KPX_ETHER_H

#include "sysdeps.h"

// Types used by ethernet NativeOp (opaque pointers in KPX context)
typedef void queue_t;
typedef void mblk_t;

// Stubs for ethernet NativeOp handlers
static inline void AO_get_ethernet_address(uint32 a) { }
static inline void AO_enable_multicast(uint32 a) { }
static inline void AO_disable_multicast(uint32 a) { }
static inline void AO_transmit_packet(uint32 a) { }
static inline void EtherIRQ(void) { }
static inline int32 InitStreamModule(void *a) { return 0; }
static inline void TerminateStreamModule(void) { }
static inline int32 ether_open(queue_t *a, void *b, uint32 c, uint32 d, void *e) { return 0; }
static inline int32 ether_close(queue_t *a, uint32 b, void *c) { return 0; }
static inline int32 ether_wput(queue_t *a, mblk_t *b) { return 0; }
static inline int32 ether_rsrv(queue_t *a) { return 0; }

#endif
