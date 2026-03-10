/*
 *  platform.h - Platform adapter layer
 *
 *  Function pointer table for runtime driver selection.
 *  All pointers initialized to null drivers (safe no-ops).
 *
 *  No changes to core BasiliskII code needed - adapters defer to these pointers.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct M68kRegisters;

/*
 *  Platform Structure - All driver function pointers
 */
typedef struct {
    /*
     *  SCSI Driver
     */
    void (*scsi_init)(void);
    void (*scsi_exit)(void);
    void (*scsi_set_cmd)(int cmd_length, uint8_t *cmd);
    bool (*scsi_is_target_present)(int id);
    bool (*scsi_set_target)(int id, int lun);
    bool (*scsi_send_cmd)(size_t data_length, bool reading, int sg_index,
                          uint8_t **sg_ptr, uint32_t *sg_len, uint16_t *stat, uint32_t timeout);

    /*
     *  Video Driver
     */
    bool (*video_init)(bool classic);
    void (*video_exit)(void);
    void (*video_refresh)(void);

    /*
     *  Disk Driver
     *  Note: No disk_init/disk_exit pointers - disk.cpp provides DiskInit/Exit directly
     *  and calls Sys_* functions for file I/O
     */

    /*
     *  Audio Driver
     */
    void (*audio_init)(void);
    void (*audio_exit)(void);

    /*
     *  Serial Driver
     */
    void (*serial_init)(void);
    void (*serial_exit)(void);

    /*
     *  Ethernet Driver
     */
    bool (*ether_init)(void);
    void (*ether_exit)(void);
    void (*ether_reset)(void);
    int16_t (*ether_add_multicast)(uint32_t pb);
    int16_t (*ether_del_multicast)(uint32_t pb);
    int16_t (*ether_attach_ph)(uint16_t type, uint32_t handler);
    int16_t (*ether_detach_ph)(uint16_t type);
    int16_t (*ether_write)(uint32_t wds);
    bool (*ether_start_udp_thread)(int socket_fd);
    void (*ether_stop_udp_thread)(void);
    void (*ether_interrupt)(void);

    /*
     *  Host Platform - File/Disk System
     */
    void (*mount_volume)(const char *path);
    void (*file_disk_layout)(int64_t size, int64_t *start, int64_t *length);
    void (*floppy_init)(void);
    void (*sys_add_serial_prefs)(void);
    void (*sys_add_floppy_prefs)(void);
    void (*sys_add_disk_prefs)(void);
    void (*sys_add_cdrom_prefs)(void);

    // File operations
    void* (*sys_open)(const char *path, bool read_only, bool no_cache);
    void (*sys_close)(void *fh);
    size_t (*sys_read)(void *fh, void *buf, int64_t offset, size_t length);
    size_t (*sys_write)(void *fh, void *buf, int64_t offset, size_t length);
    bool (*sys_is_readonly)(void *fh);
    bool (*sys_is_disk_inserted)(void *fh);
    bool (*sys_is_fixed_disk)(void *fh);
    int64_t (*sys_get_file_size)(void *fh);
    void (*sys_eject)(void *fh);
    void (*sys_allow_removal)(void *fh);
    void (*sys_prevent_removal)(void *fh);
    bool (*sys_format)(void *fh);

    // CD-ROM operations
    bool (*sys_cd_get_volume)(void *fh, uint8_t *left, uint8_t *right);
    bool (*sys_cd_set_volume)(void *fh, uint8_t left, uint8_t right);
    void (*sys_cd_pause)(void *fh);
    void (*sys_cd_resume)(void *fh);
    bool (*sys_cd_play)(void *fh, uint8_t m1, uint8_t s1, uint8_t f1, uint8_t m2, uint8_t s2, uint8_t f2);
    bool (*sys_cd_stop)(void *fh, uint8_t m, uint8_t s, uint8_t f);
    bool (*sys_cd_get_position)(void *fh, uint8_t *pos);
    bool (*sys_cd_scan)(void *fh, uint8_t m, uint8_t s, uint8_t f, bool reverse);
    bool (*sys_cd_read_toc)(void *fh, uint8_t *toc);

    /*
     *  CPU Emulation Backend
     */
    const char *cpu_name;  // Backend name: "UAE", "Unicorn", "DualCPU"
    bool use_aline_emulops;  // true: EmulOps encoded as 0xAExx (Unicorn); false: 0x71xx (UAE)

    // Lifecycle
    bool (*cpu_init)(void);
    void (*cpu_reset)(void);

    // Configuration (must be called before cpu_init)
    void (*cpu_set_type)(int cpu_type, int fpu_type);  // cpu_type: 2=68020, 3=68030, 4=68040

    // Execution - returns: 0=ok, 1=stopped, 2=breakpoint, 3=exception, 4=emulop, 5=divergence
    int (*cpu_execute_one)(void);
    void (*cpu_execute_fast)(void);  // Optional: run until stopped (NULL if not supported)
    void (*cpu_request_stop)(void);  // Signal CPU to break out of execute_fast (thread-safe)

    // State query
    uint32_t (*cpu_get_pc)(void);
    uint16_t (*cpu_get_sr)(void);
    uint32_t (*cpu_get_dreg)(int n);
    uint32_t (*cpu_get_areg)(int n);

    // Interrupts
    void (*cpu_trigger_interrupt)(int level);

    // 68k Trap Execution (for ROM patches and drivers)
    // Executes a 68k trap with given register state, returns updated registers
    // This allows ROM patches to call Mac OS traps without depending on specific CPU backend
    void (*cpu_execute_68k_trap)(uint16_t trap, struct M68kRegisters *r);

    // 68k Subroutine Execution (for timer callbacks, ADB handlers, etc.)
    // Executes 68k code at given address, returns updated registers
    void (*cpu_execute_68k)(uint32_t addr, struct M68kRegisters *r);

    // Code cache flush (for JIT backends like Unicorn)
    // Called when code is patched at runtime (system patches, CheckLoad, etc.)
    // JIT backends must invalidate cached translations for the affected region.
    void (*flush_code_cache)(void);

    /*
     *  Memory System API (backend-independent)
     *
     *  These functions provide memory access for initialization and ROM patching.
     *  Different backends implement these differently:
     *    - UAE: Uses memory banking system (get_long/put_long with byte-swapping)
     *    - Unicorn: Direct memory access to ROMBaseHost/RAMBaseHost
     *    - DualCPU: Uses UAE's implementation
     *
     *  All functions read/write in big-endian (M68K native) format.
     */
    uint8_t (*mem_read_byte)(uint32_t addr);
    uint16_t (*mem_read_word)(uint32_t addr);
    uint32_t (*mem_read_long)(uint32_t addr);
    void (*mem_write_byte)(uint32_t addr, uint8_t val);
    void (*mem_write_word)(uint32_t addr, uint16_t val);
    void (*mem_write_long)(uint32_t addr, uint32_t val);

    // Address translation (Mac address <-> Host pointer)
    uint8_t* (*mem_mac_to_host)(uint32_t addr);
    uint32_t (*mem_host_to_mac)(uint8_t *ptr);

    /*
     *  CPU Special Instruction Handlers
     *
     *  EmulOps (0x71xx) and Traps (A-line/F-line) need special handling in dual-CPU mode.
     *  The handler is called by both CPUs, with is_primary indicating which CPU is calling.
     *
     *  is_primary=true:  This CPU should execute the operation and sync to the other CPU
     *  is_primary=false: This CPU should skip execution (state will be synced from primary)
     *
     *  Return value:
     *    true  = Handler executed and advanced PC (caller should not advance PC)
     *    false = Handler skipped execution (caller should advance PC)
     */

    // EmulOp handler (0x71xx illegal instructions used for emulator functions)
    // Returns true if PC was advanced, false if caller should advance
    bool (*emulop_handler)(uint16_t opcode, bool is_primary);

    // Trap handler (A-line and F-line exceptions)
    // Returns true if PC was advanced, false if caller should advance
    bool (*trap_handler)(int vector, uint16_t opcode, bool is_primary);
} Platform;

