/*
 *  platform.cpp - Platform initialization
 *
 *  Sets up global platform with null driver defaults.
 */

#include "platform.h"

/*
 *  Global platform instance
 */
extern "C" {
Platform g_platform;
}

/*
 *  Initialize platform with null drivers (safe defaults)
 */
void platform_init(void)
{
	// SCSI - null driver
	g_platform.scsi_init = scsi_null_init;
	g_platform.scsi_exit = scsi_null_exit;
	g_platform.scsi_set_cmd = scsi_null_set_cmd;
	g_platform.scsi_is_target_present = scsi_null_is_target_present;
	g_platform.scsi_set_target = scsi_null_set_target;
	g_platform.scsi_send_cmd = scsi_null_send_cmd;

	// Video - null driver
	g_platform.video_init = video_null_init;
	g_platform.video_exit = video_null_exit;
	g_platform.video_refresh = video_null_refresh;

	// Disk - no driver pointers (disk.cpp provides DiskInit/Exit directly)

	// Audio - null driver
	g_platform.audio_init = audio_null_init;
	g_platform.audio_exit = audio_null_exit;

	// Serial - null driver
	g_platform.serial_init = serial_null_init;
	g_platform.serial_exit = serial_null_exit;

	// Ether - null driver
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

	// Platform/Sys functions - Unix driver (real file I/O)
	g_platform.mount_volume = platform_unix_mount_volume;
	g_platform.file_disk_layout = platform_unix_file_disk_layout;
	g_platform.floppy_init = platform_unix_floppy_init;
	g_platform.sys_add_serial_prefs = platform_unix_sys_add_serial_prefs;
	g_platform.sys_add_floppy_prefs = platform_unix_sys_add_floppy_prefs;
	g_platform.sys_add_disk_prefs = platform_unix_sys_add_disk_prefs;
	g_platform.sys_add_cdrom_prefs = platform_unix_sys_add_cdrom_prefs;
	g_platform.sys_open = platform_unix_sys_open;
	g_platform.sys_close = platform_unix_sys_close;
	g_platform.sys_read = platform_unix_sys_read;
	g_platform.sys_write = platform_unix_sys_write;
	g_platform.sys_is_readonly = platform_unix_sys_is_readonly;
	g_platform.sys_is_disk_inserted = platform_unix_sys_is_disk_inserted;
	g_platform.sys_is_fixed_disk = platform_unix_sys_is_fixed_disk;
	g_platform.sys_get_file_size = platform_unix_sys_get_file_size;
	g_platform.sys_eject = platform_unix_sys_eject;
	g_platform.sys_allow_removal = platform_unix_sys_allow_removal;
	g_platform.sys_prevent_removal = platform_unix_sys_prevent_removal;
	g_platform.sys_format = platform_unix_sys_format;
	g_platform.sys_cd_get_volume = platform_unix_sys_cd_get_volume;
	g_platform.sys_cd_set_volume = platform_unix_sys_cd_set_volume;
	g_platform.sys_cd_pause = platform_unix_sys_cd_pause;
	g_platform.sys_cd_resume = platform_unix_sys_cd_resume;
	g_platform.sys_cd_play = platform_unix_sys_cd_play;
	g_platform.sys_cd_stop = platform_unix_sys_cd_stop;
	g_platform.sys_cd_get_position = platform_unix_sys_cd_get_position;
	g_platform.sys_cd_scan = platform_unix_sys_cd_scan;
	g_platform.sys_cd_read_toc = platform_unix_sys_cd_read_toc;

	// EmulOp/Trap handlers (NULL by default - set by CPU backend or main)
	g_platform.emulop_handler = nullptr;
	g_platform.trap_handler = nullptr;

	// Code cache flush (NULL by default - set by JIT backends like Unicorn)
	g_platform.flush_code_cache = nullptr;

	// PPC backend (NULL by default - set by PPC cpu_ppc_*_install)
	g_platform.cpu_ppc_get_gpr = nullptr;
	g_platform.cpu_ppc_set_gpr = nullptr;
	g_platform.cpu_ppc_get_pc = nullptr;
	g_platform.cpu_ppc_set_pc = nullptr;
	g_platform.cpu_ppc_get_lr = nullptr;
	g_platform.cpu_ppc_set_lr = nullptr;
	g_platform.cpu_ppc_get_ctr = nullptr;
	g_platform.cpu_ppc_set_ctr = nullptr;
	g_platform.cpu_ppc_get_cr = nullptr;
	g_platform.cpu_ppc_set_cr = nullptr;
	g_platform.cpu_ppc_get_msr = nullptr;
	g_platform.cpu_ppc_set_msr = nullptr;
	g_platform.cpu_ppc_get_xer = nullptr;
	g_platform.cpu_ppc_set_xer = nullptr;
	g_platform.cpu_ppc_execute = nullptr;
	g_platform.cpu_ppc_stop = nullptr;
	g_platform.cpu_ppc_interrupt = nullptr;
	g_platform.ppc_sheep_handler = nullptr;
}
