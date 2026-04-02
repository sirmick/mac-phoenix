/*
 *  serial.h - KPX serial driver declarations (namespace ppc)
 *
 *  Real implementation in serial_ppc.cpp (ported from SheepShaver).
 *  All functions are in namespace ppc to avoid collisions with the
 *  m68k serial driver in src/core/serial.cpp.
 */

#ifndef KPX_SERIAL_H
#define KPX_SERIAL_H

#include "sysdeps.h"

class SERDPort;

namespace ppc {

extern SERDPort *the_serd_port[2];

extern void SerialInit(void);
extern void SerialExit(void);
extern void SerialInterrupt(void);

extern int16 SerialNothing(uint32 pb, uint32 dce);
extern int16 SerialOpen(uint32 pb, uint32 dce);
extern int16 SerialPrimeIn(uint32 pb, uint32 dce);
extern int16 SerialPrimeOut(uint32 pb, uint32 dce);
extern int16 SerialControl(uint32 pb, uint32 dce);
extern int16 SerialStatus(uint32 pb, uint32 dce);
extern int16 SerialClose(uint32 pb, uint32 dce);

} // namespace ppc

// Pull ppc:: serial into scope for KPX compilation units
using ppc::SerialInit;
using ppc::SerialExit;
using ppc::SerialInterrupt;
using ppc::SerialNothing;
using ppc::SerialOpen;
using ppc::SerialPrimeIn;
using ppc::SerialPrimeOut;
using ppc::SerialControl;
using ppc::SerialStatus;
using ppc::SerialClose;
using ppc::the_serd_port;

#endif
