/*
 *  crash_handler.h - Signal handlers for crash reporting with stack traces
 *
 *  Basilisk II (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

#include <signal.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ucontext.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Print a backtrace to stderr using execinfo
 */
static inline void print_backtrace(const char *prefix)
{
	void *array[64];
	size_t size;
	char **strings;

	size = backtrace(array, 64);
	strings = backtrace_symbols(array, size);

	fprintf(stderr, "\n=== %s BACKTRACE (%zu frames) ===\n", prefix, size);

	if (strings != NULL) {
		for (size_t i = 0; i < size; i++) {
			fprintf(stderr, "  [%2zu] %s\n", i, strings[i]);
		}
		fprintf(stderr, "=== END BACKTRACE ===\n\n");
		fflush(stderr);

		// Now try to get file:line info using addr2line
		fprintf(stderr, "=== DETAILED BACKTRACE ===\n");
		for (size_t i = 0; i < size; i++) {
			// Parse backtrace_symbols output: "path(+offset) [address]"
			char exe_buf[512];
			strncpy(exe_buf, strings[i], sizeof(exe_buf) - 1);
			exe_buf[sizeof(exe_buf) - 1] = '\0';

			char *paren = strchr(exe_buf, '(');

			if (paren) {
				*paren = '\0';  // Null-terminate at '(' to get exe path

				// Extract offset from (+0xOFFSET) if present
				char *offset_str = paren + 1;
				char *plus = strchr(offset_str, '+');
				char *close_paren = strchr(offset_str, ')');

				// Try method 1: Use offset from backtrace_symbols
				if (plus && close_paren) {
					*close_paren = '\0';
					char cmd[1024];
					snprintf(cmd, sizeof(cmd), "addr2line -e %s -f -C -i %s 2>/dev/null",
					         exe_buf, plus + 1);

					FILE *fp = popen(cmd, "r");
					if (fp) {
						char func_line[256];
						char file_line[256];
						bool got_info = false;

						// addr2line with -f outputs: function\nfile:line
						if (fgets(func_line, sizeof(func_line), fp)) {
							func_line[strcspn(func_line, "\n")] = 0;
							if (fgets(file_line, sizeof(file_line), fp)) {
								file_line[strcspn(file_line, "\n")] = 0;
								// Only print if we got useful info (not "??:0")
								if (strstr(file_line, "??:0") == NULL && strstr(file_line, "??:?") == NULL) {
									fprintf(stderr, "  [%2zu] %s at %s\n", i, func_line, file_line);
									got_info = true;
								}
							}
						}
						pclose(fp);

						if (got_info) continue;
					}
				}

				// Method 2: Try using raw address (fallback)
				char cmd2[1024];
				snprintf(cmd2, sizeof(cmd2), "addr2line -e %s -f -C -i %p 2>/dev/null",
				         exe_buf, array[i]);

				FILE *fp2 = popen(cmd2, "r");
				if (fp2) {
					char func_line[256];
					char file_line[256];

					if (fgets(func_line, sizeof(func_line), fp2)) {
						func_line[strcspn(func_line, "\n")] = 0;
						if (fgets(file_line, sizeof(file_line), fp2)) {
							file_line[strcspn(file_line, "\n")] = 0;
							if (strstr(file_line, "??:0") == NULL && strstr(file_line, "??:?") == NULL) {
								fprintf(stderr, "  [%2zu] %s at %s\n", i, func_line, file_line);
							}
						}
					}
					pclose(fp2);
				}
			}
		}
		fprintf(stderr, "=== END DETAILED BACKTRACE ===\n\n");
		fflush(stderr);
		free(strings);
	} else {
		// Fallback: just print addresses
		fprintf(stderr, "  (symbols unavailable, printing addresses)\n");
		backtrace_symbols_fd(array, size, STDERR_FILENO);
		fprintf(stderr, "=== END BACKTRACE ===\n\n");
	}
}

/*
 *  Print register state from ucontext (x86-64 specific)
 */
