/*
 *  cpu_ppc_kpx.cpp - KPX (Kheperix) PPC CPU backend for Platform API
 *
 *  Adapted from SheepShaver's sheepshaver_glue.cpp.
 *  Implements the Platform function pointer table for PPC emulation
 *  using the Kheperix interpreter (no JIT).
 *
 *  Original: SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Kheperix (C) 2003-2005 Gwenole Beauchesne
 *  Licensed under GPL v2+
 */

#include "sysdeps.h"
#include <sys/mman.h>
#include "kpx_cpu_emulation.h"

// ppc_insn_counter defined in ppc-cpu.cpp
#include "main.h"
#include "xlowmem.h"
#include "emul_op.h"
#include "thunks.h"
#include "macos_util.h"
#include "block-alloc.hpp"
#include "cpu/ppc/ppc-cpu.hpp"
#include "cpu/ppc/ppc-operations.hpp"
#include "cpu/ppc/ppc-instructions.hpp"
#include "rom_patches.h"
#include "video.h"
#include "name_registry.h"
#include "serial.h"
#include "ether.h"
#include "timer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

extern "C" unsigned int boot_progress_checkloads(void);
extern "C" void boot_progress_export_cursor_to_ipc(void);
extern "C" void boot_progress_export_app_to_ipc(void);
extern "C" void boot_progress_export_mac_state(void);


#define DEBUG 0
#include "debug.h"

using namespace ppc;

// Include mac-phoenix Platform API and SIGSEGV handler
#include "../../common/include/sigsegv.h"
extern "C" {
#include "platform.h"
}

// ============================================================================
// Globals required by KPX interpreter
// ============================================================================

// REAL_ADDRESSING: VMBaseDiff = 0 (defined as const in vm.hpp)
// No need for gZeroPage, gKernelData, gPageTable etc. — all mapped directly

// PPC-only globals — SheepShaver naming, ppc:: namespace.
// BasiliskII uses RAMBaseMac/ROMBaseMac (different globals in basilisk_glue.cpp).
// Set by cpu_context.cpp:init_ppc().
namespace ppc {
    uint32 RAMBase = 0;
    uint32 RAMSize = 0;
    uint8 *RAMBaseHost = nullptr;
    uint32 ROMBase = 0;
    uint8 *ROMBaseHost = nullptr;
    uint32 KernelDataAddr = KERNEL_DATA_BASE;
}

// InterruptFlags: SHARED — single definition in uae_wrapper.cpp.
// SetInterruptFlag/ClearInterruptFlag: SHARED — defined in uae_wrapper.cpp.

// PVR (Processor Version Register) - PowerPC 7400 with AltiVec (matching SheepShaver default)
uint32 PVR = 0x000c0000;

// Clock speeds (matching SheepShaver defaults, int64 to match legacy)
int64 TimebaseSpeed = 25000000;    // 25 MHz timebase
int64 BusClockSpeed = 100000000;   // 100 MHz bus
int64 CPUClockSpeed = 100000000;   // 100 MHz CPU

// BootGlobs address (set during init, at top of RAM)
uint32 BootGlobsAddr = 0;

// GetTicks_usec provided by timer_unix.cpp

// SheepMem static members (stubbed - initialized during PPC boot, out of scope)
uint32  SheepMem::page_size = 4096;
uintptr SheepMem::zero_page = 0;
uintptr SheepMem::base = 0;
uintptr SheepMem::data = 0;
uintptr SheepMem::proc = 0;

// ============================================================================
// Stub functions referenced by KPX code (implemented during PPC boot, out of scope)
// ============================================================================

// QuitEmulator: single definition in main.cpp (calls ExitAll + exit).
// No stub needed — KPX links against the same binary.

// HasMacStarted provided by common/include/macos_util.h (static inline)
extern int dt_seq;
void MakeExecutable(int dummy, uint32 start, uint32 length)
{
	if ((start >= ROMBase) && (start < (ROMBase + ROM_SIZE)))
		return;
	FlushCodeCache(start, start + length);
}
// Interrupt enable/disable — must match SheepShaver's main_unix.cpp
// DisableInterrupt increments XLM_IRQ_NEST to block HandleInterrupt
// EnableInterrupt decrements it to re-allow interrupts
void DisableInterrupt(void)
{
    WriteMacInt32(XLM_IRQ_NEST, int32(ReadMacInt32(XLM_IRQ_NEST)) + 1);
}
void EnableInterrupt(void)
{
    WriteMacInt32(XLM_IRQ_NEST, int32(ReadMacInt32(XLM_IRQ_NEST)) - 1);
}

// idle_resume provided by timer_unix.cpp

// SetInterruptFlag/ClearInterruptFlag: SHARED — defined in uae_wrapper.cpp,
// declared in platform.h. No duplicate needed here.

// ADBInterrupt: use core/adb.cpp's full implementation (matches legacy SheepShaver).
// Legacy EMULATED_PPC calls Execute68k() from ADBInterrupt — reentrancy is safe.
// TimerInterrupt: use core's version (needed for Mac OS Time Manager)
// ExecuteNative: real implementation in thunks_ppc.cpp

// Signal stack for PPC interrupt handling.
// Must be in Mac address space. Set to ROMEnd (= ROMBase + ROM_AREA_SIZE)
// during initialization, matching legacy SheepShaver (main_unix.cpp:2340).
static uintptr sig_stack = 0;
static const uint32 SIG_STACK_SIZE = 0x10000;  // 64KB

uintptr SignalStackBase(void)
{
    return sig_stack + SIG_STACK_SIZE;
}

// Called from cpu_context.cpp after ROM area is allocated
extern "C" void kpx_set_signal_stack(uintptr addr)
{
    sig_stack = addr;
}

// ThunksInit/ThunksExit: real implementations in thunks_ppc.cpp
// NativeOpcode/NativeTVECT/NativeFunction/NativeRoutineDescriptor: in thunks_ppc.cpp

// SheepMem::Init/Exit implemented in ppc_memory.cpp

// Execute68k/Execute68kTrap: implemented below after sheepshaver_cpu class

// EmulOp is implemented in emul_op_ppc.cpp (lifted from SheepShaver)

// FlushCodeCache - forward declared, defined after sheepshaver_cpu class
void FlushCodeCache(uintptr start, uintptr end);

// check_load_invoc implemented in rsrc_patches_ppc.cpp
extern "C" void check_load_invoc(uint32 type, int16 id, uint32 h);
extern "C" void named_check_load_invoc(uint32 type, uint32 name, uint32 h);



// Enable Execute68k() safety checks? (matches legacy)
#define SAFE_EXEC_68K 1

// Save FP state in Execute68k()? (matches legacy)
#define SAVE_FP_EXEC_68K 1

// Interrupts in EMUL_OP mode? (matches legacy)
#define INTERRUPTS_IN_EMUL_OP_MODE 1

// Interrupts in native mode? (matches legacy)
#define INTERRUPTS_IN_NATIVE_MODE 1

// SIGSEGV handler
sigsegv_return_t sigsegv_handler(sigsegv_address_t, sigsegv_address_t);

#if PPC_ENABLE_JIT && PPC_REENTRANT_JIT
// Special trampolines for EmulOp and NativeOp
static uint8 *emul_op_trampoline;
static uint8 *native_op_trampoline;
#endif

// ============================================================================
// SheepShaver CPU class (from sheepshaver_glue.cpp)
// ============================================================================

enum {
    PPC_I(SHEEP) = PPC_I(MAX),
    PPC_I(SHEEP_MAX)
};

