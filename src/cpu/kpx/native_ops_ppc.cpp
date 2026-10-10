/*
 *  native_ops_ppc.cpp - the NativeOp table (SheepShaver's NATIVE_OP
 *  selectors) on a caller-supplied GPR array, shared by every PPC core's
 *  installer (cpu_ppc_kpx.cpp, cpu_ppc_mame.cpp). Moved out of
 *  cpu_ppc_kpx.cpp unchanged. The GET_RESOURCE family returns false: it
 *  re-enters PPC code and so lives with each installer.
 *
 *  Original: SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Licensed under GPL v2+
 */
#include "sysdeps.h"
#include "kpx_cpu_emulation.h"
#include "main.h"
#include "xlowmem.h"
#include "emul_op.h"
#include "thunks.h"
#include "macos_util.h"
#include "rom_patches.h"
#include "video.h"
#include "name_registry.h"
#include "serial.h"
#include "ether.h"
#include "timer.h"

#include <cstdio>

using namespace ppc;

extern "C" void check_load_invoc(uint32 type, int16 id, uint32 h);
extern "C" void named_check_load_invoc(uint32 type, uint32 name, uint32 h);

// Backend-agnostic NativeOp body — operates on a caller-supplied GPR array
// rather than `this->gpr(i)` so another PPC backend could invoke it via
// g_platform.ppc_native_op after marshaling its own GPRs. Returns true if
// the selector was handled; false means the caller must dispatch via a CPU
// singleton (GET_RESOURCE family re-enters PPC via execute_ppc()).
bool ppc_native_op_pure(uint32 selector, uint32 *gprs)
{
    switch (selector) {
    case NATIVE_PATCH_NAME_REGISTRY:
        DoPatchNameRegistry();
        return true;
    case NATIVE_VIDEO_INSTALL_ACCEL:
        VideoInstallAccel();
        return true;
    case NATIVE_VIDEO_VBL:
        VideoVBL();
        return true;
    case NATIVE_VIDEO_DO_DRIVER_IO:
        gprs[3] = (int32)(int16)VideoDoDriverIO(gprs[3], gprs[4], gprs[5], gprs[6], gprs[7]);
        return true;
    case NATIVE_ETHER_AO_GET_HWADDR:
        AO_get_ethernet_address(gprs[3]);
        return true;
    case NATIVE_ETHER_AO_ADD_MULTI:
        AO_enable_multicast(gprs[3]);
        return true;
    case NATIVE_ETHER_AO_DEL_MULTI:
        AO_disable_multicast(gprs[3]);
        return true;
    case NATIVE_ETHER_AO_SEND_PACKET:
        AO_transmit_packet(gprs[3]);
        return true;
    case NATIVE_ETHER_IRQ:
        EtherIRQ();
        return true;
    case NATIVE_ETHER_INIT:
        gprs[3] = InitStreamModule((void *)(uintptr_t)gprs[3]);
        return true;
    case NATIVE_ETHER_TERM:
        TerminateStreamModule();
        return true;
    case NATIVE_ETHER_OPEN:
        gprs[3] = ether_open((queue_t *)(uintptr_t)gprs[3], (void *)(uintptr_t)gprs[4], gprs[5], gprs[6], (void*)(uintptr_t)gprs[7]);
        return true;
    case NATIVE_ETHER_CLOSE:
        gprs[3] = ether_close((queue_t *)(uintptr_t)gprs[3], gprs[4], (void *)(uintptr_t)gprs[5]);
        return true;
    case NATIVE_ETHER_WPUT:
        gprs[3] = ether_wput((queue_t *)(uintptr_t)gprs[3], (mblk_t *)(uintptr_t)gprs[4]);
        return true;
    case NATIVE_ETHER_RSRV:
        gprs[3] = ether_rsrv((queue_t *)(uintptr_t)gprs[3]);
        return true;
    case NATIVE_NQD_SYNC_HOOK:
        gprs[3] = NQD_sync_hook(gprs[3]);
        return true;
    case NATIVE_NQD_UNKNOWN_HOOK:
        gprs[3] = NQD_unknown_hook(gprs[3]);
        return true;
    case NATIVE_NQD_BITBLT_HOOK:
        gprs[3] = NQD_bitblt_hook(gprs[3]);
        return true;
    case NATIVE_NQD_BITBLT:
        NQD_bitblt(gprs[3]);
        return true;
    case NATIVE_NQD_FILLRECT_HOOK:
        gprs[3] = NQD_fillrect_hook(gprs[3]);
        return true;
    case NATIVE_NQD_INVRECT:
        NQD_invrect(gprs[3]);
        return true;
    case NATIVE_NQD_FILLRECT:
        NQD_fillrect(gprs[3]);
        return true;
    case NATIVE_SERIAL_NOTHING:
    case NATIVE_SERIAL_OPEN:
    case NATIVE_SERIAL_PRIME_IN:
    case NATIVE_SERIAL_PRIME_OUT:
    case NATIVE_SERIAL_CONTROL:
    case NATIVE_SERIAL_STATUS:
    case NATIVE_SERIAL_CLOSE: {
        typedef int16 (*SerialCallback)(uint32, uint32);
        static const SerialCallback serial_callbacks[] = {
            SerialNothing, SerialOpen, SerialPrimeIn, SerialPrimeOut,
            SerialControl, SerialStatus, SerialClose
        };
        gprs[3] = serial_callbacks[selector - NATIVE_SERIAL_NOTHING](gprs[3], gprs[4]);
        return true;
    }
    case NATIVE_MAKE_EXECUTABLE:
        MakeExecutable(0, gprs[4], gprs[5]);
        return true;
    case NATIVE_CHECK_LOAD_INVOC:
        check_load_invoc(gprs[3], gprs[4], gprs[5]);
        return true;
    case NATIVE_NAMED_CHECK_LOAD_INVOC:
        named_check_load_invoc(gprs[3], gprs[4], gprs[5]);
        return true;
    case NATIVE_GET_RESOURCE:
    case NATIVE_GET_1_RESOURCE:
    case NATIVE_GET_IND_RESOURCE:
    case NATIVE_GET_1_IND_RESOURCE:
    case NATIVE_R_GET_RESOURCE:
    case NATIVE_GET_NAMED_RESOURCE:
    case NATIVE_GET_1_NAMED_RESOURCE:
        return false;  // needs CPU singleton (execute_ppc)
    default:
        printf("FATAL: NATIVE_OP called with bogus selector %d\n", selector);
        QuitEmulator();
        return true;
    }
}

