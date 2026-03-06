/*
 *  platform_adapter.cpp - Platform function adapters
 *
 *  Bridges core Sys_* function calls to g_platform function pointers.
 *  This allows different platform implementations (Unix, Windows, null, etc.)
 *  to be selected at runtime.
 */

#include "sysdeps.h"
#include "platform.h"

/*
 *  Platform initialization stubs
 */
void SysAddSerialPrefs(void)
{
	g_platform.sys_add_serial_prefs();
}

void SysAddFloppyPrefs(void)
{
	g_platform.sys_add_floppy_prefs();
}

void SysAddDiskPrefs(void)
{
	g_platform.sys_add_disk_prefs();
}

void SysAddCDROMPrefs(void)
{
	g_platform.sys_add_cdrom_prefs();
}

/*
 *  File operations
 */
void *Sys_open(const char *path, bool read_only, bool no_cache)
{
	return g_platform.sys_open(path, read_only, no_cache);
}

void Sys_close(void *fh)
{
	g_platform.sys_close(fh);
}

size_t Sys_read(void *fh, void *buf, loff_t offset, size_t length)
{
	return g_platform.sys_read(fh, buf, offset, length);
}

size_t Sys_write(void *fh, void *buf, loff_t offset, size_t length)
{
	return g_platform.sys_write(fh, buf, offset, length);
}

bool SysIsReadOnly(void *fh)
{
	return g_platform.sys_is_readonly(fh);
}

bool SysIsDiskInserted(void *fh)
{
	return g_platform.sys_is_disk_inserted(fh);
}

bool SysIsFixedDisk(void *fh)
{
	return g_platform.sys_is_fixed_disk(fh);
}

loff_t SysGetFileSize(void *fh)
{
	return g_platform.sys_get_file_size(fh);
}

void SysEject(void *fh)
{
	g_platform.sys_eject(fh);
}

void SysAllowRemoval(void *fh)
{
	g_platform.sys_allow_removal(fh);
}

void SysPreventRemoval(void *fh)
{
	g_platform.sys_prevent_removal(fh);
}

bool SysFormat(void *fh)
{
	return g_platform.sys_format(fh);
}

/*
 *  CD-ROM operations
 */
bool SysCDGetVolume(void *fh, uint8 &left, uint8 &right)
{
	return g_platform.sys_cd_get_volume(fh, &left, &right);
}

bool SysCDSetVolume(void *fh, uint8 left, uint8 right)
{
	return g_platform.sys_cd_set_volume(fh, left, right);
}

void SysCDPause(void *fh)
{
	g_platform.sys_cd_pause(fh);
}

void SysCDResume(void *fh)
{
	g_platform.sys_cd_resume(fh);
}

bool SysCDPlay(void *fh, uint8 start_m, uint8 start_s, uint8 start_f, uint8 end_m, uint8 end_s, uint8 end_f)
{
	return g_platform.sys_cd_play(fh, start_m, start_s, start_f, end_m, end_s, end_f);
}

bool SysCDStop(void *fh, uint8 lead_out_m, uint8 lead_out_s, uint8 lead_out_f)
{
	return g_platform.sys_cd_stop(fh, lead_out_m, lead_out_s, lead_out_f);
}

bool SysCDGetPosition(void *fh, uint8 *pos)
{
	return g_platform.sys_cd_get_position(fh, pos);
}

bool SysCDScan(void *fh, uint8 start_m, uint8 start_s, uint8 start_f, bool reverse)
{
	return g_platform.sys_cd_scan(fh, start_m, start_s, start_f, reverse);
}

bool SysCDReadTOC(void *fh, uint8 *toc)
{
	return g_platform.sys_cd_read_toc(fh, toc);
}

/*
 *  Platform-specific functions
 */
void MountVolume(const char *path)
{
	g_platform.mount_volume(path);
}

void FileDiskLayout(loff_t size, loff_t *start, loff_t *length)
{
	g_platform.file_disk_layout(size, start, length);
}

void FloppyInit(void)
{
	g_platform.floppy_init();
}