class sheepshaver_cpu
    : public powerpc_cpu
{
    void init_decoder();
    void execute_sheep(uint32 opcode);

public:
    sheepshaver_cpu();

    // CR & XER accessors
    uint32 get_cr() const    { return cr().get(); }
    void set_cr(uint32 v)    { cr().set(v); }
    uint32 get_xer() const   { return xer().get(); }
    void set_xer(uint32 v)   { xer().set(v); }

    // Execute NATIVE_OP routine
    void execute_native_op(uint32 native_op);
    static void call_execute_native_op(powerpc_cpu *cpu, uint32 native_op);

    // Execute EMUL_OP routine
    void execute_emul_op(uint32 emul_op);
    static void call_execute_emul_op(powerpc_cpu *cpu, uint32 emul_op);

    // Execute 68k routine
    void execute_68k(uint32 entry, M68kRegisters *r);

    // Execute ppc routine
    void execute_ppc(uint32 entry);

    // Execute MacOS/PPC code
    uint32 execute_macos_code(uint32 tvect, int nargs, uint32 const *args);

#if PPC_ENABLE_JIT
    // Compile one instruction
    virtual int compile1(codegen_context_t & cg_context);
#endif
    // Resource manager thunk
    void get_resource(uint32 old_get_resource);
    static void call_get_resource(powerpc_cpu *cpu, uint32 old_get_resource);

    // Handle MacOS interrupt
    void interrupt(uint32 entry);

    // Public wrapper for Platform API stop mechanism
    void request_stop() { spcflags().set(SPCFLAG_CPU_EXEC_RETURN); }

    // Make sure the SIGSEGV handler can access CPU registers
    friend sigsegv_return_t sigsegv_handler(sigsegv_info_t *sip);
};

// PowerPC EXEC_RETURN opcode
const uint32 POWERPC_EXEC_RETURN = POWERPC_EMUL_OP | 1;

// Pointer to KernelData
static KernelData *kernel_data;

// Global CPU instance
sheepshaver_cpu *ppc_cpu = nullptr;

// Note: Legacy SheepShaver (EMULATED_PPC) uses no SIGUSR2 handler.
// Interrupts are driven by PPC_CHECK_INTERRUPTS=1 which makes
// trigger_interrupt() set spcflags, checked at block boundaries.

// Execute68k / Execute68kTrap — removed.
// Single canonical C-linkage versions live in basilisk_glue.cpp and dispatch
// through g_platform.cpu_execute_68k[_trap] → kpx_cpu_execute_68k[_trap].
// KPX registers its implementations via cpu_ppc_kpx_install().

// FlushCodeCache (called when code is patched)
void FlushCodeCache(uintptr start, uintptr end)
{
    if (ppc_cpu)
        ppc_cpu->invalidate_cache_range(start, end);
}

// Stop flag
static std::atomic<bool> kpx_stop_requested{false};


// ============================================================================
// sheepshaver_cpu implementation
// ============================================================================

sheepshaver_cpu::sheepshaver_cpu()
{
    init_decoder();
}

void sheepshaver_cpu::init_decoder()
{
    static const instr_info_t sheep_ii_table[] = {
        { "sheep",
          (execute_pmf)&sheepshaver_cpu::execute_sheep,
          PPC_I(SHEEP),
          D_form, 6, 0, CFLOW_JUMP | CFLOW_TRAP
        }
    };

    const int ii_count = sizeof(sheep_ii_table) / sizeof(sheep_ii_table[0]);
    for (int i = 0; i < ii_count; i++)
        init_decoder_entry(&sheep_ii_table[i]);
}

// NativeOp instruction format:
// +------+-------------------------+--+-----------+------+
// |  6   |                         |FN|    OP     |  2   |
// +------+-------------------------+--+-----------+------+
//  0    5  6                     18 19 20       25 26  31

typedef bit_field< 19, 19 > FN_field;
typedef bit_field< 20, 25 > NATIVE_OP_field;
typedef bit_field< 26, 31 > EMUL_OP_field;

// Execute SheepShaver instruction
// Debug: intercept PPC execution at SheepMem addresses
void sheepshaver_cpu::execute_sheep(uint32 opcode)
{
    assert((((opcode >> 26) & 0x3f) == 6) && OP_MAX <= 64 + 3);

    switch (opcode & 0x3f) {
    case 0:     // EMUL_RETURN
        QuitEmulator();
        break;

    case 1:     // EXEC_RETURN
        spcflags().set(SPCFLAG_CPU_EXEC_RETURN);
        break;

    case 2:     // EXEC_NATIVE
        execute_native_op(NATIVE_OP_field::extract(opcode));
        if (FN_field::test(opcode))
            pc() = lr();
        else
            pc() += 4;
        break;

    default:    // EMUL_OP
        execute_emul_op(EMUL_OP_field::extract(opcode) - 3);
        pc() += 4;
        break;
    }
}

// Compile one instruction
#if PPC_ENABLE_JIT
int sheepshaver_cpu::compile1(codegen_context_t & cg_context)
{
    const instr_info_t *ii = cg_context.instr_info;
    if (ii->mnemo != PPC_I(SHEEP))
        return COMPILE_FAILURE;

    int status = COMPILE_FAILURE;
    powerpc_dyngen & dg = cg_context.codegen;
    uint32 opcode = cg_context.opcode;

    switch (opcode & 0x3f) {
    case 0:     // EMUL_RETURN
        dg.gen_invoke(QuitEmulator);
        status = COMPILE_CODE_OK;
        break;

    case 1:     // EXEC_RETURN
        dg.gen_spcflags_set(SPCFLAG_CPU_EXEC_RETURN);
        dg.gen_exec_return();
        status = COMPILE_EPILOGUE_OK;
        break;

    case 2: {   // EXEC_NATIVE
        uint32 selector = NATIVE_OP_field::extract(opcode);
        switch (selector) {
#if !PPC_REENTRANT_JIT
        case NATIVE_PATCH_NAME_REGISTRY:
            dg.gen_invoke(DoPatchNameRegistry);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_VIDEO_INSTALL_ACCEL:
            dg.gen_invoke(VideoInstallAccel);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_VIDEO_VBL:
            dg.gen_invoke(VideoVBL);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_GET_RESOURCE:
        case NATIVE_GET_1_RESOURCE:
        case NATIVE_GET_IND_RESOURCE:
        case NATIVE_GET_1_IND_RESOURCE:
        case NATIVE_R_GET_RESOURCE: {
            static const uint32 get_resource_ptr[] = {
                XLM_GET_RESOURCE,
                XLM_GET_1_RESOURCE,
                XLM_GET_IND_RESOURCE,
                XLM_GET_1_IND_RESOURCE,
                XLM_R_GET_RESOURCE
            };
            uint32 old_get_resource = ReadMacInt32(get_resource_ptr[selector - NATIVE_GET_RESOURCE]);
            typedef void (*func_t)(dyngen_cpu_base, uint32);
            func_t func = &sheepshaver_cpu::call_get_resource;
            dg.gen_invoke_CPU_im(func, old_get_resource);
            status = COMPILE_CODE_OK;
            break;
        }
#endif
        case NATIVE_CHECK_LOAD_INVOC:
            dg.gen_load_T0_GPR(3);
            dg.gen_load_T1_GPR(4);
            dg.gen_se_16_32_T1();
            dg.gen_load_T2_GPR(5);
            dg.gen_invoke_T0_T1_T2((void (*)(uint32, uint32, uint32))check_load_invoc);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NAMED_CHECK_LOAD_INVOC:
            dg.gen_load_T0_GPR(3);
            dg.gen_load_T1_GPR(4);
            dg.gen_load_T2_GPR(5);
            dg.gen_invoke_T0_T1_T2((void (*)(uint32, uint32, uint32))named_check_load_invoc);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NQD_SYNC_HOOK:
            dg.gen_load_T0_GPR(3);
            dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_sync_hook);
            dg.gen_store_T0_GPR(3);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NQD_BITBLT_HOOK:
            dg.gen_load_T0_GPR(3);
            dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_bitblt_hook);
            dg.gen_store_T0_GPR(3);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NQD_FILLRECT_HOOK:
            dg.gen_load_T0_GPR(3);
            dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_fillrect_hook);
            dg.gen_store_T0_GPR(3);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NQD_UNKNOWN_HOOK:
            dg.gen_load_T0_GPR(3);
            dg.gen_invoke_T0_ret_T0((uint32 (*)(uint32))NQD_unknown_hook);
            dg.gen_store_T0_GPR(3);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NQD_BITBLT:
            dg.gen_load_T0_GPR(3);
            dg.gen_invoke_T0((void (*)(uint32))NQD_bitblt);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NQD_INVRECT:
            dg.gen_load_T0_GPR(3);
            dg.gen_invoke_T0((void (*)(uint32))NQD_invrect);
            status = COMPILE_CODE_OK;
            break;
        case NATIVE_NQD_FILLRECT:
            dg.gen_load_T0_GPR(3);
            dg.gen_invoke_T0((void (*)(uint32))NQD_fillrect);
            status = COMPILE_CODE_OK;
            break;
        }
        // Could we fully translate this NativeOp?
        if (status == COMPILE_CODE_OK) {
            if (!FN_field::test(opcode))
                cg_context.done_compile = false;
            else {
                dg.gen_load_T0_LR_aligned();
                dg.gen_set_PC_T0();
                cg_context.done_compile = true;
            }
            break;
        }
