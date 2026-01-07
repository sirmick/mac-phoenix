/*
 *  crash_handler_init.cpp - Install crash handlers for macemu-next
 *
 *  Provides crash reporting with backtrace and register dumps.
 */

#include "include/crash_handler.h"
#include "include/crash_handler_init.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>

// Crash signal handler
static void crash_signal_handler(int sig, siginfo_t *info, void *context)
{
	ucontext_t *uctx = (ucontext_t *)context;

	// Print crash header
	print_crash_header(sig, info, "macemu-next");

	// Print register state (x86-64/i386 only)
	print_register_state(uctx);

	// Print backtrace
	print_backtrace("CRASH");

	// Print helpful message
	fprintf(stderr, "=== CRASH INFORMATION ===\n");
	fprintf(stderr, "Please report this crash with the above information at:\n");
	fprintf(stderr, "  https://github.com/YOUR_USERNAME/macemu-next/issues\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "To get more detailed backtrace with line numbers, rebuild with:\n");
	fprintf(stderr, "  meson configure build -Dbuildtype=debug\n");
	fprintf(stderr, "  ninja -C build\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Or run with gdb to get full debugging info:\n");
	fprintf(stderr, "  gdb --args ./build/macemu-next <rom>\n");
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
	sigaction(SIGSEGV, &sa, NULL);  // Segmentation fault
	sigaction(SIGBUS, &sa, NULL);   // Bus error
	sigaction(SIGABRT, &sa, NULL);  // Abort
	sigaction(SIGILL, &sa, NULL);   // Illegal instruction
	sigaction(SIGFPE, &sa, NULL);   // Floating point exception

	fprintf(stderr, "[CrashHandler] Installed signal handlers (SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE)\n");
}
