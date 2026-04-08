/*
 *  main.h - KPX compatibility stub
 *
 *  Stubs for SheepShaver main.h references from KPX code.
 */

#ifndef KPX_MAIN_H
#define KPX_MAIN_H

#include "sysdeps.h"
#include "ppc_memory.h"

extern "C" {
#include "platform.h"  // InterruptFlags, SetInterruptFlag, ClearInterruptFlag, TriggerInterrupt
}

// KPX code references these but we stub them out
extern void QuitEmulator(void);
extern void MakeExecutable(int dummy, uint32 start, uint32 length);
extern void DisableInterrupt(void);
extern void EnableInterrupt(void);
// idle_resume declared in platform.h (C linkage)

// KernelData address (set during PPC init) — ppc:: namespace
namespace ppc { extern uint32 KernelDataAddr; }
using ppc::KernelDataAddr;

// ROM type — MUST match SheepShaver's enum order in rom_patches.h!
enum {
    ROMTYPE_TNT,        // 0
    ROMTYPE_ALCHEMY,    // 1
    ROMTYPE_ZANZIBAR,   // 2
    ROMTYPE_GAZELLE,    // 3
    ROMTYPE_GOSSAMER,   // 4
    ROMTYPE_NEWWORLD    // 5
};
// ROMType is in namespace ppc — declared in rom_patches.h

// Interrupt flags — must match legacy SheepShaver main.h enum
enum {
    INTFLAG_VIA    = 1,     // 60.15Hz VBL
    INTFLAG_SERIAL = 2,     // Serial driver
    INTFLAG_ETHER  = 4,     // Ethernet driver
    INTFLAG_AUDIO  = 16,    // Audio block read
    INTFLAG_TIMER  = 32,    // Time Manager
    INTFLAG_ADB    = 64,    // ADB
    INTFLAG_NMI    = 128    // NMI (Programmer's Key)
};
#define INTFLAG_1HZ  (1 << 1)   // mac-phoenix addition (alias for 1Hz tick)
// InterruptFlags, SetInterruptFlag, ClearInterruptFlag, TriggerInterrupt
// are declared in platform.h (backend-agnostic, C linkage)
extern void DisableInterrupt(void);
extern void EnableInterrupt(void);
extern void ADBInterrupt(void);
extern void ExecuteNative(int selector);

// Signal stack
extern uintptr SignalStackBase(void);

// Timebase and clock speeds for PPC (int64 to match legacy SheepShaver)
extern int64 TimebaseSpeed;
extern int64 BusClockSpeed;
extern int64 CPUClockSpeed;
extern uint32 PVR;
extern uint64 GetTicks_usec(void);
static inline uint64 muldiv64(uint64 a, uint64 b, uint64 c) { return (a * b) / c; }

// BootGlobs address
extern uint32 BootGlobsAddr;

// Alert/error functions
static inline void WarningAlert(const char *msg) { fprintf(stderr, "WARNING: %s\n", msg); }
static inline void ErrorAlert(const char *msg) { fprintf(stderr, "ERROR: %s\n", msg); }

// Macros
#define FOURCC(a,b,c,d) (((uint32)(a)<<24)|((uint32)(b)<<16)|((uint32)(c)<<8)|(uint32)(d))

#endif /* KPX_MAIN_H */