#if PPC_REENTRANT_JIT
        // Try to execute NativeOp trampoline
        if (!FN_field::test(opcode))
            dg.gen_set_PC_im(cg_context.pc + 4);
        else {
            dg.gen_load_T0_LR_aligned();
            dg.gen_set_PC_T0();
        }
        dg.gen_mov_32_T0_im(selector);
        dg.gen_jmp(native_op_trampoline);
        cg_context.done_compile = true;
        status = COMPILE_EPILOGUE_OK;
        break;
#else
        // Invoke NativeOp handler
        if (!FN_field::test(opcode)) {
            typedef void (*func_t)(dyngen_cpu_base, uint32);
            func_t func = &sheepshaver_cpu::call_execute_native_op;
            dg.gen_invoke_CPU_im(func, selector);
            cg_context.done_compile = false;
            status = COMPILE_CODE_OK;
        }
        break;
#endif
    }

    default: {  // EMUL_OP
        uint32 emul_op = EMUL_OP_field::extract(opcode) - 3;
#if PPC_REENTRANT_JIT
        dg.gen_set_PC_im(cg_context.pc + 4);
        dg.gen_mov_32_T0_im(emul_op);
        dg.gen_jmp(emul_op_trampoline);
        cg_context.done_compile = true;
        status = COMPILE_EPILOGUE_OK;
        break;
#else
        typedef void (*func_t)(dyngen_cpu_base, uint32);
        func_t func = &sheepshaver_cpu::call_execute_emul_op;
        dg.gen_invoke_CPU_im(func, emul_op);
        cg_context.done_compile = false;
        status = COMPILE_CODE_OK;
        break;
#endif
    }
    }
    return status;
}
#endif

void sheepshaver_cpu::call_execute_emul_op(powerpc_cpu *cpu, uint32 emul_op)
{
    static_cast<sheepshaver_cpu *>(cpu)->execute_emul_op(emul_op);
}

// Execute EMUL_OP routine (clean — matches legacy, no debug logging)
int dt_seq = 0;

void sheepshaver_cpu::execute_emul_op(uint32 emul_op)
{
    if (!g_platform.ppc_emulop_handler) {
        fprintf(stderr, "FATAL: g_platform.ppc_emulop_handler not set\n");
        abort();
    }
    M68kRegisters r68;
    WriteMacInt32(XLM_68K_R25, gpr(25));
    WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);
    for (int i = 0; i < 8; i++)
        r68.d[i] = gpr(8 + i);
    for (int i = 0; i < 7; i++)
        r68.a[i] = gpr(16 + i);
    r68.a[7] = gpr(1);
    uint32 saved_cr = get_cr() & 0xff9fffff;
    uint32 saved_xer = get_xer();
    g_platform.ppc_emulop_handler(&r68, gpr(24), emul_op);
    set_cr(saved_cr);
    set_xer(saved_xer);
    for (int i = 0; i < 8; i++)
        gpr(8 + i) = r68.d[i];
    for (int i = 0; i < 7; i++)
        gpr(16 + i) = r68.a[i];
    gpr(1) = r68.a[7];
    WriteMacInt32(XLM_RUN_MODE, MODE_68K);
}

void sheepshaver_cpu::call_execute_native_op(powerpc_cpu *cpu, uint32 selector)
{
    static_cast<sheepshaver_cpu *>(cpu)->execute_native_op(selector);
}

