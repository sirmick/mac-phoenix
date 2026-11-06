/*****************************************************************************
 *                                                                           *
 *                                   PCI.h                                   *
 *                                   =====                                   *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          OSErr, UInt8, UInt16, UInt32                      *
 *     NameRegistry.h      RegEntryIDPtr                                     *
 *     DriverServices.h    LogicalAddress                                    *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "NameRegistry.h"
#include "DriverServices.h"

#define MODULE_NAME PCI
#include <base/api-module.h>


namespace Executor {

// configAddr is typed as an address but carries a register offset in
// configuration space, 0 to 255.  The word and long routines swap
// bytes, so values arrive as they appear on the bus.
OSErr ExpMgrConfigReadByte(RegEntryIDPtr node, LogicalAddress configAddr, UInt8 *valuePtr);

OSErr ExpMgrConfigReadWord(RegEntryIDPtr node, LogicalAddress configAddr, GUEST<UInt16> *valuePtr);

OSErr ExpMgrConfigReadLong(RegEntryIDPtr node, LogicalAddress configAddr, GUEST<UInt32> *valuePtr);

OSErr ExpMgrConfigWriteByte(RegEntryIDPtr node, LogicalAddress configAddr, UInt8 value);

OSErr ExpMgrConfigWriteWord(RegEntryIDPtr node, LogicalAddress configAddr, UInt16 value);

OSErr ExpMgrConfigWriteLong(RegEntryIDPtr node, LogicalAddress configAddr, UInt32 value);

} /* namespace Executor */