/*
 *  Global platform instance
 */
extern Platform g_platform;

/*
 *  Convert EmulOp to correct encoding for active backend.
 *  UAE uses 0x71xx (overloaded MOVEQ); Unicorn uses 0xAExx (A-line trap).
 */
static inline uint16_t platform_make_emulop(uint16_t emulop) {
    if (g_platform.use_aline_emulops && (emulop & 0xFF00) == 0x7100) {
        return 0xAE00 | (emulop & 0x3F);
    }
    return emulop;
}

/*
 *  Platform initialization
 *  Sets all function pointers to null drivers (safe defaults)
 */
void platform_init(void);

/*
 *  Null driver function declarations
 *  These are the default implementations (no-ops)
 */

// SCSI null driver
extern void scsi_null_init(void);
extern void scsi_null_exit(void);
extern void scsi_null_set_cmd(int cmd_length, uint8_t *cmd);
extern bool scsi_null_is_target_present(int id);
extern bool scsi_null_set_target(int id, int lun);
extern bool scsi_null_send_cmd(size_t data_length, bool reading, int sg_index,
                                uint8_t **sg_ptr, uint32_t *sg_len, uint16_t *stat, uint32_t timeout);

// Video null driver
extern bool video_null_init(bool classic);
extern void video_null_exit(void);
extern void video_null_refresh(void);

// Disk null driver - none needed (disk.cpp provides DiskInit/Exit)

// Audio null driver
extern void audio_null_init(void);
extern void audio_null_exit(void);

// Serial null driver
extern void serial_null_init(void);
extern void serial_null_exit(void);

