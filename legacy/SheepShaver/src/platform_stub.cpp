/*
 *  platform_stub.cpp - Platform API stub for standalone legacy SheepShaver
 *
 *  Provides g_platform global and all null/unix driver stubs.
 *  Nothing routes through g_platform yet — legacy code still calls
 *  drivers directly. This exists to test whether the Platform API's
 *  presence alone affects PPC boot timing.
 */

#include "platform.h"
#include <cstdio>
#include <cstring>

// ============================================================================
// Global platform instance
// ============================================================================

extern "C" {
Platform g_platform;
}

// ============================================================================
// Null driver stubs (all no-ops)
// ============================================================================

extern "C" {

// SCSI null driver
void scsi_null_init(void) {}
void scsi_null_exit(void) {}
void scsi_null_set_cmd(int, uint8_t *) {}
bool scsi_null_is_target_present(int) { return false; }
bool scsi_null_set_target(int, int) { return false; }
bool scsi_null_send_cmd(size_t, bool, int, uint8_t **, uint32_t *, uint16_t *, uint32_t) { return false; }

// Video null driver
bool video_null_init(bool) { return true; }
void video_null_exit(void) {}
void video_null_refresh(void) {}

// Audio null driver
void audio_null_init(void) {}
void audio_null_exit(void) {}

// Serial null driver
void serial_null_init(void) {}
void serial_null_exit(void) {}

// Ether null driver
bool ether_null_init(void) { return false; }
void ether_null_exit(void) {}
void ether_null_reset(void) {}
int16_t ether_null_add_multicast(uint32_t) { return -1; }
int16_t ether_null_del_multicast(uint32_t) { return -1; }
int16_t ether_null_attach_ph(uint16_t, uint32_t) { return -1; }
int16_t ether_null_detach_ph(uint16_t) { return -1; }
int16_t ether_null_write(uint32_t) { return -1; }
bool ether_null_start_udp_thread(int) { return false; }
void ether_null_stop_udp_thread(void) {}
void ether_null_interrupt(void) {}

// Platform/Sys null driver
void platform_null_mount_volume(const char *) {}
void platform_null_file_disk_layout(int64_t, int64_t *start, int64_t *length) { *start = 0; *length = 0; }
void platform_null_floppy_init(void) {}
void platform_null_sys_add_serial_prefs(void) {}
void platform_null_sys_add_floppy_prefs(void) {}
void platform_null_sys_add_disk_prefs(void) {}
void platform_null_sys_add_cdrom_prefs(void) {}
void *platform_null_sys_open(const char *, bool, bool) { return nullptr; }
void platform_null_sys_close(void *) {}
size_t platform_null_sys_read(void *, void *, int64_t, size_t) { return 0; }
size_t platform_null_sys_write(void *, void *, int64_t, size_t) { return 0; }
bool platform_null_sys_is_readonly(void *) { return true; }
bool platform_null_sys_is_disk_inserted(void *) { return false; }
bool platform_null_sys_is_fixed_disk(void *) { return false; }
int64_t platform_null_sys_get_file_size(void *) { return 0; }
void platform_null_sys_eject(void *) {}
void platform_null_sys_allow_removal(void *) {}
void platform_null_sys_prevent_removal(void *) {}
bool platform_null_sys_format(void *) { return false; }
bool platform_null_sys_cd_get_volume(void *, uint8_t *, uint8_t *) { return false; }
bool platform_null_sys_cd_set_volume(void *, uint8_t, uint8_t) { return false; }
void platform_null_sys_cd_pause(void *) {}
void platform_null_sys_cd_resume(void *) {}
bool platform_null_sys_cd_play(void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) { return false; }
bool platform_null_sys_cd_stop(void *, uint8_t, uint8_t, uint8_t) { return false; }
bool platform_null_sys_cd_get_position(void *, uint8_t *) { return false; }
bool platform_null_sys_cd_scan(void *, uint8_t, uint8_t, uint8_t, bool) { return false; }
bool platform_null_sys_cd_read_toc(void *, uint8_t *) { return false; }

// Platform/Sys Unix driver stubs (not used — legacy SS calls sys_unix.cpp directly)
void platform_unix_mount_volume(const char *) {}
void platform_unix_file_disk_layout(int64_t, int64_t *s, int64_t *l) { *s = 0; *l = 0; }
void platform_unix_floppy_init(void) {}
void platform_unix_sys_add_serial_prefs(void) {}
void platform_unix_sys_add_floppy_prefs(void) {}
void platform_unix_sys_add_disk_prefs(void) {}
void platform_unix_sys_add_cdrom_prefs(void) {}
void *platform_unix_sys_open(const char *, bool, bool) { return nullptr; }
void platform_unix_sys_close(void *) {}
size_t platform_unix_sys_read(void *, void *, int64_t, size_t) { return 0; }
size_t platform_unix_sys_write(void *, void *, int64_t, size_t) { return 0; }
bool platform_unix_sys_is_readonly(void *) { return true; }
bool platform_unix_sys_is_disk_inserted(void *) { return false; }
bool platform_unix_sys_is_fixed_disk(void *) { return false; }
int64_t platform_unix_sys_get_file_size(void *) { return 0; }
void platform_unix_sys_eject(void *) {}
void platform_unix_sys_allow_removal(void *) {}
void platform_unix_sys_prevent_removal(void *) {}
bool platform_unix_sys_format(void *) { return false; }
bool platform_unix_sys_cd_get_volume(void *, uint8_t *, uint8_t *) { return false; }
bool platform_unix_sys_cd_set_volume(void *, uint8_t, uint8_t) { return false; }
void platform_unix_sys_cd_pause(void *) {}
void platform_unix_sys_cd_resume(void *) {}
bool platform_unix_sys_cd_play(void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) { return false; }
bool platform_unix_sys_cd_stop(void *, uint8_t, uint8_t, uint8_t) { return false; }
bool platform_unix_sys_cd_get_position(void *, uint8_t *) { return false; }
bool platform_unix_sys_cd_scan(void *, uint8_t, uint8_t, uint8_t, bool) { return false; }
bool platform_unix_sys_cd_read_toc(void *, uint8_t *) { return false; }

// CPU backend install stubs (not used — legacy SS doesn't wire through g_platform)
void cpu_uae_install(Platform *) {}
void cpu_unicorn_install(Platform *) {}
void cpu_dualcpu_install(Platform *) {}
void cpu_ppc_kpx_install(Platform *) {}

} // extern "C"