void sheepshaver_cpu::execute_native_op(uint32 selector)
{
    // Track native op rates per second
    {
        static int nop_counts[40] = {0};
        static uint64 last_dump = 0;
        static int dump_count = 0;
        if (selector < 40) nop_counts[selector]++;
        uint64 now = GetTicks_usec();
        if (last_dump == 0) last_dump = now;
        if (now - last_dump >= 5000000) {  // Every 5s
            ++dump_count;
            if (dump_count <= 40 || dump_count % 10 == 0) {
                fprintf(stderr, "[NOP-RATE] #%d: VBL=%d DIO=%d RES=%d CL=%d NCL=%d MKX=%d NQD=%d\n",
                    dump_count,
                    nop_counts[NATIVE_VIDEO_VBL],
                    nop_counts[NATIVE_VIDEO_DO_DRIVER_IO],
                    nop_counts[NATIVE_GET_RESOURCE] + nop_counts[NATIVE_GET_1_RESOURCE] +
                    nop_counts[NATIVE_GET_IND_RESOURCE] + nop_counts[NATIVE_GET_1_IND_RESOURCE] +
                    nop_counts[NATIVE_R_GET_RESOURCE] + nop_counts[NATIVE_GET_NAMED_RESOURCE] +
                    nop_counts[NATIVE_GET_1_NAMED_RESOURCE],
                    nop_counts[NATIVE_CHECK_LOAD_INVOC],
                    nop_counts[NATIVE_NAMED_CHECK_LOAD_INVOC],
                    nop_counts[NATIVE_MAKE_EXECUTABLE],
                    nop_counts[NATIVE_NQD_SYNC_HOOK] + nop_counts[NATIVE_NQD_BITBLT_HOOK] +
                    nop_counts[NATIVE_NQD_FILLRECT_HOOK] + nop_counts[NATIVE_NQD_BITBLT] +
                    nop_counts[NATIVE_NQD_INVRECT] + nop_counts[NATIVE_NQD_FILLRECT]);
            }
            memset(nop_counts, 0, sizeof(nop_counts));
            last_dump = now;
        }
    }
    switch (selector) {
    case NATIVE_PATCH_NAME_REGISTRY:
        DoPatchNameRegistry();
        break;
    case NATIVE_VIDEO_INSTALL_ACCEL:
        VideoInstallAccel();
        break;
    case NATIVE_VIDEO_VBL:
        VideoVBL();
        break;
    case NATIVE_VIDEO_DO_DRIVER_IO:
        gpr(3) = (int32)(int16)VideoDoDriverIO(gpr(3), gpr(4), gpr(5), gpr(6), gpr(7));
        break;
    case NATIVE_ETHER_AO_GET_HWADDR:
        AO_get_ethernet_address(gpr(3));
        break;
    case NATIVE_ETHER_AO_ADD_MULTI:
        AO_enable_multicast(gpr(3));
        break;
    case NATIVE_ETHER_AO_DEL_MULTI:
        AO_disable_multicast(gpr(3));
        break;
    case NATIVE_ETHER_AO_SEND_PACKET:
        AO_transmit_packet(gpr(3));
        break;
    case NATIVE_ETHER_IRQ:
        EtherIRQ();
        break;
    case NATIVE_ETHER_INIT:
        gpr(3) = InitStreamModule((void *)(uintptr_t)gpr(3));
        break;
    case NATIVE_ETHER_TERM:
        TerminateStreamModule();
        break;
    case NATIVE_ETHER_OPEN:
        gpr(3) = ether_open((queue_t *)(uintptr_t)gpr(3), (void *)(uintptr_t)gpr(4), gpr(5), gpr(6), (void*)(uintptr_t)gpr(7));
        break;
    case NATIVE_ETHER_CLOSE:
        gpr(3) = ether_close((queue_t *)(uintptr_t)gpr(3), gpr(4), (void *)(uintptr_t)gpr(5));
        break;
    case NATIVE_ETHER_WPUT:
        gpr(3) = ether_wput((queue_t *)(uintptr_t)gpr(3), (mblk_t *)(uintptr_t)gpr(4));
        break;
    case NATIVE_ETHER_RSRV:
        gpr(3) = ether_rsrv((queue_t *)(uintptr_t)gpr(3));
        break;
    case NATIVE_NQD_SYNC_HOOK:
        gpr(3) = NQD_sync_hook(gpr(3));
        break;
    case NATIVE_NQD_UNKNOWN_HOOK:
        gpr(3) = NQD_unknown_hook(gpr(3));
        break;
    case NATIVE_NQD_BITBLT_HOOK:
        gpr(3) = NQD_bitblt_hook(gpr(3));
        break;
    case NATIVE_NQD_BITBLT:
        NQD_bitblt(gpr(3));
        break;
    case NATIVE_NQD_FILLRECT_HOOK:
        gpr(3) = NQD_fillrect_hook(gpr(3));
        break;
    case NATIVE_NQD_INVRECT:
        NQD_invrect(gpr(3));
        break;
    case NATIVE_NQD_FILLRECT:
        NQD_fillrect(gpr(3));
        break;
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
        gpr(3) = serial_callbacks[selector - NATIVE_SERIAL_NOTHING](gpr(3), gpr(4));
        break;
    }
    case NATIVE_GET_RESOURCE:
        get_resource(ReadMacInt32(XLM_GET_RESOURCE));
        break;
    case NATIVE_GET_1_RESOURCE:
        get_resource(ReadMacInt32(XLM_GET_1_RESOURCE));
        break;
    case NATIVE_GET_IND_RESOURCE:
        get_resource(ReadMacInt32(XLM_GET_IND_RESOURCE));
        break;
    case NATIVE_GET_1_IND_RESOURCE:
        get_resource(ReadMacInt32(XLM_GET_1_IND_RESOURCE));
        break;
    case NATIVE_R_GET_RESOURCE:
        get_resource(ReadMacInt32(XLM_R_GET_RESOURCE));
        break;
    case NATIVE_MAKE_EXECUTABLE:
        MakeExecutable(0, gpr(4), gpr(5));
        break;
    case NATIVE_CHECK_LOAD_INVOC:
        check_load_invoc(gpr(3), gpr(4), gpr(5));
        break;
    case NATIVE_NAMED_CHECK_LOAD_INVOC:
        named_check_load_invoc(gpr(3), gpr(4), gpr(5));
        break;
    case NATIVE_GET_NAMED_RESOURCE:
        get_resource(ReadMacInt32(XLM_GET_NAMED_RESOURCE));
        break;
    case NATIVE_GET_1_NAMED_RESOURCE:
        get_resource(ReadMacInt32(XLM_GET_1_NAMED_RESOURCE));
        break;
    default:
        printf("FATAL: NATIVE_OP called with bogus selector %d\n", selector);
        QuitEmulator();
        break;
    }
}

// Execute 68k routine
void sheepshaver_cpu::execute_68k(uint32 entry, M68kRegisters *r)
{
    extern uint64_t ppc_insn_counter;
    uint64_t e68k_start = ppc_insn_counter;
#if SAFE_EXEC_68K
    if (ReadMacInt32(XLM_RUN_MODE) != MODE_EMUL_OP)
        printf("FATAL: Execute68k() not called from EMUL_OP mode\n");
#endif

    // Save program counters and branch registers
    uint32 saved_pc = pc();
    uint32 saved_lr = lr();
    uint32 saved_ctr = ctr();
    uint32 saved_cr = get_cr();

    // Create MacOS stack frame
    uint32 sp = gpr(1);
    gpr(1) -= 56;
    WriteMacInt32(gpr(1), sp);

    // Save PowerPC registers
    uint32 saved_GPRs[19];
    memcpy(&saved_GPRs[0], &gpr(13), sizeof(uint32)*(32-13));
#if SAVE_FP_EXEC_68K
    double saved_FPRs[18];
    memcpy(&saved_FPRs[0], &fpr(14), sizeof(double)*(32-14));
#endif

    // Setup registers for 68k emulator
    cr().set(CR_SO_field<2>::mask());            // Supervisor mode
    for (int i = 0; i < 8; i++)                 // d[0]..d[7]
        gpr(8 + i) = r->d[i];
    for (int i = 0; i < 7; i++)                 // a[0]..a[6]
        gpr(16 + i) = r->a[i];
    gpr(23) = 0;
    gpr(24) = entry;
    gpr(25) = ReadMacInt32(XLM_68K_R25);        // MSB of SR
    gpr(26) = 0;
    gpr(28) = 0;                                // VBR
    gpr(29) = ReadMacInt32(KERNEL_DATA_BASE + 0x1074);  // Pointer to opcode table
    gpr(30) = ReadMacInt32(KERNEL_DATA_BASE + 0x1078);  // Address of emulator
    gpr(31) = KernelDataAddr + 0x1000;

    // Push return address (points to EXEC_RETURN opcode) on stack
    gpr(1) -= 4;
    WriteMacInt32(gpr(1), XLM_EXEC_RETURN_OPCODE);

    // Reentering 68k emulator
    WriteMacInt32(XLM_RUN_MODE, MODE_68K);

    // Set r0 to 0 for 68k emulator
    gpr(0) = 0;

    // Execute 68k opcode
    uint32 opcode = ReadMacInt16(gpr(24));
    gpr(27) = (int32)(int16)ReadMacInt16(gpr(24) += 2);
    gpr(29) += opcode * 8;
    execute(gpr(29));

    // Save r25 (contains current 68k interrupt level)
    WriteMacInt32(XLM_68K_R25, gpr(25));

    // Reentering EMUL_OP mode
    WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);

    // Save 68k registers
    for (int i = 0; i < 8; i++)                 // d[0]..d[7]
        r->d[i] = gpr(8 + i);
    for (int i = 0; i < 7; i++)                 // a[0]..a[6]
        r->a[i] = gpr(16 + i);

    // Restore PowerPC registers
    memcpy(&gpr(13), &saved_GPRs[0], sizeof(uint32)*(32-13));
