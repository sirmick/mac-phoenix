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