static inline void print_register_state(ucontext_t *uctx)
{
#if defined(__x86_64__) || defined(__i386__)
	if (uctx == NULL) {
		fprintf(stderr, "Register state: unavailable (NULL context)\n");
		return;
	}

	fprintf(stderr, "=== REGISTER STATE ===\n");

#ifdef __x86_64__
	mcontext_t *mctx = &uctx->uc_mcontext;
	fprintf(stderr, "  RIP: 0x%016llx\n", (unsigned long long)mctx->gregs[REG_RIP]);
	fprintf(stderr, "  RSP: 0x%016llx\n", (unsigned long long)mctx->gregs[REG_RSP]);
	fprintf(stderr, "  RBP: 0x%016llx\n", (unsigned long long)mctx->gregs[REG_RBP]);
	fprintf(stderr, "  RAX: 0x%016llx  RBX: 0x%016llx\n",
		(unsigned long long)mctx->gregs[REG_RAX],
		(unsigned long long)mctx->gregs[REG_RBX]);
	fprintf(stderr, "  RCX: 0x%016llx  RDX: 0x%016llx\n",
		(unsigned long long)mctx->gregs[REG_RCX],
		(unsigned long long)mctx->gregs[REG_RDX]);
	fprintf(stderr, "  RSI: 0x%016llx  RDI: 0x%016llx\n",
		(unsigned long long)mctx->gregs[REG_RSI],
		(unsigned long long)mctx->gregs[REG_RDI]);
	fprintf(stderr, "  R8:  0x%016llx  R9:  0x%016llx\n",
		(unsigned long long)mctx->gregs[REG_R8],
		(unsigned long long)mctx->gregs[REG_R9]);
	fprintf(stderr, "  R10: 0x%016llx  R11: 0x%016llx\n",
		(unsigned long long)mctx->gregs[REG_R10],
		(unsigned long long)mctx->gregs[REG_R11]);
	fprintf(stderr, "  R12: 0x%016llx  R13: 0x%016llx\n",
		(unsigned long long)mctx->gregs[REG_R12],
		(unsigned long long)mctx->gregs[REG_R13]);
	fprintf(stderr, "  R14: 0x%016llx  R15: 0x%016llx\n",
		(unsigned long long)mctx->gregs[REG_R14],
		(unsigned long long)mctx->gregs[REG_R15]);
	fprintf(stderr, "  EFL: 0x%016llx\n", (unsigned long long)mctx->gregs[REG_EFL]);
#elif defined(__i386__)
	mcontext_t *mctx = &uctx->uc_mcontext;
	fprintf(stderr, "  EIP: 0x%08x\n", (unsigned int)mctx->gregs[REG_EIP]);
	fprintf(stderr, "  ESP: 0x%08x\n", (unsigned int)mctx->gregs[REG_ESP]);
	fprintf(stderr, "  EBP: 0x%08x\n", (unsigned int)mctx->gregs[REG_EBP]);
	fprintf(stderr, "  EAX: 0x%08x  EBX: 0x%08x\n",
		(unsigned int)mctx->gregs[REG_EAX],
		(unsigned int)mctx->gregs[REG_EBX]);
	fprintf(stderr, "  ECX: 0x%08x  EDX: 0x%08x\n",
		(unsigned int)mctx->gregs[REG_ECX],
		(unsigned int)mctx->gregs[REG_EDX]);
	fprintf(stderr, "  ESI: 0x%08x  EDI: 0x%08x\n",
		(unsigned int)mctx->gregs[REG_ESI],
		(unsigned int)mctx->gregs[REG_EDI]);
	fprintf(stderr, "  EFL: 0x%08x\n", (unsigned int)mctx->gregs[REG_EFL]);
#endif

	fprintf(stderr, "=== END REGISTER STATE ===\n\n");
#else
	fprintf(stderr, "Register state: unavailable (not x86/x86-64)\n");
#endif
}

/*
 *  Get signal fault address (for SIGSEGV/SIGBUS)
 */
static inline void* get_fault_address(siginfo_t *info)
{
	if (info && (info->si_signo == SIGSEGV || info->si_signo == SIGBUS)) {
		return info->si_addr;
	}
	return NULL;
}

/*
 *  Get signal name
 */
static inline const char* get_signal_name(int sig)
{
	switch (sig) {
		case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
		case SIGBUS:  return "SIGBUS (Bus Error)";
		case SIGABRT: return "SIGABRT (Abort)";
		case SIGILL:  return "SIGILL (Illegal Instruction)";
		case SIGFPE:  return "SIGFPE (Floating Point Exception)";
		case SIGTRAP: return "SIGTRAP (Trace/Breakpoint Trap)";
		default:      return "UNKNOWN SIGNAL";
	}
}

/*
 *  Print crash header with signal information
 */
static inline void print_crash_header(int sig, siginfo_t *info, const char *component)
{
	fprintf(stderr, "\n");
	fprintf(stderr, "╔════════════════════════════════════════════════════════════════╗\n");
	fprintf(stderr, "║                    FATAL CRASH DETECTED                        ║\n");
	fprintf(stderr, "╚════════════════════════════════════════════════════════════════╝\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Component: %s\n", component);
	fprintf(stderr, "Signal:    %d (%s)\n", sig, get_signal_name(sig));

	if (info) {
		fprintf(stderr, "Code:      %d\n", info->si_code);

		void *fault_addr = get_fault_address(info);
		if (fault_addr) {
			fprintf(stderr, "Address:   %p (invalid memory access)\n", fault_addr);
		}
	}

	fprintf(stderr, "\n");
}

#ifdef __cplusplus
}
#endif

#endif /* CRASH_HANDLER_H */