#if SAVE_FP_EXEC_68K
    memcpy(&fpr(14), &saved_FPRs[0], sizeof(double)*(32-14));
#endif

    // Cleanup stack
    gpr(1) += 56;

    // Restore program counters and branch registers
    pc() = saved_pc;
    lr() = saved_lr;
    ctr() = saved_ctr;
    set_cr(saved_cr);

}

void sheepshaver_cpu::execute_ppc(uint32 entry)
{
    uint32 saved_lr = lr();
    SheepVar32 trampoline = POWERPC_EXEC_RETURN;
    WriteMacInt32(trampoline.addr(), POWERPC_EXEC_RETURN);
    lr() = trampoline.addr();
    execute(entry);
    lr() = saved_lr;
}

uint32 sheepshaver_cpu::execute_macos_code(uint32 tvect, int nargs, uint32 const *args)
{
    uint32 saved_pc = pc();
    uint32 saved_lr = lr();
    uint32 saved_ctr = ctr();

    SheepVar32 trampoline = POWERPC_EXEC_RETURN;
    lr() = trampoline.addr();

    gpr(1) -= 64;
    uint32 proc = ReadMacInt32(tvect);
    uint32 toc = ReadMacInt32(tvect + 4);

    uint32 regs[8];
    regs[0] = gpr(2);
    for (int i = 0; i < nargs; i++)
        regs[i + 1] = gpr(i + 3);

    gpr(2) = toc;
    for (int i = 0; i < nargs; i++)
        gpr(i + 3) = args[i];
    execute(proc);
    uint32 retval = gpr(3);

    for (int i = 0; i <= nargs; i++)
        gpr(i + 2) = regs[i];

    gpr(1) += 64;
    pc() = saved_pc;
    lr() = saved_lr;
    ctr() = saved_ctr;

    return retval;
}

void sheepshaver_cpu::interrupt(uint32 entry)
{
    uint32 saved_pc = pc();
    uint32 saved_lr = lr();
    uint32 saved_ctr = ctr();
    uint32 saved_sp = gpr(1);

    gpr(1) = SignalStackBase() - 64;

    SheepVar32 trampoline = POWERPC_EXEC_RETURN;

    WriteMacInt32(KERNEL_DATA_BASE + 0x004, gpr(1));
    WriteMacInt32(KERNEL_DATA_BASE + 0x018, gpr(6));

    gpr(6) = ReadMacInt32(KERNEL_DATA_BASE + 0x65c);
    assert(gpr(6) != 0);
    WriteMacInt32(gpr(6) + 0x13c, gpr(7));
    WriteMacInt32(gpr(6) + 0x144, gpr(8));
    WriteMacInt32(gpr(6) + 0x14c, gpr(9));
    WriteMacInt32(gpr(6) + 0x154, gpr(10));
    WriteMacInt32(gpr(6) + 0x15c, gpr(11));
    WriteMacInt32(gpr(6) + 0x164, gpr(12));
    WriteMacInt32(gpr(6) + 0x16c, gpr(13));

    gpr(1)  = KernelDataAddr;
    gpr(7)  = ReadMacInt32(KERNEL_DATA_BASE + 0x660);
    gpr(8)  = 0;
    gpr(10) = trampoline.addr();
    gpr(12) = trampoline.addr();
    gpr(13) = get_cr();

    uint32 result = op_ppc_rlwimi::apply(gpr(7), 8, 0x80000000, gpr(7));
    record_cr0(result);
    gpr(7) = result;

    gpr(11) = 0xf072;
    cr().set((gpr(11) & 0x0fff0000) | (get_cr() & ~0x0fff0000));

    execute(entry);

    pc() = saved_pc;
    lr() = saved_lr;
    ctr() = saved_ctr;
    gpr(1) = saved_sp;
}

void sheepshaver_cpu::call_get_resource(powerpc_cpu *cpu, uint32 old_get_resource)
{
    static_cast<sheepshaver_cpu *>(cpu)->get_resource(old_get_resource);
}

void sheepshaver_cpu::get_resource(uint32 old_get_resource)
{
    uint32 type = gpr(3);
    int16 id = gpr(4);

    gpr(1) -= 56;
    execute_ppc(old_get_resource);

    uint32 handle = gpr(3);
    check_load_invoc(type, id, handle);
    gpr(3) = handle;

    gpr(1) += 56;
}


// ============================================================================
// HandleInterrupt (called from KPX spcflags check)
// ============================================================================

static uint32_t kpx_cpu_get_pc(void);  // forward decl for stall logging

void HandleInterrupt(powerpc_registers *r)
{
    static int hi_count = 0;
    static int hi_mode1_count = 0;  // mode=1 (MODE_NATIVE) in last 5s window
    static int hi_skip_count = 0;
    static uint64 hi_last_report = 0;
    static int hi_peak_mode1 = 0;
    hi_count++;

    // Do nothing if interrupts are disabled
    if (int32(ReadMacInt32(XLM_IRQ_NEST)) > 0) {
        hi_skip_count++;
        return;
    }

    uint32 mode = ReadMacInt32(XLM_RUN_MODE);
    if (mode == MODE_NATIVE) hi_mode1_count++;

    (void)mode;

    // 5-second rate reporting
    static int hi_window_count = 0;
    hi_window_count++;
    uint64 now = GetTicks_usec();
    if (hi_last_report == 0) hi_last_report = now;
    if (now - hi_last_report >= 5000000) {
        int rate = hi_window_count * 1000000ULL / (now - hi_last_report);
        if (hi_mode1_count > hi_peak_mode1) hi_peak_mode1 = hi_mode1_count;
        fprintf(stderr, "[TRACE] HI-RATE: %d HI/s (%d mode=1, peak=%d)\n",
                rate, hi_mode1_count, hi_peak_mode1);
        hi_mode1_count = 0;
        hi_window_count = 0;
        hi_last_report = now;
    }

    // Interrupt action depends on current run mode
    switch (mode) {
    case MODE_68K:
        // 68k emulator active, trigger 68k interrupt level 1
        WriteMacInt16(ReadMacInt32(KERNEL_DATA_BASE + 0x67c), 1);
        r->cr.set(r->cr.get() | ReadMacInt32(KERNEL_DATA_BASE + 0x674));
        break;

#if INTERRUPTS_IN_NATIVE_MODE
    case MODE_NATIVE:
        // 68k emulator inactive, in nanokernel?
        if (r->gpr[1] != KernelDataAddr) {
            // Prepare for 68k interrupt level 1
            WriteMacInt16(ReadMacInt32(KERNEL_DATA_BASE + 0x67c), 1);
            WriteMacInt32(ReadMacInt32(KERNEL_DATA_BASE + 0x658) + 0xdc,
                          ReadMacInt32(ReadMacInt32(KERNEL_DATA_BASE + 0x658) + 0xdc)
                          | ReadMacInt32(KERNEL_DATA_BASE + 0x674));

            // Execute nanokernel interrupt routine
            DisableInterrupt();
            if (ROMType == ROMTYPE_NEWWORLD)
                ppc_cpu->interrupt(ROMBase + 0x312b1c);
            else
                ppc_cpu->interrupt(ROMBase + 0x312a3c);
        }
        break;
#endif

#if INTERRUPTS_IN_EMUL_OP_MODE
    case MODE_EMUL_OP:
        // 68k emulator active, within EMUL_OP routine
        if ((ReadMacInt32(XLM_68K_R25) & 7) == 0) {
            M68kRegisters r;
            uint32 old_r25 = ReadMacInt32(XLM_68K_R25);
            WriteMacInt32(XLM_68K_R25, 0x21);
            static const uint8 proc_template[] = {
                0x3f, 0x3c, 0x00, 0x00,
                0x48, 0x7a, 0x00, 0x0a,
                0x40, 0xe7,
                0x20, 0x78, 0x00, 0x064,
                0x4e, 0xd0,
                M68K_RTS >> 8, M68K_RTS & 0xff
            };
            BUILD_SHEEPSHAVER_PROCEDURE(proc);
            Execute68k(proc, &r);
            WriteMacInt32(XLM_68K_R25, old_r25);
        }
        break;
#endif
    }
}


