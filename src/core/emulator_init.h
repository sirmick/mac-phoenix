/*
 *  emulator_init.h - Emulator initialization functions
 *
 *  Provides deferred initialization support for MacPhoenix.
 *  Allows ROM loading and CPU initialization to be called on-demand
 *  from either main() at startup OR from API handler at runtime.
 */

#ifndef EMULATOR_INIT_H
#define EMULATOR_INIT_H

#include <stdint.h>
#include <stdbool.h>

// Global state flag - tracks if emulator has been initialized
extern bool g_emulator_initialized;

// Load ROM file into memory
// Args:
//   rom_path: Path to ROM file
//   rom_base_out: Output pointer to ROM base address (ROMBaseHost)
//   rom_size_out: Output ROM size in bytes
// Returns: true on success, false on error
bool load_rom_file(const char* rom_path,
                   uint8_t** rom_base_out,
                   uint32_t* rom_size_out);

// Initialize CPU subsystem (m68k only for now)
// Must be called after ROM is loaded.
// Args:
//   cpu_backend: "uae", "unicorn", or "dualcpu"
// Returns: true on success, false on error
bool init_cpu_subsystem(const char* cpu_backend);

// Initialize Mac subsystems (XPRAM, drivers, audio, video, etc.)
// Must be called after CPU is initialized.
// This function is called by both main() and init_emulator_from_config()
// to avoid code duplication.
// Returns: true on success, false on error
bool init_mac_subsystems(void);

// Full emulator initialization (ROM + CPU + devices)
// Can be called from main() at startup OR from API handler.
// Thread-safe (uses g_emulator_initialized flag).
// Args:
//   emulator_type: "m68k" or "ppc" (currently only m68k supported)
//   storage_dir: Base directory for storage (config, ROM, disk images)
//   rom_filename: ROM filename (relative to storage_dir/roms or absolute path)
// Returns: true on success, false on error
bool init_emulator_from_config(const char* emulator_type,
                                const char* storage_dir,
                                const char* rom_filename);

#endif // EMULATOR_INIT_H
