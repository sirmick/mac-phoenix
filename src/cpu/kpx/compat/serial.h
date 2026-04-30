/*
 *  serial.h - KPX serial driver declarations (namespace ppc)
 *
 *  Real implementation in serial_ppc.cpp (ported from SheepShaver).
 *  Function declarations stay in namespace ppc to avoid colliding with
 *  the m68k SerialOpen et al, which take a different parameter list.
 *
 *  Storage (the_serd_port[2]) is shared with the m68k path — both
 *  dispatchers read from the SAME global array, defined in
 *  src/core/serial.cpp:44. Whichever SerialInit ran (PPC's NullSERDPort
 *  factory or, after Phase 3, serial_unix_init) populates this array;
 *  both dispatchers read it.
 *
 *  We can't `#include "serial.h"` from the common include dir — it would
 *  pull THIS file in instead (compat/ is earlier in the include path).
 *  Keep declarations inline; they must stay byte-compatible with
 *  src/common/include/serial.h.
 */

#ifndef KPX_SERIAL_H
#define KPX_SERIAL_H

#include "sysdeps.h"

// SERDPort — copy of the abstract base in common/serial.h. Class
// definitions in different TUs compare ABI-equal as long as fields and
// virtual method signatures match exactly.
class SERDPort {
public:
    SERDPort()
    {
        is_open = false;
        input_dt = output_dt = 0;
    }
    virtual ~SERDPort() {}
    virtual int16 open(uint16 config) = 0;
    virtual int16 prime_in(uint32 pb, uint32 dce) = 0;
    virtual int16 prime_out(uint32 pb, uint32 dce) = 0;
    virtual int16 control(uint32 pb, uint32 dce, uint16 code) = 0;
    virtual int16 status(uint32 pb, uint32 dce, uint16 code) = 0;
    virtual int16 close(void) = 0;
    bool is_open;
    uint8 cum_errors;
    bool read_pending;
    bool read_done;
    uint32 input_dt;
    bool write_pending;
    bool write_done;
    uint32 output_dt;
    uint32 dt_store;  // PPC-only field (POWERPC_ROM in common header).
                      // Always present here to keep ABI-identical layout
                      // for kpx; m68k header conditionally omits it.
};

// Deferred-task structure offsets (mirror of common/serial.h enum).
enum {
    serdtCode = 20,
    serdtResult = 30,
    serdtDCE = 34,
    SIZEOF_serdt = 38
};

// Single global storage; defined in src/core/serial.cpp:44. Both m68k
// and PPC dispatchers index this array.
extern SERDPort *the_serd_port[2];

// Global SerialInit / SerialExit live in serial_adapter.cpp and dispatch
// through g_platform.serial_init / serial_exit (→ serial_unix_init/exit).
// Both PPC and m68k init paths call these.
extern void SerialInit(void);
extern void SerialExit(void);

namespace ppc {

extern void SerialInterrupt(void);

// Driver-trap entry points. PPC-specific signatures (no `port` arg —
// the Device Manager passes it via dCtlRefNum). Not provided as global
// symbols because the m68k SerialOpen et al take a different
// parameter list; collapsing them would shadow each other.
extern int16 SerialNothing(uint32 pb, uint32 dce);
extern int16 SerialOpen(uint32 pb, uint32 dce);
extern int16 SerialPrimeIn(uint32 pb, uint32 dce);
extern int16 SerialPrimeOut(uint32 pb, uint32 dce);
extern int16 SerialControl(uint32 pb, uint32 dce);
extern int16 SerialStatus(uint32 pb, uint32 dce);
extern int16 SerialClose(uint32 pb, uint32 dce);

} // namespace ppc

using ppc::SerialInterrupt;
using ppc::SerialNothing;
using ppc::SerialOpen;
using ppc::SerialPrimeIn;
using ppc::SerialPrimeOut;
using ppc::SerialControl;
using ppc::SerialStatus;
using ppc::SerialClose;

#endif
