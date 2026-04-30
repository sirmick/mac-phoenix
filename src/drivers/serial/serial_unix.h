/*
 *  serial_unix.h — Unix serial backend (PTY + real /dev/tty*).
 *
 *  Wired via g_platform.serial_init / serial_exit (see common/platform.cpp).
 *  Reads device strings from g_emu_config.serial_a / serial_b at init time.
 *
 *  Per-port string interpretation:
 *    ""              — port disabled; XSERDPort with empty name still
 *                      constructed so the_serd_port[i] is non-null, but
 *                      open() returns openErr (today's m68k behavior).
 *    "pty"           — allocate a fresh pseudo-terminal pair on open();
 *                      slave path is printed to stderr so the user can
 *                      `screen /dev/pts/N`.
 *    anything else   — treated as a device path: opened directly via
 *                      ::open() at open() time, termios applied if tty.
 */

#ifndef SERIAL_UNIX_H
#define SERIAL_UNIX_H

// Idempotent — safe to call multiple times.
void serial_unix_init(void);
void serial_unix_exit(void);

#endif // SERIAL_UNIX_H
