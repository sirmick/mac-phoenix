/*
 * rsys/mactcp.h — native MacTCP (.IPP) driver and resolver for Executor 2000.
 */
#if !defined(_RSYS_MACTCP_H_)
#define _RSYS_MACTCP_H_

#include <base/common.h>

namespace Executor
{
void InitMacTCPDriver();

/* Raise the network interrupt (any thread): the driver's parked calls,
 * ASRs and finished resolver lookups are served there. */
void ROMlib_mactcp_wake();

/* The resolver (mactcp/dnr.cpp): a fresh, locked 'dnrp' code handle for
 * GetIndResource when no file in the chain has one; and the delivery of
 * finished lookups, from the network interrupt. */
Handle ROMlib_dnr_resource();
void ROMlib_dnr_pump();
}
#endif