// ============================================================================
// Forward declaration
static uint32_t kpx_cpu_get_pc(void);

// ============================================================================
// TriggerInterrupt (called from timer/driver threads)
// Flight recorder control (called from emul_op_ppc.cpp)
void kpx_flight_recorder_cmd(int cmd)
{
    if (!ppc_cpu) return;
    if (cmd == 1) ppc_cpu->start_log();
    else if (cmd == 2) ppc_cpu->dump_log("/tmp/kpx_flight.log");
}

// ============================================================================
// call_macos functions (PPC calling conventions)
// ============================================================================

uint32 call_macos(uint32 tvect) { return ppc_cpu->execute_macos_code(tvect, 0, NULL); }
uint32 call_macos1(uint32 tvect, uint32 a1) { const uint32 args[] = {a1}; return ppc_cpu->execute_macos_code(tvect, 1, args); }
uint32 call_macos2(uint32 tvect, uint32 a1, uint32 a2) { const uint32 args[] = {a1,a2}; return ppc_cpu->execute_macos_code(tvect, 2, args); }
uint32 call_macos3(uint32 tvect, uint32 a1, uint32 a2, uint32 a3) { const uint32 args[] = {a1,a2,a3}; return ppc_cpu->execute_macos_code(tvect, 3, args); }
uint32 call_macos4(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4) { const uint32 args[] = {a1,a2,a3,a4}; return ppc_cpu->execute_macos_code(tvect, 4, args); }
uint32 call_macos5(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4, uint32 a5) { const uint32 args[] = {a1,a2,a3,a4,a5}; return ppc_cpu->execute_macos_code(tvect, 5, args); }
uint32 call_macos6(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4, uint32 a5, uint32 a6) { const uint32 args[] = {a1,a2,a3,a4,a5,a6}; return ppc_cpu->execute_macos_code(tvect, 6, args); }
uint32 call_macos7(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4, uint32 a5, uint32 a6, uint32 a7) { const uint32 args[] = {a1,a2,a3,a4,a5,a6,a7}; return ppc_cpu->execute_macos_code(tvect, 7, args); }

#if PPC_ENABLE_JIT && PPC_REENTRANT_JIT
// Initialize EmulOp trampolines
void init_emul_op_trampolines(basic_dyngen & dg)
{
    typedef void (*func_t)(dyngen_cpu_base, uint32);
    func_t func;

    // EmulOp
    emul_op_trampoline = dg.gen_start();
    func = &sheepshaver_cpu::call_execute_emul_op;
    dg.gen_invoke_CPU_T0(func);
    dg.gen_exec_return();
    dg.gen_end();

    // NativeOp
    native_op_trampoline = dg.gen_start();
    func = &sheepshaver_cpu::call_execute_native_op;
    dg.gen_invoke_CPU_T0(func);
    dg.gen_exec_return();
    dg.gen_end();

    D(bug("EmulOp trampoline:   %p\n", emul_op_trampoline));
    D(bug("NativeOp trampoline: %p\n", native_op_trampoline));
}
#endif

// ============================================================================
// Platform API wrapper functions
// ============================================================================

static bool kpx_cpu_init(void)
{
    // vm_init() now called from cpu_context.cpp before SheepMem::Init,
    // matching legacy's allocation order.

    // DISABLED: virtual clock breaks PRECISE_TIMING_POSIX timer thread.
    // Legacy uses real wall clock (clock_gettime CLOCK_REALTIME) for all
    // timer_current_time() calls. The virtual clock caused clock_nanosleep
    // (wall clock) to conflict with timer_current_time (virtual clock),
    // preventing Time Manager tasks from firing.
    // extern bool g_use_ppc_virtual_clock;
    // g_use_ppc_virtual_clock = true;

    // Create CPU instance (deferred from cpu_ppc_kpx_install so the parent
    // subprocess doesn't allocate JIT/decode caches it never uses)
    if (!ppc_cpu) {
        ppc_cpu = new sheepshaver_cpu();
        extern Platform g_platform;
        if (g_platform.ppc_jit) {
            ppc_cpu->enable_jit();
            fprintf(stderr, "[KPX] JIT enabled\n");
        } else {
            fprintf(stderr, "[KPX] JIT disabled (interpreter mode)\n");
        }
    }

    // Get pointer to KernelData in host address space
    kernel_data = (KernelData *)Mac2HostAddr(KERNEL_DATA_BASE);

    // Set up initial GPR state for ROM entry (matches legacy init_emul_ppc)
    ppc_cpu->set_register(powerpc_registers::GPR(3), any_register((uint32)ROMBase + 0x30d000));
    ppc_cpu->set_register(powerpc_registers::GPR(4), any_register(KernelDataAddr + 0x1000));
    WriteMacInt32(XLM_RUN_MODE, MODE_68K);

    return true;
}

static void kpx_cpu_reset(void)
{
    // No-op for now
}

static void kpx_cpu_set_type(int cpu_type, int fpu_type)
{
    // PPC doesn't use M68K cpu_type - ignore
    (void)cpu_type;
    (void)fpu_type;
}

static int kpx_cpu_execute_one(void)
{
    // Not supported for PPC - use cpu_execute_fast
    return 1;
}

// 60Hz tick thread (matches SheepShaver tick_func)
// Fires TriggerInterrupt() at 60Hz to drive Mac OS boot and VBL
static std::atomic<bool> tick_thread_running{false};
// Tick function — copied from SheepShaver main_unix.cpp tick_func()
// Tick thread — matches legacy SheepShaver tick_func structure
static void tick_thread_func() {
    int tick_counter = 0;
    uint64 start = GetTicks_usec();
    int64 ticks = 0;
    uint64 next = start;
    extern bool tick_inhibit;

    while (tick_thread_running) {
        int period = 16625;  // 60Hz
        next += period;
        int64 delay = next - GetTicks_usec();
        if (delay > 0) {
            struct timespec ts = {0, (long)(delay * 1000)};
            nanosleep(&ts, nullptr);
        } else if (delay < -period) {
            next = GetTicks_usec();
        }
        if (tick_inhibit) continue;
        ticks++;

        // Pseudo Mac 1Hz interrupt, update local time
        if (++tick_counter > 60) {
            tick_counter = 0;
            WriteMacInt32(0x20c, (uint32)time(NULL) + 0x7C25B080);
        }

        // Trigger 60Hz interrupt
        if (ReadMacInt32(XLM_IRQ_NEST) == 0) {
            SetInterruptFlag(INTFLAG_VIA);
            TriggerInterrupt();
        }

        // Capture frame for WebRTC/screenshot pipeline (mac-phoenix addition)
        if (g_platform.video_refresh)
            g_platform.video_refresh();

        // Export cursor, app name, and Mac state to IPC SHM for parent's web API
        boot_progress_export_cursor_to_ipc();
        boot_progress_export_app_to_ipc();
        boot_progress_export_mac_state();
    }
}