// Ether null driver
extern bool ether_null_init(void);
extern void ether_null_exit(void);
extern void ether_null_reset(void);
extern int16_t ether_null_add_multicast(uint32_t pb);
extern int16_t ether_null_del_multicast(uint32_t pb);
extern int16_t ether_null_attach_ph(uint16_t type, uint32_t handler);
extern int16_t ether_null_detach_ph(uint16_t type);
extern int16_t ether_null_write(uint32_t wds);
extern bool ether_null_start_udp_thread(int socket_fd);
extern void ether_null_stop_udp_thread(void);
extern void ether_null_interrupt(void);

// Platform/Sys null driver
extern void platform_null_mount_volume(const char *path);
extern void platform_null_file_disk_layout(int64_t size, int64_t *start, int64_t *length);
extern void platform_null_floppy_init(void);
extern void platform_null_sys_add_serial_prefs(void);
extern void platform_null_sys_add_floppy_prefs(void);
extern void platform_null_sys_add_disk_prefs(void);
extern void platform_null_sys_add_cdrom_prefs(void);
extern void *platform_null_sys_open(const char *path, bool read_only, bool no_cache);
extern void platform_null_sys_close(void *fh);
extern size_t platform_null_sys_read(void *fh, void *buf, int64_t offset, size_t length);
extern size_t platform_null_sys_write(void *fh, void *buf, int64_t offset, size_t length);
extern bool platform_null_sys_is_readonly(void *fh);
extern bool platform_null_sys_is_disk_inserted(void *fh);
extern bool platform_null_sys_is_fixed_disk(void *fh);
extern int64_t platform_null_sys_get_file_size(void *fh);
extern void platform_null_sys_eject(void *fh);
extern void platform_null_sys_allow_removal(void *fh);
extern void platform_null_sys_prevent_removal(void *fh);
extern bool platform_null_sys_format(void *fh);
extern bool platform_null_sys_cd_get_volume(void *fh, uint8_t *left, uint8_t *right);
extern bool platform_null_sys_cd_set_volume(void *fh, uint8_t left, uint8_t right);
extern void platform_null_sys_cd_pause(void *fh);
extern void platform_null_sys_cd_resume(void *fh);
extern bool platform_null_sys_cd_play(void *fh, uint8_t m1, uint8_t s1, uint8_t f1, uint8_t m2, uint8_t s2, uint8_t f2);
extern bool platform_null_sys_cd_stop(void *fh, uint8_t m, uint8_t s, uint8_t f);
extern bool platform_null_sys_cd_get_position(void *fh, uint8_t *pos);
extern bool platform_null_sys_cd_scan(void *fh, uint8_t m, uint8_t s, uint8_t f, bool reverse);
extern bool platform_null_sys_cd_read_toc(void *fh, uint8_t *toc);

// Platform/Sys Unix driver
extern void platform_unix_mount_volume(const char *path);
extern void platform_unix_file_disk_layout(int64_t size, int64_t *start, int64_t *length);
extern void platform_unix_floppy_init(void);
extern void platform_unix_sys_add_serial_prefs(void);
extern void platform_unix_sys_add_floppy_prefs(void);
extern void platform_unix_sys_add_disk_prefs(void);
extern void platform_unix_sys_add_cdrom_prefs(void);
extern void *platform_unix_sys_open(const char *path, bool read_only, bool no_cache);
extern void platform_unix_sys_close(void *fh);
extern size_t platform_unix_sys_read(void *fh, void *buf, int64_t offset, size_t length);
extern size_t platform_unix_sys_write(void *fh, void *buf, int64_t offset, size_t length);
extern bool platform_unix_sys_is_readonly(void *fh);
extern bool platform_unix_sys_is_disk_inserted(void *fh);
extern bool platform_unix_sys_is_fixed_disk(void *fh);
extern int64_t platform_unix_sys_get_file_size(void *fh);
extern void platform_unix_sys_eject(void *fh);
extern void platform_unix_sys_allow_removal(void *fh);
extern void platform_unix_sys_prevent_removal(void *fh);
extern bool platform_unix_sys_format(void *fh);
extern bool platform_unix_sys_cd_get_volume(void *fh, uint8_t *left, uint8_t *right);
extern bool platform_unix_sys_cd_set_volume(void *fh, uint8_t left, uint8_t right);
extern void platform_unix_sys_cd_pause(void *fh);
extern void platform_unix_sys_cd_resume(void *fh);
extern bool platform_unix_sys_cd_play(void *fh, uint8_t m1, uint8_t s1, uint8_t f1, uint8_t m2, uint8_t s2, uint8_t f2);
extern bool platform_unix_sys_cd_stop(void *fh, uint8_t m, uint8_t s, uint8_t f);
extern bool platform_unix_sys_cd_get_position(void *fh, uint8_t *pos);
extern bool platform_unix_sys_cd_scan(void *fh, uint8_t m, uint8_t s, uint8_t f, bool reverse);
extern bool platform_unix_sys_cd_read_toc(void *fh, uint8_t *toc);

/*
 *  CPU Backend Installation Functions
 */
extern void cpu_uae_install(Platform *p);
extern void cpu_unicorn_install(Platform *p);
extern void cpu_dualcpu_install(Platform *p);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
