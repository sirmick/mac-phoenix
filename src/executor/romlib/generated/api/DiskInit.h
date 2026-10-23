/*****************************************************************************
 *                                                                           *
 *                                 DiskInit.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER, LONGINT, OSErr, StringPtr                      *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME DiskInit
#include <base/api-module.h>


namespace Executor {

DISPATCHER_TRAP(Pack2, 0xA9E9, StackW);


void C_DILoad();PASCAL_SUBTRAP(DILoad, 0xA9E9, 0x0002, Pack2);


void C_DIUnload();PASCAL_SUBTRAP(DIUnload, 0xA9E9, 0x0004, Pack2);


INTEGER C_DIBadMount(Point pt, LONGINT evtmess);PASCAL_SUBTRAP(DIBadMount, 0xA9E9, 0x0000, Pack2);


OSErr C_DIFormat(INTEGER dn);PASCAL_SUBTRAP(DIFormat, 0xA9E9, 0x0006, Pack2);


OSErr C_DIVerify(INTEGER dn);PASCAL_SUBTRAP(DIVerify, 0xA9E9, 0x0008, Pack2);


OSErr C_DIZero(INTEGER dn, StringPtr vname);PASCAL_SUBTRAP(DIZero, 0xA9E9, 0x000A, Pack2);


} /* namespace Executor */