// Forward declarations for SIGSEGV handler (defined later in this file)
static uint32_t kpx_cpu_get_pc(void);
static uint32_t kpx_cpu_get_gpr(int n);

// ============================================================================
// SIGSEGV handler (from SheepShaver sheepshaver_glue.cpp)
// ============================================================================

static sigsegv_return_t kpx_sigsegv_handler(sigsegv_info_t *sip)
{
    const uintptr addr = (uintptr)sigsegv_get_fault_address(sip);
    const uintptr host_pc = (uintptr)sigsegv_get_fault_instruction_address(sip);

    // Ignore writes to ROM
    if ((addr - (uintptr)ROMBaseHost) < ROM_SIZE)
        return SIGSEGV_RETURN_SKIP_INSTRUCTION;

    // Get program counter of target CPU (use public accessor)
    const uint32 pc = ppc_cpu ? kpx_cpu_get_pc() : 0;

    // Fault in Mac ROM or RAM?
    bool mac_fault = (pc >= ROMBase && pc < (ROMBase + ROM_AREA_SIZE))
                  || (pc >= RAMBase && pc < (RAMBase + RAMSize))
                  || (pc >= DR_CACHE_BASE && pc < (DR_CACHE_BASE + DR_CACHE_SIZE));
    if (mac_fault) {
        // "VM settings" during MacOS 8 installation
        if (pc == ROMBase + 0x488160 && kpx_cpu_get_gpr(20) == 0xf8000000)
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;

        // MacOS 8.5 installation
        else if (pc == ROMBase + 0x488140 && kpx_cpu_get_gpr(16) == 0xf8000000)
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;

        // MacOS 8 serial drivers on startup
        else if (pc == ROMBase + 0x48e080 && (kpx_cpu_get_gpr(8) == 0xf3012002 || kpx_cpu_get_gpr(8) == 0xf3012000))
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;

        // MacOS 8.1 serial drivers on startup
        else if (pc == ROMBase + 0x48c5e0 && (kpx_cpu_get_gpr(20) == 0xf3012002 || kpx_cpu_get_gpr(20) == 0xf3012000))
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;
        else if (pc == ROMBase + 0x4a10a0 && (kpx_cpu_get_gpr(20) == 0xf3012002 || kpx_cpu_get_gpr(20) == 0xf3012000))
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;

        // MacOS 8.6 serial drivers on startup (with DR Cache and OldWorld ROM)
        else if ((pc - DR_CACHE_BASE) < DR_CACHE_SIZE && (kpx_cpu_get_gpr(16) == 0xf3012002 || kpx_cpu_get_gpr(16) == 0xf3012000))
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;
        else if ((pc - DR_CACHE_BASE) < DR_CACHE_SIZE && (kpx_cpu_get_gpr(20) == 0xf3012002 || kpx_cpu_get_gpr(20) == 0xf3012000))
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;

        // Ignore writes to the zero page
        else if ((uint32)(addr - SheepMem::ZeroPage()) < (uint32)SheepMem::PageSize())
            return SIGSEGV_RETURN_SKIP_INSTRUCTION;

        // Ignore all other faults in Mac code (matches SheepShaver ignoresegv=true)
        return SIGSEGV_RETURN_SKIP_INSTRUCTION;
    }

    fprintf(stderr, "SIGSEGV\n");
    fprintf(stderr, "  pc %p\n", (void *)(uintptr_t)kpx_cpu_get_pc());
    fprintf(stderr, "  ea %p\n", (void *)addr);

    return SIGSEGV_RETURN_FAILURE;
}

// Install KPX SIGSEGV handler (called from cpu_ppc_kpx.cpp and cpu_process.cpp)
extern "C" void kpx_install_sigsegv_handler(void)
{
    sigsegv_install_handler(kpx_sigsegv_handler);
}

static void kpx_cpu_execute_fast(void)
{
    if (!ppc_cpu) return;
    kpx_stop_requested = false;

    // Install PPC-aware SIGSEGV handler (replaces the blind skip-all from main.cpp)
    kpx_install_sigsegv_handler();

    // Signal masking removed to match legacy SheepShaver — legacy has no
    // SIGRTMIN+6/7 masking on the CPU thread.

    // EXPERIMENT: Guard at 0x40000000 DISABLED to match legacy memory layout.
    // Legacy has no guards at all. The nanokernel probes 0x40000000 during init;
    // with the guard mapped as PROT_NONE, probes get SIGSEGV→skip (same as unmapped).
    // Without the guard, probes might find thread stacks (mapped RW) and take a
    // different init path.
    void *guard40 = MAP_FAILED; // disabled
    // Unmap DR Emulator and DR Cache BEFORE nanokernel boot.
    // The nanokernel probes 0x68070000 and 0x69000000 during init — if these
    // are mapped (returning zeros), it takes a different init path than legacy
    // SheepShaver where they're unmapped (SIGSEGV → skip → different register state).
    // They get remapped after the nanokernel init completes (first HandleInterrupt).
    {
        munmap((void *)0x68070000, 0x10000);
        munmap((void *)0x69000000, 0x80000);
    }

    // Entry point: ROMBase + 0x310000 (nanokernel boot)
    uint32 entry = ROMBase + 0x310000;

    // Initialize WebRTC video pipeline
    {
        extern void video_ppc_init_webrtc_null(void);
        video_ppc_init_webrtc_null();
    }

    // Inhibit ticks during nanokernel init — OP_RESET clears tick_inhibit.
    {
        extern bool tick_inhibit;
        tick_inhibit = true;
    }

    // Start 60Hz tick thread
    tick_thread_running = true;
    std::thread ticker(tick_thread_func);

    ppc_cpu->execute(entry);

    tick_thread_running = false;
    if (ticker.joinable()) ticker.join();
}

static void kpx_cpu_request_stop(void)
{
    kpx_stop_requested = true;
    if (ppc_cpu)
        ppc_cpu->request_stop();
}

static uint32_t kpx_cpu_get_pc(void)
{
    if (!ppc_cpu) return 0;
    return ppc_cpu->get_register(powerpc_registers::PC).i;
}

static uint16_t kpx_cpu_get_sr(void)
{
    return 0;  // PPC has no SR; return 0
}

static uint32_t kpx_cpu_get_dreg(int n)
{
    // PPC has no D registers; map to GPR for compatibility
    if (ppc_cpu && n >= 0 && n < 32)
        return ppc_cpu->gpr(n);
    return 0;
}

static uint32_t kpx_cpu_get_areg(int n)
{
    return 0;  // PPC has no A registers
}

// PPC cursor update via CursorDeviceDispatch (matches legacy SheepShaver POWERPC_ROM path)
static void kpx_ppc_cursor_move(uint32_t mouse_base, int x, int y)
{
    static const uint8 proc_template[] = {
        0x2f, 0x08,     // move.l a0,-(sp)
        0x2f, 0x00,     // move.l d0,-(sp)
        0x2f, 0x01,     // move.l d1,-(sp)
        0x70, 0x01,     // moveq #1,d0 (MoveTo)
        0xaa, 0xdb,     // CursorDeviceDispatch
        M68K_RTS >> 8, M68K_RTS & 0xff
    };
    BUILD_SHEEPSHAVER_PROCEDURE(proc);
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.a[0] = ReadMacInt32(mouse_base + 4);
    r.d[0] = x;
    r.d[1] = y;
    Execute68k(proc, &r);
}