// ============================================================================
// platform_init — matches mac-phoenix's platform.cpp
// ============================================================================

void platform_init(void)
{
    memset(&g_platform, 0, sizeof(g_platform));

    // SCSI
    g_platform.scsi_init = scsi_null_init;
    g_platform.scsi_exit = scsi_null_exit;
    g_platform.scsi_set_cmd = scsi_null_set_cmd;
    g_platform.scsi_is_target_present = scsi_null_is_target_present;
    g_platform.scsi_set_target = scsi_null_set_target;
    g_platform.scsi_send_cmd = scsi_null_send_cmd;

    // Video
    g_platform.video_init = video_null_init;
    g_platform.video_exit = video_null_exit;
    g_platform.video_refresh = video_null_refresh;

    // Audio
    g_platform.audio_init = audio_null_init;
    g_platform.audio_exit = audio_null_exit;

    // Serial
    g_platform.serial_init = serial_null_init;
    g_platform.serial_exit = serial_null_exit;

    // Ether
    g_platform.ether_init = ether_null_init;
    g_platform.ether_exit = ether_null_exit;
    g_platform.ether_reset = ether_null_reset;
    g_platform.ether_add_multicast = ether_null_add_multicast;
    g_platform.ether_del_multicast = ether_null_del_multicast;
    g_platform.ether_attach_ph = ether_null_attach_ph;
    g_platform.ether_detach_ph = ether_null_detach_ph;
    g_platform.ether_write = ether_null_write;
    g_platform.ether_start_udp_thread = ether_null_start_udp_thread;
    g_platform.ether_stop_udp_thread = ether_null_stop_udp_thread;
    g_platform.ether_interrupt = ether_null_interrupt;

    // Platform/Sys — null for now (legacy SS calls sys_unix directly)
    g_platform.mount_volume = platform_null_mount_volume;
    g_platform.file_disk_layout = platform_null_file_disk_layout;
    g_platform.sys_open = platform_null_sys_open;
    g_platform.sys_close = platform_null_sys_close;
    g_platform.sys_read = platform_null_sys_read;
    g_platform.sys_write = platform_null_sys_write;
    g_platform.sys_is_readonly = platform_null_sys_is_readonly;
    g_platform.sys_is_disk_inserted = platform_null_sys_is_disk_inserted;
    g_platform.sys_is_fixed_disk = platform_null_sys_is_fixed_disk;
    g_platform.sys_get_file_size = platform_null_sys_get_file_size;
    g_platform.sys_eject = platform_null_sys_eject;
    g_platform.sys_allow_removal = platform_null_sys_allow_removal;
    g_platform.sys_prevent_removal = platform_null_sys_prevent_removal;
    g_platform.sys_format = platform_null_sys_format;
}