// Convert common M68K_EMUL_OP_* values (0x71xx from common/include/emul_op.h)
// to KPX encoding (M68K_EMUL_BREAK + OP_*, 0xFExx).
// Common 0x71xx opcodes are valid m68k (moveq #xx, d0) — the DR emulator
// executes them silently instead of trapping to the emulop handler.
extern "C" uint16_t kpx_make_emulop_shared(uint16_t op)
{
    // Common encoding: 0x7100 + enum offset
    // Map to KPX encoding: M68K_EMUL_BREAK + OP_*
    switch (op) {
    case 0x7108: return M68K_EMUL_OP_FIX_BOOTSTACK;
    case 0x7109: return M68K_EMUL_OP_FIX_MEMSIZE;
    case 0x710A: return M68K_EMUL_OP_INSTALL_DRIVERS;
    case 0x710C: return M68K_EMUL_OP_SONY_OPEN;
    case 0x710D: return M68K_EMUL_OP_SONY_PRIME;
    case 0x710E: return M68K_EMUL_OP_SONY_CONTROL;
    case 0x710F: return M68K_EMUL_OP_SONY_STATUS;
    case 0x7110: return M68K_EMUL_OP_DISK_OPEN;
    case 0x7111: return M68K_EMUL_OP_DISK_PRIME;
    case 0x7112: return M68K_EMUL_OP_DISK_CONTROL;
    case 0x7113: return M68K_EMUL_OP_DISK_STATUS;
    case 0x7114: return M68K_EMUL_OP_CDROM_OPEN;
    case 0x7115: return M68K_EMUL_OP_CDROM_PRIME;
    case 0x7116: return M68K_EMUL_OP_CDROM_CONTROL;
    case 0x7117: return M68K_EMUL_OP_CDROM_STATUS;
    case 0x7123: return M68K_EMUL_OP_ADBOP;
    case 0x7124: return M68K_EMUL_OP_INSTIME;
    case 0x7125: return M68K_EMUL_OP_RMVTIME;
    case 0x7126: return M68K_EMUL_OP_PRIMETIME;
    case 0x7127: return M68K_EMUL_OP_MICROSECONDS;
    case 0x7128: return M68K_EMUL_OP_SCSI_DISPATCH;
    case 0x7129: return M68K_EMUL_OP_IRQ;
    case 0x712A: return M68K_EMUL_OP_PUT_SCRAP;
    case 0x712B: return M68K_EMUL_OP_GET_SCRAP;
    case 0x712C: return M68K_EMUL_OP_CHECKLOAD;
    case 0x712E: return M68K_EMUL_OP_EXTFS_COMM;
    case 0x712F: return M68K_EMUL_OP_EXTFS_HFS;
    case 0x7131: return M68K_EMUL_OP_SOUNDIN_OPEN;
    case 0x7132: return M68K_EMUL_OP_SOUNDIN_PRIME;
    case 0x7133: return M68K_EMUL_OP_SOUNDIN_CONTROL;
    case 0x7134: return M68K_EMUL_OP_SOUNDIN_STATUS;
    case 0x7135: return M68K_EMUL_OP_SOUNDIN_CLOSE;
    case 0x7137: return M68K_EMUL_OP_IDLE_TIME;
    default:
        fprintf(stderr, "[KPX] WARNING: unmapped common emulop 0x%04x\n", op);
        return op;
    }
}