static void kpx_cpu_trigger_interrupt(int level)
{
    (void)level;
    idle_resume();
    if (ppc_cpu)
        ppc_cpu->trigger_interrupt();
}

static void kpx_cpu_execute_68k_trap(uint16_t trap, struct M68kRegisters *r)
{
    // Direct implementation — must NOT call Execute68kTrap() which dispatches
    // back through g_platform, causing infinite recursion.
    SheepVar proc_var(4);
    uint32 proc = proc_var.addr();
    WriteMacInt16(proc, trap);
    WriteMacInt16(proc + 2, M68K_RTS);
    ppc_cpu->execute_68k(proc, r);
}

static void kpx_cpu_execute_68k(uint32_t addr, struct M68kRegisters *r)
{
    // Direct implementation — must NOT call Execute68k() which dispatches
    // back through g_platform, causing infinite recursion.
    ppc_cpu->execute_68k(addr, r);
}

static void kpx_flush_code_cache(void)
{
    if (ppc_cpu)
        ppc_cpu->invalidate_cache();
}

// Memory access via KPX vm.hpp (big-endian)
static uint8_t kpx_mem_read_byte(uint32_t addr)  { return vm_read_memory_1(addr); }
static uint16_t kpx_mem_read_word(uint32_t addr)  { return vm_read_memory_2(addr); }
static uint32_t kpx_mem_read_long(uint32_t addr)  { return vm_read_memory_4(addr); }
static void kpx_mem_write_byte(uint32_t addr, uint8_t val)  { vm_write_memory_1(addr, val); }
static void kpx_mem_write_word(uint32_t addr, uint16_t val) { vm_write_memory_2(addr, val); }
static void kpx_mem_write_long(uint32_t addr, uint32_t val) { vm_write_memory_4(addr, val); }

static uint8_t *kpx_mem_mac_to_host(uint32_t addr)
{
    return vm_do_get_real_address(addr);
}

static uint32_t kpx_mem_host_to_mac(uint8_t *ptr)
{
    return vm_do_get_virtual_address(ptr);
}

// PPC-specific accessors
static uint32_t kpx_cpu_get_gpr(int n)
{
    if (ppc_cpu && n >= 0 && n < 32)
        return ppc_cpu->gpr(n);
    return 0;
}

static void kpx_cpu_set_gpr(int n, uint32_t val)
{
    if (ppc_cpu && n >= 0 && n < 32)
        ppc_cpu->gpr(n) = val;
}

static uint32_t kpx_cpu_get_cr(void)
{
    return ppc_cpu ? ppc_cpu->get_cr() : 0;
}

static uint32_t kpx_cpu_get_lr(void)
{
    return ppc_cpu ? ppc_cpu->get_register(powerpc_registers::LR).i : 0;
}

static uint32_t kpx_cpu_get_ctr(void)
{
    return ppc_cpu ? ppc_cpu->get_register(powerpc_registers::CTR).i : 0;
}

static void kpx_cpu_execute_ppc(uint32_t entry)
{
    if (ppc_cpu)
        ppc_cpu->execute(entry);
}


// ============================================================================
// Flight recorder dump (called from SIGSEGV handler)
// ============================================================================

extern "C" void ppc_dump_flight_recorder(const char *filename)
{
    if (ppc_cpu) {
        ppc_cpu->stop_log();
        ppc_cpu->dump_log(filename);
    }
}

// PPC EmulOp platform handler — delegates to ppc::EmulOp
static void kpx_ppc_emulop_handler(void *r68k_regs, uint32_t pc, int selector)
{
    ppc::EmulOp(static_cast<M68kRegisters *>(r68k_regs), pc, selector);
}

// Convert common M68K_EMUL_OP_* values (0x71xx from common/include/emul_op.h)
// to KPX encoding (M68K_EMUL_BREAK + OP_*, 0xFExx).
// Common 0x71xx opcodes are valid m68k (moveq #xx, d0) — the DR emulator
// executes them silently instead of trapping to the emulop handler.
static uint16_t kpx_make_emulop(uint16_t op)
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

// ============================================================================
// cpu_ppc_kpx_install - Wire KPX into Platform API
// ============================================================================

extern "C" void cpu_ppc_kpx_install(Platform *p)
{
    // Don't create CPU here — defer to kpx_cpu_init() which only runs
    // in the child subprocess. The parent only needs the function pointers.

    // Backend identification
    p->cpu_name = "KPX";
    p->use_aline_emulops = false;  // PPC uses POWERPC_EMUL_OP (0x18xxxxxx), not A-line

    // Lifecycle
    p->cpu_init = kpx_cpu_init;
    p->cpu_reset = kpx_cpu_reset;
    p->cpu_set_type = kpx_cpu_set_type;

    // Execution
    p->cpu_execute_one = kpx_cpu_execute_one;
    p->cpu_execute_fast = kpx_cpu_execute_fast;
    p->cpu_request_stop = kpx_cpu_request_stop;

    // State query (generic M68K-compatible slots)
    p->cpu_get_pc = kpx_cpu_get_pc;
    p->cpu_get_sr = kpx_cpu_get_sr;
    p->cpu_get_dreg = kpx_cpu_get_dreg;
    p->cpu_get_areg = kpx_cpu_get_areg;

    // Interrupts
    p->cpu_trigger_interrupt = kpx_cpu_trigger_interrupt;

    // 68k execution (from PPC context)
    p->cpu_execute_68k_trap = kpx_cpu_execute_68k_trap;
    p->cpu_execute_68k = kpx_cpu_execute_68k;

    // Post-startup patching (NQD accel + ExtFS)
    p->patch_after_startup = []() { ppc::PatchAfterStartup_PPC(); };

    // Code cache
    p->flush_code_cache = kpx_flush_code_cache;

    // Memory
    p->mem_read_byte = kpx_mem_read_byte;
    p->mem_read_word = kpx_mem_read_word;
    p->mem_read_long = kpx_mem_read_long;
    p->mem_write_byte = kpx_mem_write_byte;
    p->mem_write_word = kpx_mem_write_word;
    p->mem_write_long = kpx_mem_write_long;
    p->mem_mac_to_host = kpx_mem_mac_to_host;
    p->mem_host_to_mac = kpx_mem_host_to_mac;

    // PPC-specific accessors
    p->cpu_get_gpr = kpx_cpu_get_gpr;
    p->cpu_set_gpr = kpx_cpu_set_gpr;
    p->cpu_get_cr = kpx_cpu_get_cr;
    p->cpu_get_lr = kpx_cpu_get_lr;
    p->cpu_get_ctr = kpx_cpu_get_ctr;
    p->cpu_execute_ppc = kpx_cpu_execute_ppc;

    // EmulOp encoding: convert common 0x71xx values to KPX 0xFExx encoding
    // Core code (extfs.cpp, rsrc_patches.cpp) uses common/include/emul_op.h values,
    // but the PPC DR emulator needs 0xFExx (M68K_EMUL_BREAK+OP_*) to trap properly.
    // Without this, 0x71xx is executed as 'moveq #xx, d0' — a valid m68k no-op.
    p->make_emulop = kpx_make_emulop;

    // EmulOp/trap handlers
    p->m68k_emulop_handler = nullptr;  // PPC doesn't use m68k EmulOp traps
    p->ppc_emulop_handler = kpx_ppc_emulop_handler;
    p->trap_handler = nullptr;

    // PPC cursor (CursorDeviceDispatch via Execute68k + SheepMem)
    p->ppc_cursor_move = kpx_ppc_cursor_move;

    // Video refresh — set by mode-specific init (IPC subprocess or headless).

}
