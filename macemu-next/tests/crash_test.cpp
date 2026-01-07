/*
 *  crash_test.cpp - Test crash handler
 *
 *  Tests that the crash handler properly catches segfaults
 *  and prints a stack trace.
 */

#include <stdio.h>
#include "../src/common/include/crash_handler_init.h"

// Function that will cause a segfault
void crash_function() {
	printf("About to trigger a segfault...\n");
	int *ptr = nullptr;
	*ptr = 42;  // BOOM!
}

// Another function for stack trace depth
void intermediate_function() {
	crash_function();
}

int main() {
	printf("=== Crash Handler Test ===\n\n");

	// Install crash handlers
	install_crash_handlers();
	printf("Crash handlers installed\n\n");

	// Trigger a crash
	intermediate_function();

	return 0;
}
