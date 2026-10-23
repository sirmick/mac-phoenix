/*****************************************************************************
 *                                                                           *
 *                                 Gestalt.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSErr, OSType, LONGINT                                  *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME Gestalt
#include <base/api-module.h>


namespace Executor {

enum {gestaltPhysicalRAMSize = "ram "_4,
gestaltAddressingModeAttr = "addr"_4,
gestaltAliasMgrAttr = "alis"_4,
gestaltAppleEventsAttr = "evnt"_4,
gestaltAppleTalkVersion = "atlk"_4,
gestaltAUXVersion = "a/ux"_4,
gestaltConnMgrAttr = "conn"_4,
gestaltCRMAttr = "crm "_4,
gestaltCTBVersion = "ctbv"_4,
gestaltDBAccessMgrAttr = "dbac"_4,
gestaltDITLExtAttr = "ditl"_4,
gestaltEasyAccessAttr = "easy"_4,
gestaltEditionMgrAttr = "edtn"_4,
gestaltExtToolboxTable = "xttt"_4,
gestaltFindFolderAttr = "fold"_4,
gestaltFontMgrAttr = "font"_4,
gestaltFPUType = "fpu "_4,
gestaltFSAttr = "fs  "_4,
gestaltFXfrMgrAttr = "fxfr"_4,
gestaltHardwareAttr = "hdwr"_4,
gestaltHelpMgrAttr = "help"_4,
gestaltIconUtilitiesAttr = "icon"_4,
gestaltKeyboardType = "kbd "_4,
gestaltLogicalPageSize = "pgsz"_4,
gestaltLogicalRAMSize = "lram"_4,
gestaltLowMemorySize = "lmem"_4,
gestaltMiscAttr = "misc"_4,
gestaltMMUType = "mmu "_4,
gestaltNotificatinMgrAttr = "nmgr"_4,
gestaltNuBusConnectors = "sltc"_4,
gestaltOSAttr = "os  "_4,
gestaltOSTable = "ostt"_4,
gestaltParityAttr = "prty"_4,
gestaltPopupAttr = "pop!"_4,
gestaltPowerMgrAttr = "powr"_4,
gestaltPPCToolboxAttr = "ppc "_4,
gestaltProcessorType = "proc"_4,
gestaltQuickdrawVersion = "qd  "_4,
gestaltQuickdrawFeatures = "qdrw"_4,
gestaltResourceMgrAttr = "rsrc"_4,
gestaltScriptCount = "scr#"_4,
gestaltScriptMgrVersion = "scri"_4,
gestaltSoundAttr = "snd "_4,
gestaltSpeechAttr = "ttsc"_4,
gestaltStandardFileAttr = "stdf"_4,
gestaltStdNBPAttr = "nlup"_4,
gestaltTermMgrAttr = "term"_4,
gestaltTextEditVersion = "te  "_4,
gestaltTimeMgrVersion = "tmgr"_4,
gestaltToolboxTable = "tbbt"_4,
gestaltVersion = "vers"_4,
gestaltVMAttr = "vm  "_4,
gestaltMachineIcon = "micn"_4,
gestaltMachineType = "mach"_4,
gestaltROMSize = "rom "_4,
gestaltROMVersion = "romv"_4,
gestaltSystemVersion = "sysv"_4,
gestaltNativeCPUtype = "cput"_4,
gestaltSysArchitecture = "sysa"_4,
};

enum {gestaltMacQuadra610 = 53,
gestaltCPU68040 = 4,
gestaltCPU601 = 257,
gestaltCPU603 = 259,
gestaltCPU604 = 260,
gestaltCPU603e = 262,
gestaltCPU603ev = 263,
gestaltCPU750 = 264, // G3
gestaltCPU604e = 265,
gestaltCPU604ev = 266,
gestaltCPUG4 = 268, // determined by running test app on Sybil
gestaltNoMMU = 0,
gestalt68k = 1,
gestaltPowerPC = 2,
};

enum {gestalt32BitAddressing = 0,
gestalt32BitSysZone = 1,
gestalt32BitCapable = 2,
};

// gestaltHardwareAttr return values
enum {gestaltHasVIA1 = 0,
gestaltHasVIA2 = 1,
gestaltHasASC = 3,
gestaltHasSSC = 4,
gestaltHasSCI = 7,
};

enum {gestaltEasyAccessOff = 1 << 0,
gestalt68881 = 1,
gestaltMacKbd = 1,
gestalt68040MMu = 4,
gestalt68000 = 1,
gestalt68040 = 5,
};

enum {gestaltOriginalQD = 0,
gestalt8BitQD = 256,
gestalt32BitQD = 512,
gestalt32BitQD11 = 528,
gestalt32BitQD12 = 544,
gestalt32BitQD13 = 560,
};

enum {gestaltHasColor = 0,
gestaltHasDeepGWorlds = 1,
gestaltHasDirectPixMaps = 2,
gestaltHasGrayishTextOr = 3,
};

enum {gestaltTE1 = 1,
gestaltTE2 = 2,
gestaltTE3 = 3,
gestaltTE4 = 4,
gestaltTE5 = 5,
};

enum {gestaltDITLExtPresent = 0,
};

enum {gestaltStandardTimeMgr = 1,
gestaltVMPresent = 1 << 0,
};

enum {gestaltClassic = 1,
gestaltMacXL = 2,
gestaltMac512KE = 3,
gestaltMacPlus = 4,
gestaltMacSE = 5,
gestaltMacII = 6,
gestaltMacIIx = 7,
gestaltMacIIcx = 8,
gestaltMacSE30 = 9,
gestaltPortable = 10,
gestaltMacIIci = 11,
gestaltMacIIfx = 13,
gestaltMacClassic = 17,
gestaltMacIIsi = 18,
gestaltMacLC = 19,
};

enum {gestaltHasFSSpecCalls = 1 << 1,
};

enum {gestaltStandardFile58 = 1 << 0,
};

enum {gestaltUndefSelectorErr = -5551,
gestaltUnknownErr = -5550,
gestaltDupSelectorErr = -5552,
gestaltLocationErr = -5553,
};

enum {gestaltSerialAttr = "ser "_4,
gestaltHasGPIaToDCDa = 0,
gestaltHasGPIaToRTxCa = 1,
gestaltHasGPIbToDCDb = 2,
gestaltHidePortA = 3,
gestaltHidePortB = 4,
gestaltPortADisabled = 5,
gestaltPortBDisabled = 6,
};

enum {gestaltOpenTpt = "otan"_4,
};

using SelectorFunctionUPP = UPP<OSErr(OSType , GUEST<LONGINT> *)>;


DISPATCHER_TRAP(GestaltDispatch, 0xA1AD, TrapBits);


enum {_Gestalt = 41389,
};

OSErr C_Gestalt(OSType selector, GUEST<LONGINT> *responsep);REGISTER_SUBTRAP(Gestalt, 0xA1AD, 0x0000, GestaltDispatch, D0(D0, Out<LONGINT, A0>));


OSErr C_NewGestalt(OSType selector, SelectorFunctionUPP selFunc);REGISTER_SUBTRAP(NewGestalt, 0xA1AD, 0x0200, GestaltDispatch, D0(D0, A0));


OSErr C_ReplaceGestalt(OSType selector, SelectorFunctionUPP selFunc, GUEST<SelectorFunctionUPP> *oldSelFuncp);REGISTER_SUBTRAP(ReplaceGestalt, 0xA1AD, 0x0400, GestaltDispatch, D0(D0, A0, Out<SelectorFunctionUPP, A0>));


// System 7.5's GestaltValue library: a selector that answers with a
// stored value instead of a function.  D0's low byte selects, its high
// byte is the parameter size in words (the ROM pops that many).  7.5.5
// answers paramErr for an unknown selector.
DISPATCHER_TRAP(GestaltValueDispatch, 0xABF1, D0<0xFF>);


// Installs selector answering newValue; gestaltDupSelectorErr if it exists already (as a value, a function or a built-in).
OSErr C_NewGestaltValue(OSType selector, int32_t newValue);PASCAL_SUBTRAP(NewGestaltValue, 0xABF1, 0x0401, GestaltValueDispatch);


// Makes an existing selector answer replacementValue; gestaltUndefSelectorErr if there is none.
OSErr C_ReplaceGestaltValue(OSType selector, int32_t replacementValue);PASCAL_SUBTRAP(ReplaceGestaltValue, 0xABF1, 0x0402, GestaltValueDispatch);


// Removes a selector installed by the value calls; gestaltUndefSelectorErr for any other.
OSErr C_DeleteGestaltValue(OSType selector);PASCAL_SUBTRAP(DeleteGestaltValue, 0xABF1, 0x0203, GestaltValueDispatch);


// Installs or replaces: selector answers newValue whether or not it existed before.  Find File Extension registers 'fndf' with it.
OSErr C_SetGestaltValue(OSType selector, int32_t newValue);PASCAL_SUBTRAP(SetGestaltValue, 0xABF1, 0x0404, GestaltValueDispatch);


} /* namespace Executor */