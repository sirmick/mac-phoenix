/*
 *  platform_null.cpp - Null implementations for host platform functions
 *
 *  These are stub implementations that do nothing or return safe defaults.
 *  Used for testing and as fallbacks.
 */

#include "sysdeps.h"
#include "platform.h"
#include <sys/time.h>
#include <time.h>

/*
 *  Platform-specific stubs
 */
void platform_null_mount_volume(const char *path)
{
	(void)path;
}

void platform_null_file_disk_layout(loff_t size, loff_t *start, loff_t *length)
{
	(void)size;
	(void)start;
	(void)length;
}

void platform_null_floppy_init(void)
{
}

/*
 *  System preference stubs
 */
void platform_null_sys_add_serial_prefs(void)
{
}

void platform_null_sys_add_floppy_prefs(void)
{
}

void platform_null_sys_add_disk_prefs(void)
{
}

void platform_null_sys_add_cdrom_prefs(void)
{
}

/*
 *  File operation stubs
 */
void *platform_null_sys_open(const char *path, bool read_only, bool no_cache)
{
	(void)path;
	(void)read_only;
	(void)no_cache;
	return NULL;
}

void platform_null_sys_close(void *fh)
{
	(void)fh;
}

size_t platform_null_sys_read(void *fh, void *buf, loff_t offset, size_t length)
{
	(void)fh;
	(void)buf;
	(void)offset;
	(void)length;
	return 0;
}

size_t platform_null_sys_write(void *fh, void *buf, loff_t offset, size_t length)
{
	(void)fh;
	(void)buf;
	(void)offset;
	(void)length;
	return 0;
}

bool platform_null_sys_is_readonly(void *fh)
{
	(void)fh;
	return true;
}

bool platform_null_sys_is_disk_inserted(void *fh)
{
	(void)fh;
	return false;
}

bool platform_null_sys_is_fixed_disk(void *fh)
{
	(void)fh;
	return false;
}

loff_t platform_null_sys_get_file_size(void *fh)
{
	(void)fh;
	return 0;
}

void platform_null_sys_eject(void *fh)
{
	(void)fh;
}

void platform_null_sys_allow_removal(void *fh)
{
	(void)fh;
}

void platform_null_sys_prevent_removal(void *fh)
{
	(void)fh;
}

bool platform_null_sys_format(void *fh)
{
	(void)fh;
	return false;
}

/*
 *  CD-ROM operation stubs
 */
bool platform_null_sys_cd_get_volume(void *fh, uint8 *left, uint8 *right)
{
	(void)fh;
	*left = 255;
	*right = 255;
	return true;
}

bool platform_null_sys_cd_set_volume(void *fh, uint8 left, uint8 right)
{
	(void)fh;
	(void)left;
	(void)right;
	return true;
}

void platform_null_sys_cd_pause(void *fh)
{
	(void)fh;
}

void platform_null_sys_cd_resume(void *fh)
{
	(void)fh;
}

bool platform_null_sys_cd_play(void *fh, uint8 m1, uint8 s1, uint8 f1, uint8 m2, uint8 s2, uint8 f2)
{
	(void)fh;
	(void)m1; (void)s1; (void)f1;
	(void)m2; (void)s2; (void)f2;
	return false;
}

bool platform_null_sys_cd_stop(void *fh, uint8 m, uint8 s, uint8 f)
{
	(void)fh;
	(void)m; (void)s; (void)f;
	return true;
}

bool platform_null_sys_cd_get_position(void *fh, uint8 *pos)
{
	(void)fh;
	(void)pos;
	return false;
}

bool platform_null_sys_cd_scan(void *fh, uint8 m, uint8 s, uint8 f, bool reverse)
{
	(void)fh;
	(void)m; (void)s; (void)f;
	(void)reverse;
	return false;
}

bool platform_null_sys_cd_read_toc(void *fh, uint8 *toc)
{
	(void)fh;
	(void)toc;
	return false;
}

// Timer functions provided by timer_unix.cpp

/*
 *  Mutex stubs (not used in minimal test)
 */
struct B2_mutex {
	int dummy;
};

B2_mutex *B2_create_mutex()
{
	return new B2_mutex();
}

void B2_delete_mutex(B2_mutex *m)
{
	delete m;
}

void B2_lock_mutex(B2_mutex *m)
{
	(void)m;
}

void B2_unlock_mutex(B2_mutex *m)
{
	(void)m;
}

/*
 *  Interrupt flag operations — must be atomic (tick thread + CPU thread race)
 *  Legacy SheepShaver uses atomic_or/atomic_and for these.
 */
extern "C" void SetInterruptFlag(uint32 flag)
{
	__sync_fetch_and_or((volatile int *)&InterruptFlags, flag);
}

extern "C" void ClearInterruptFlag(uint32 flag)
{
	__sync_fetch_and_and((volatile int *)&InterruptFlags, ~flag);
}

/*
 *  CPU emulation stubs
 */
void FlushCodeCache(void *start, uint32 size)
{
	(void)start;
	(void)size;
	// Notify CPU backend that code was patched so JIT caches are invalidated.
	// Without this, JIT backends (Unicorn) execute stale translation blocks
	// after system patches (ptch resources) modify code in RAM.
	if (g_platform.flush_code_cache) {
		g_platform.flush_code_cache();
	}
}

// ExtFS stubs removed — real implementations are in platform_unix.cpp

/*
 *  Scratch memory (not used in minimal test)
 */
uint8 *ScratchMem = NULL;

/*
 *  Additional platform stubs needed for EmulOp
 */

// Video stubs
void VideoQuitFullScreen()
{
}

void VideoInterrupt()
{
}

// Timer stubs
bool tick_inhibit = false;

// TimerDateTime and Microseconds provided by timer_unix.cpp

// Ethernet interrupt — dispatch through platform driver
void EtherInterrupt()
{
	g_platform.ether_interrupt();
}

// Audio stubs
void AudioInterrupt()
{
}

bool audio_get_speaker_mute()
{
	return false;
}

void audio_set_speaker_mute(bool mute)
{
	(void)mute;
}

uint32 audio_get_speaker_volume()
{
	return 0x100;
}

void audio_set_speaker_volume(uint32 vol)
{
	(void)vol;
}

void audio_exit_stream()
{
}

bool audio_get_main_mute()
{
	return false;
}

uint32 audio_get_main_volume()
{
	return 0x100;
}

void audio_set_sample_size(int size)
{
	(void)size;
}

void audio_set_sample_rate(int rate)
{
	(void)rate;
}

void audio_set_channels(int channels)
{
	(void)channels;
}

void audio_set_main_mute(bool mute)
{
	(void)mute;
}

void audio_set_main_volume(uint32 vol)
{
	(void)vol;
}

void audio_enter_stream()
{
}

// idle_wait and idle_resume provided by timer_unix.cpp

