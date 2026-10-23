/*****************************************************************************
 *                                                                           *
 *                                 ShutDown.h                                *
 *                                 ==========                                *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>

#define MODULE_NAME ShutDown
#include <base/api-module.h>


namespace Executor {

enum {sdOnPowerOff = 1,
sdOnRestart = 2,
sdOnUnmount = 4,
sdOnDrivers = 8,
sdOnRestartOrPower = sdOnPowerOff | sdOnRestart,
};

DISPATCHER_TRAP(ShutDown, 0xA895, StackW);


void C_ShutDwnPower();PASCAL_SUBTRAP(ShutDwnPower, 0xA895, 0x0001, ShutDown);


void C_ShutDwnStart();PASCAL_SUBTRAP(ShutDwnStart, 0xA895, 0x0002, ShutDown);


void C_ShutDwnInstall(ProcPtr shutdown_proc, int16_t flags);PASCAL_SUBTRAP(ShutDwnInstall, 0xA895, 0x0003, ShutDown);


void C_ShutDwnRemove(ProcPtr shutdown_proc);PASCAL_SUBTRAP(ShutDwnRemove, 0xA895, 0x0004, ShutDown);


} /* namespace Executor */