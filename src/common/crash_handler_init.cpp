/*
 *  crash_handler_init.cpp - Install crash handlers for mac-phoenix
 *
 *  Provides crash reporting with backtrace and register dumps.
 */

#include "include/crash_handler.h"
#include "include/crash_handler_init.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ucontext.h>
#include <cstring>
#include <cstdint>

// Weak externs from the Unicorn PPC backend (cpu_unicorn_ppc.cpp). The
// handler uses them to report which guest PPC PCs were executing just
// before a QEMU-internal abort (e.g. get_page_addr_code_hostp abort()).
// Declared weak so non-Unicorn-PPC builds still link.
extern "C" {
	extern volatile uint32_t g_uppc_last_block_pc __attribute__((weak));
	extern uint32_t g_uppc_last_block_pcs[32] __attribute__((weak));
	extern volatile int g_uppc_last_block_pcs_idx __attribute__((weak));
	extern volatile uint64_t g_uppc_block_seq __attribute__((weak));
}

static void print_uppc_last_pcs(void)
{
	if (&g_uppc_block_seq == nullptr) return;
	if (g_uppc_block_seq == 0) return;
	fprintf(stderr, "=== Unicorn PPC last guest PCs ===\n");
	fprintf(stderr, "Latest block PC: 0x%08x (block seq %llu)\n",
	        (uint32_t)g_uppc_last_block_pc,
	        (unsigned long long)g_uppc_block_seq);
	fprintf(stderr, "Last 32 block entries (oldest -> newest):\n");
	int idx = g_uppc_last_block_pcs_idx;
	for (int k = 0; k < 32; k++) {
		int j = (idx + k) & 31;
		fprintf(stderr, "  [%2d] 0x%08x\n", k, g_uppc_last_block_pcs[j]);
	}
	fprintf(stderr, "==================================\n\n");
}

// Crash signal handler
static void crash_signal_handler(int sig, siginfo_t *info, void *context)
{
	auto *uctx = static_cast<ucontext_t *>(context);

	// Print crash header
	print_crash_header(sig, info, "mac-phoenix");

	// Print register state (x86-64/i386 only)
	print_register_state(uctx);

	// Print backtrace
	print_backtrace("CRASH");

	// Print last known guest PCs if the Unicorn PPC backend is active.
	print_uppc_last_pcs();

	// Print helpful message
	fprintf(stderr, "=== CRASH INFORMATION ===\n");
	fprintf(stderr, "Please report this crash with the above information at:\n");
	fprintf(stderr, "  https://github.com/YOUR_USERNAME/mac-phoenix/issues\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "To get more detailed backtrace with line numbers, rebuild with:\n");
	fprintf(stderr, "  meson configure build -Dbuildtype=debug\n");
	fprintf(stderr, "  ninja -C build\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Or run with gdb to get full debugging info:\n");
	fprintf(stderr, "  gdb --args ./build/mac-phoenix <rom>\n");
	fprintf(stderr, "  (gdb) run\n");
	fprintf(stderr, "  (gdb) bt full\n");
	fprintf(stderr, "=========================\n\n");

	// Restore default handler and re-raise to generate core dump
	signal(sig, SIG_DFL);
	raise(sig);
}

// Install crash handlers for all fatal signals
extern "C" void install_crash_handlers(void)
{
	struct sigaction sa;
	sa.sa_sigaction = crash_signal_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);

	// Install handlers for various crash signals
	sigaction(SIGSEGV, &sa, nullptr);  // Segmentation fault
	sigaction(SIGBUS, &sa, nullptr);   // Bus error
	sigaction(SIGABRT, &sa, nullptr);  // Abort
	sigaction(SIGILL, &sa, nullptr);   // Illegal instruction
	sigaction(SIGFPE, &sa, nullptr);   // Floating point exception

	fprintf(stderr, "[CrashHandler] Installed signal handlers (SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE)\n");
}
