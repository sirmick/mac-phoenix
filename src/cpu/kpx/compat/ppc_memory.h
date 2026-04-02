/*
 *  ppc_memory.h - PPC subsystem declarations
 *
 *  Extern declarations for functions used by KPX PPC emulation code.
 *  Implementations are in core/, drivers/, or cpu/kpx/ as noted.
 */

#ifndef PPC_MEMORY_H
#define PPC_MEMORY_H

#include "sysdeps.h"

// Timer functions — extern "C" to match core/timer.cpp linkage
#ifdef __cplusplus
extern "C" {
#endif
extern int16 InsTime(uint32 tm, uint16 proc);
extern int16 RmvTime(uint32 tm);
extern int16 PrimeTime(uint32 tm, int32 time);
extern void TimerReset(void);
extern void TimerInterrupt(void);
extern void TimerInit(void);
extern void TimerExit(void);
#ifdef __cplusplus
}
// Microseconds: defined in ppc_memory.cpp
extern void Microseconds(uint32 &hi, uint32 &lo);
#endif

// MacOS utility functions
extern void MacOSUtilReset(void);
extern uint32 FindLibSymbol(const char *lib, const char *sym);
extern void InitCallUniversalProc(void);

// Driver installation (in namespace ppc, declared in rom_patches.h)

// Name Registry
extern void PatchNameRegistry(void);

// Ethernet
extern void EtherResetCachedAllocation(void);
extern void ether_reset(void);
// SerialInterrupt declared in compat/serial.h (namespace ppc)
extern void EtherInterrupt(void);

// Resource manager
extern void PatchNativeResourceManager(void);

// Audio
struct audio_status;
extern void AddSifter(uint32 type, int16 id);
extern bool FindSifter(uint32 type, int16 id);

// Idle
extern void idle_wait(void);

// Subsystem init/exit (may be shared with m68k via common headers,
// or stubbed out for PPC)
extern void EtherInit(void);
extern void EtherExit(void);
// SerialInit/SerialExit/SerialInterrupt declared in compat/serial.h (namespace ppc)
// TimerInit/TimerExit declared above with extern "C"
// VideoInit/VideoExit provided by compat/video.h
extern void ClipInit(void);
extern void ClipExit(void);
extern void XPRAMInit(const char *vmdir);
extern void XPRAMExit(void);
extern void SonyInit(void);
extern void SonyExit(void);
extern void DiskInit(void);
extern void DiskExit(void);
extern void CDROMInit(void);
extern void CDROMExit(void);
extern void SCSIInit(void);
extern void SCSIExit(void);
extern void AudioInit(void);
extern void AudioExit(void);
extern void ADBInit(void);
extern void ADBExit(void);
extern void ExtFSInit(void);
extern void ExtFSExit(void);
extern uint8 XPRAM[];  // 8KB XPRAM buffer

// Interrupt flags — defined as enum in main.h (matching legacy SheepShaver)
// Do NOT redefine here; include main.h instead.

// Resource patches (in namespace ppc, declared in rsrc_patches.h)

// rom_patches.h declarations (in namespace ppc, declared in rom_patches.h)

// version.h
#ifndef VERSION_STRING
#define VERSION_STRING "mac-phoenix 2.0"
#endif

#endif /* PPC_MEMORY_H */
