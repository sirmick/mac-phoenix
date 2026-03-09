/*
 *  sys_unix.cpp - System dependent routines, Unix implementation
 *
 *  Basilisk II (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef HAVE_AVAILABILITYMACROS_H
#include <AvailabilityMacros.h>
#endif

#ifdef __linux__
#include <sys/mount.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <dirent.h>
#include <limits.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/cdio.h>
#endif

#if defined __APPLE__ && defined __MACH__
#include <sys/disk.h>
#if (defined AQUA || defined HAVE_FRAMEWORK_COREFOUNDATION)
#ifndef __MACOSX__
#define __MACOSX__ MAC_OS_X_VERSION_MIN_REQUIRED
#endif
#endif
#endif

#include "main.h"
#include "macos_util.h"
#include "user_strings.h"
#include "emulator_config.h"
// #include "sys.h"  // Commented out - conflicts with our static implementations

// Media type constants from sys.h
enum {
	MEDIA_FLOPPY		= 1,
	MEDIA_CD			= 2,
	MEDIA_HD			= 4,
	MEDIA_REMOVABLE		= MEDIA_FLOPPY | MEDIA_CD
};

#include "disk_unix.h"

#if defined(BINCUE)
#include "bincue.h"
#endif



#define DEBUG 0
#include "debug.h"

// Stub factory for sparsebundle disks (not implemented yet)
disk_generic::status disk_sparsebundle_factory(const char *path, bool read_only, disk_generic **disk)
{
	(void)path;
	(void)read_only;
	(void)disk;
	// Return UNKNOWN to let other factories try, not INVALID which stops processing
	return disk_generic::DISK_UNKNOWN;
}

static disk_factory *disk_factories[] = {
#ifndef STANDALONE_GUI
	disk_sparsebundle_factory,
#if defined(HAVE_LIBVHD)
	disk_vhd_factory,
#endif
#endif
	NULL
};

// File handles are pointers to these structures
struct mac_file_handle {
	char *name;	        // Copy of device/file name
	int fd;

	bool is_file;		// Flag: plain file or /dev/something?
	bool is_floppy;		// Flag: floppy device
	bool is_cdrom;		// Flag: CD-ROM device
	bool read_only;		// Copy of Sys_open() flag

	loff_t start_byte;	// Size of file header (if any)
	loff_t file_size;	// Size of file data (only valid if is_file is true)

	bool is_media_present;		// Flag: media is inserted and available
	disk_generic *generic_disk;

#if defined(__linux__)
	int cdrom_cap;		// CD-ROM capability flags (only valid if is_cdrom is true)
#elif defined(__FreeBSD__)
	struct ioc_capability cdrom_cap;
#elif defined(__APPLE__) && defined(__MACH__)
	char *ioctl_name;	// For CDs on OS X - a device for special ioctls
	int ioctl_fd;
#endif

#if defined(BINCUE)
	bool is_bincue;		// Flag: BIN CUE file
	void *bincue_fd;
#endif
};

// Open file handles
struct open_mac_file_handle {
	mac_file_handle *fh;
	open_mac_file_handle *next;
};
static open_mac_file_handle *open_mac_file_handles = NULL;

// File handle of first floppy drive (for SysMountFirstFloppy())
static mac_file_handle *first_floppy = NULL;

// Prototypes
static void cdrom_close(mac_file_handle *fh);
static bool cdrom_open(mac_file_handle *fh, const char *path = NULL);


/*
 *  Initialization
 */

void SysInit(void)
{
#if defined __MACOSX__
	extern void DarwinSysInit(void);
	DarwinSysInit();
#endif
}


/*
 *  Deinitialization
 */

void SysExit(void)
{
#if defined __MACOSX__
	extern void DarwinSysExit(void);
	DarwinSysExit();
#endif
}


/*
 *  Manage open file handles
 */

static void sys_add_mac_file_handle(mac_file_handle *fh)
{
	open_mac_file_handle *p = new open_mac_file_handle;
	p->fh = fh;
	p->next = open_mac_file_handles;
	open_mac_file_handles = p;
}

static void sys_remove_mac_file_handle(mac_file_handle *fh)
{
	open_mac_file_handle *p = open_mac_file_handles;
	open_mac_file_handle *q = NULL;

	while (p) {
		if (p->fh == fh) {
			if (q)
				q->next = p->next;
			else
				open_mac_file_handles = p->next;
			delete p;
			break;
		}
		q = p;
		p = p->next;
	}
}


/*
 *  Account for media that has just arrived
 */

void SysMediaArrived(const char *path, int type)
{
	// Replace the "cdrom" entry (we are polling, it's unique)
	if (type == MEDIA_CD && !config::EmulatorConfig::instance().nocdrom) {
		// Hot-plug CD: add to config paths
		auto& paths = config::EmulatorConfig::instance().cdrom_paths;
		if (paths.empty())
			paths.push_back(path);
		else
			paths[0] = path;
	}

	// Wait for media to be available for reading
	if (open_mac_file_handles) {
		const int MAX_WAIT = 5;
		for (int i = 0; i < MAX_WAIT; i++) {
			if (access(path, R_OK) == 0)
				break;
			switch (errno) {
			case ENOENT: // Unlikely
			case EACCES: // MacOS X is mounting the media
				sleep(1);
				continue;
			}
			printf("WARNING: Cannot access %s (%s)\n", path, strerror(errno));
			return;
		}
	}

	for (open_mac_file_handle *p = open_mac_file_handles; p != NULL; p = p->next) {
		mac_file_handle * const fh = p->fh;

		// Re-open CD-ROM device
		if (fh->is_cdrom && type == MEDIA_CD) {
			cdrom_close(fh);
			if (cdrom_open(fh, path)) {
				fh->is_media_present = true;
				MountVolume(fh);
			}
		}
	}
}


/*
 *  Account for media that has just been removed
 */

void SysMediaRemoved(const char *path, int type)
{
	if ((type & MEDIA_REMOVABLE) != MEDIA_CD)
		return;

	for (open_mac_file_handle *p = open_mac_file_handles; p != NULL; p = p->next) {
		mac_file_handle * const fh = p->fh;

		// Mark media as not available
		if (!fh->is_cdrom || !fh->is_media_present)
			continue;
		if (fh->name && strcmp(fh->name, path) == 0) {
			fh->is_media_present = false;
			break;
		}
#if defined __MACOSX__
		if (fh->ioctl_name && strcmp(fh->ioctl_name, path) == 0) {
			fh->is_media_present = false;
			break;
		}
#endif
	}
}


/*
 *  Mount first floppy disk
 */

void SysMountFirstFloppy(void)
{
	if (first_floppy)
		MountVolume(first_floppy);
}


/*
 *  This gets called when no "floppy" prefs items are found
 *  It scans for available floppy drives and adds appropriate prefs items
 */

static void SysAddFloppyPrefs(void)
{
	// No-op: floppy paths come from EmulatorConfig
}


/*
 *  This gets called when no "disk" prefs items are found
 *  It scans for available HFS volumes and adds appropriate prefs items
 *	On OS X, we could do the same, but on an OS X machine I think it is
 *	very unlikely that any mounted volumes would contain a system which
 *	is old enough to boot a 68k Mac, so we just do nothing here for now.
 */

static void SysAddDiskPrefs(void)
{
	// No-op: disk paths come from EmulatorConfig
}


/*
 *  This gets called when no "cdrom" prefs items are found
 *  It scans for available CD-ROM drives and adds appropriate prefs items
 */

static void SysAddCDROMPrefs(void)
{
	// No-op: CDROM paths come from EmulatorConfig
}


/*
 *  Add default serial prefs (must be added, even if no ports present)
 */

static void SysAddSerialPrefs(void)
{
	// No-op: serial config comes from EmulatorConfig
}


/*
 *  Open CD-ROM device and initialize internal data
 */

static bool cdrom_open_1(mac_file_handle *fh)
{
#if defined __MACOSX__
	// In OS X, the device name is OK for sending ioctls to,
	// but not for reading raw CDROM data from.
	// (it seems to have extra data padded in)
	//
	// So, we keep the already opened file handle,
	// and open a slightly different file for CDROM data 
	//
	fh->ioctl_fd = fh->fd;
	fh->ioctl_name = fh->name;
	fh->fd = -1;
	fh->name = (char *)malloc(strlen(fh->ioctl_name) + 3);
	if (fh->name) {
		strcpy(fh->name, fh->ioctl_name);
		strcat(fh->name, "s1");
		fh->fd = open(fh->name, O_RDONLY, O_NONBLOCK);
	}
	if (fh->ioctl_fd < 0)
		return false;
#endif
	return true;
}

bool cdrom_open(mac_file_handle *fh, const char *path)
{
	if (path)
		fh->name = strdup(path);
	fh->fd = open(fh->name, O_RDONLY, O_NONBLOCK);
	fh->start_byte = 0;
	if (!cdrom_open_1(fh))
		return false;
	return fh->fd >= 0;
}


/*
 *  Close a CD-ROM device
 */

void cdrom_close(mac_file_handle *fh)
{

	if (fh->fd >= 0) {
		close(fh->fd);
		fh->fd = -1;
	}
	if (fh->name) {
		free(fh->name);
		fh->name = NULL;
	}
#if defined __MACOSX__
	if (fh->ioctl_fd >= 0) {
		close(fh->ioctl_fd);
		fh->ioctl_fd = -1;
	}
	if (fh->ioctl_name) {
		free(fh->ioctl_name);
		fh->ioctl_name = NULL;
	}
#endif
}


/*
 *  Check if device is a mounted HFS volume, get mount name
 */

static bool is_drive_mounted(const char *dev_name, char *mount_name)
{
#ifdef __linux__
	FILE *f = fopen("/proc/mounts", "r");
	if (f) {
		char line[256];
		while(fgets(line, 255, f)) {
			// Read line
			int len = strlen(line);
			if (len == 0)
				continue;
			line[len-1] = 0;

			// Parse line
			if (strncmp(line, dev_name, strlen(dev_name)) == 0) {
				mount_name[0] = 0;
				char *dummy;
				sscanf(line, "%as %s", &dummy, mount_name);
				free(dummy);
				fclose(f);
				return true;
			}
		}
		fclose(f);
	}
#endif
	return false;
}


/*
 *  Open file/device, create new file handle (returns NULL on error)
 */
 
static mac_file_handle *open_filehandle(const char *name)
{
		mac_file_handle *fh = new mac_file_handle;
		memset(fh, 0, sizeof(mac_file_handle));
		fh->name = strdup(name);
		fh->fd = -1;
		fh->generic_disk = NULL;
#if defined __MACOSX__
		fh->ioctl_fd = -1;
		fh->ioctl_name = NULL;
#endif
		return fh;
}

static void *Sys_open(const char *name, bool read_only, bool is_cdrom)
{
	bool is_file = strncmp(name, "/dev/", 5) != 0;
#if defined(__FreeBSD__)
	                // SCSI                             IDE
	is_cdrom |= strncmp(name, "/dev/cd", 7) == 0 || strncmp(name, "/dev/acd", 8) == 0;
#else
	is_cdrom |= strncmp(name, "/dev/cd", 7) == 0;
#endif
	bool is_floppy = strncmp(name, "/dev/fd", 7) == 0;

	bool is_polled_media = strncmp(name, "/dev/poll/", 10) == 0;
	if (is_floppy) // Floppy open fails if there's no disk inserted
		is_polled_media = true;

#if defined __MACOSX__
	// There is no set filename in /dev which is the cdrom,
	// so we have to see if it is any of the devices that we found earlier
	{
		auto& cfg = config::EmulatorConfig::instance();
		for (const auto& cdpath : cfg.cdrom_paths) {
			if (is_polled_media || cdpath == name) {
				is_cdrom = true;
				read_only = true;
				break;
			}
		}
	}
#endif

	D(bug("Sys_open(%s, %s)\n", name, read_only ? "read-only" : "read/write"));

	// Check if write access is allowed, set read-only flag if not
	if (!read_only && access(name, W_OK))
		read_only = true;

	// Print warning message and eventually unmount drive when this is an HFS volume mounted under Linux (double mounting will corrupt the volume)
	char mount_name[256];
	if (!is_file && !read_only && is_drive_mounted(name, mount_name)) {
		char str[512];
		sprintf(str, GetString(STR_VOLUME_IS_MOUNTED_WARN), mount_name);
		WarningAlert(str);
		sprintf(str, "umount %s", mount_name);
		if (system(str)) {
			sprintf(str, GetString(STR_CANNOT_UNMOUNT_WARN), mount_name, strerror(errno));
			WarningAlert(str);
			return NULL;
		}
	}

	// Open file/device

#if defined(BINCUE)
	void *binfd = open_bincue(name);
	if (binfd) {
		mac_file_handle *fh = open_filehandle(name);
		D(bug("opening %s as bincue\n", name));
		fh->bincue_fd = binfd;
		fh->is_bincue = true;
		fh->read_only = true;
		fh->is_media_present = true;
		sys_add_mac_file_handle(fh);
		return fh;
	}
#endif


	for (int i = 0; disk_factories[i]; ++i) {
		disk_factory *f = disk_factories[i];
		disk_generic *generic;
		disk_generic::status st = f(name, read_only, &generic);
		if (st == disk_generic::DISK_INVALID)
			return NULL;
		if (st == disk_generic::DISK_VALID) {
			mac_file_handle *fh = open_filehandle(name);
			fh->generic_disk = generic;
			fh->file_size = generic->size();
			fh->read_only = generic->is_read_only();
			fh->is_media_present = true;
			sys_add_mac_file_handle(fh);
			return fh;
		}
	}

	int open_flags = (read_only ? O_RDONLY : O_RDWR);
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__MACOSX__)
	open_flags |= (is_cdrom ? O_NONBLOCK : 0);
#endif
#if defined(__MACOSX__)
	open_flags |= (is_file ? O_EXLOCK | O_NONBLOCK : 0);
#endif
	int fd = open(name, open_flags);
#if defined(__MACOSX__)
	if (fd < 0 && (open_flags & O_EXLOCK)) {
		if (errno == EOPNOTSUPP) {
			// File system does not support locking. Try again without.
			open_flags &= ~O_EXLOCK;
			fd = open(name, open_flags);
		} else if (errno == EAGAIN) {
			// File is likely already locked by another process.
			printf("WARNING: Cannot open %s (%s)\n", name, strerror(errno));
			return NULL;
		}
	}
#endif
	if (fd < 0 && !read_only) {
		// Read-write failed, try read-only
		read_only = true;
		fd = open(name, O_RDONLY);
	}
	if (fd >= 0 || is_polled_media) {
		mac_file_handle *fh = open_filehandle(name);
		fh->fd = fd;
		fh->is_file = is_file;
		fh->read_only = read_only;
		fh->is_floppy = is_floppy;
		fh->is_cdrom = is_cdrom;
		if (fh->is_file) {
			fh->is_media_present = true;
			// Detect disk image file layout
			loff_t size = 0;
			size = lseek(fd, 0, SEEK_END);
			uint8 data[256];
			lseek(fd, 0, SEEK_SET);
			read(fd, data, 256);
			FileDiskLayout(size, data, fh->start_byte, fh->file_size);
		} else {
			struct stat st;
			if (fstat(fd, &st) == 0) {
				fh->is_media_present = true;
				if (S_ISBLK(st.st_mode)) {
					fh->is_cdrom = is_cdrom;
#if defined(__linux__)
					fh->is_floppy = (MAJOR(st.st_rdev) == FLOPPY_MAJOR);
#ifdef CDROM_GET_CAPABILITY
					if (is_cdrom) {
						fh->cdrom_cap = ioctl(fh->fd, CDROM_GET_CAPABILITY);
						if (fh->cdrom_cap < 0)
							fh->cdrom_cap = 0;
					}
#endif
#elif defined(__FreeBSD__)
					fh->is_floppy = ((st.st_rdev >> 16) == 2);
#ifdef CDIOCCAPABILITY
					if (is_cdrom) {
						if (ioctl(fh->fd, CDIOCCAPABILITY, &fh->cdrom_cap) < 0)
							memset(&fh->cdrom_cap, 0, sizeof(fh->cdrom_cap));
					}
#endif
#elif defined(__NetBSD__)
					fh->is_floppy = ((st.st_rdev >> 16) == 2);
#endif
				}
#if defined __MACOSX__
				if (is_cdrom) {
					fh->is_cdrom = true;
					fh->is_floppy = false;
					if (cdrom_open_1(fh))
						fh->is_media_present = true;
				}
#endif
			}
		}
		if (fh->is_floppy && first_floppy == NULL)
			first_floppy = fh;
		sys_add_mac_file_handle(fh);
		return fh;
	} else {
		printf("WARNING: Cannot open %s (%s)\n", name, strerror(errno));
		return NULL;
	}
}


/*
 *  Close file/device, delete file handle
 */

static void Sys_close(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;

	sys_remove_mac_file_handle(fh);

#if defined(BINCUE)
	if (fh->is_bincue)
		close_bincue(fh->bincue_fd);
#endif
	if (fh->generic_disk)
		delete fh->generic_disk;

	if (fh->is_cdrom)
		cdrom_close(fh);
	if (fh->fd >= 0)
		close(fh->fd);
	if (fh->name)
		free(fh->name);
	delete fh;
}


/*
 *  Read "length" bytes from file/device, starting at "offset", to "buffer",
 *  returns number of bytes read (or 0)
 */

static size_t Sys_read(void *arg, void *buffer, loff_t offset, size_t length)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return 0;

#if defined(BINCUE)
	if (fh->is_bincue)
		return read_bincue(fh->bincue_fd, buffer, offset, length);
#endif

	if (fh->generic_disk)
		return fh->generic_disk->read(buffer, offset, length);
	
	// Seek to position
	if (lseek(fh->fd, offset + fh->start_byte, SEEK_SET) < 0)
		return 0;

	// Read data
	return read(fh->fd, buffer, length);
}


/*
 *  Write "length" bytes from "buffer" to file/device, starting at "offset",
 *  returns number of bytes written (or 0)
 */

static size_t Sys_write(void *arg, void *buffer, loff_t offset, size_t length)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return 0;

	if (fh->generic_disk)
		return fh->generic_disk->write(buffer, offset, length);

	// Seek to position
	if (lseek(fh->fd, offset + fh->start_byte, SEEK_SET) < 0)
		return 0;

	// Write data
	return write(fh->fd, buffer, length);
}


/*
 *  Return size of file/device (minus header)
 */

static loff_t SysGetFileSize(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return true;

#if defined(BINCUE)
	if (fh->is_bincue)
		return size_bincue(fh->bincue_fd);
#endif 

	if (fh->generic_disk)
		return fh->file_size;

	if (fh->is_file)
		return fh->file_size;
	else {
#if defined(__linux__)
		long blocks;
		if (ioctl(fh->fd, BLKGETSIZE, &blocks) < 0)
			return 0;
		D(bug(" BLKGETSIZE returns %d blocks\n", blocks));
		return (loff_t)blocks * 512;
#elif defined __MACOSX__
		uint32 block_size;
		if (ioctl(fh->ioctl_fd, DKIOCGETBLOCKSIZE, &block_size) < 0)
			return 0;
		D(bug(" DKIOCGETBLOCKSIZE returns %lu bytes\n", (unsigned long)block_size));
		uint64 block_count;
		if (ioctl(fh->ioctl_fd, DKIOCGETBLOCKCOUNT, &block_count) < 0)
			return 0;
		D(bug(" DKIOCGETBLOCKCOUNT returns %llu blocks\n", (unsigned long long)block_count));
		return block_count * block_size;
#else
		return lseek(fh->fd, 0, SEEK_END) - fh->start_byte;
#endif
	}
}


/*
 *  Eject volume (if applicable)
 */

static void SysEject(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;

#if defined(__linux__)
	if (fh->is_floppy) {
		if (fh->fd >= 0) {
			fsync(fh->fd);
			ioctl(fh->fd, FDFLUSH);
			ioctl(fh->fd, FDEJECT);
			close(fh->fd);	// Close and reopen so the driver will see the media change
		}
		fh->fd = open(fh->name, fh->read_only ? O_RDONLY : O_RDWR);
	} else if (fh->is_cdrom) {
		ioctl(fh->fd, CDROMEJECT);
		close(fh->fd);	// Close and reopen so the driver will see the media change
		fh->fd = open(fh->name, O_RDONLY | O_NONBLOCK);
	}
#elif defined(__FreeBSD__) || defined(__NetBSD__)
	if (fh->is_floppy) {
		fsync(fh->fd);
	} else if (fh->is_cdrom) {
		ioctl(fh->fd, CDIOCEJECT);
		close(fh->fd);	// Close and reopen so the driver will see the media change
		fh->fd = open(fh->name, O_RDONLY | O_NONBLOCK);
	}
#elif defined(__APPLE__) && defined(__MACH__)
	if (fh->is_cdrom && fh->is_media_present) {
		close(fh->fd);
		fh->fd = -1;
		if (ioctl(fh->ioctl_fd, DKIOCEJECT) < 0) {
			D(bug(" DKIOCEJECT failed on file %s: %s\n",
				   fh->ioctl_name, strerror(errno)));

			// If we are running MacOS X, the device may be in busy
			// state because the Finder has mounted the disk
			close(fh->ioctl_fd);
			fh->ioctl_fd = -1;

			// Try to use "diskutil eject" but it can take up to 5
			// seconds to complete
			if (fh->ioctl_name) {
				static const char eject_cmd[] = "/usr/sbin/diskutil eject %s 2>&1 >/dev/null";
				char *cmd = (char *)alloca(strlen(eject_cmd) + strlen(fh->ioctl_name) + 1);
				sprintf(cmd, eject_cmd, fh->ioctl_name);
				system(cmd);
			}
		}
		fh->is_media_present = false;
	}
#endif
}


/*
 *  Format volume (if applicable)
 */

static bool SysFormat(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

	//!!
	return true;
}


/*
 *  Check if file/device is read-only (this includes the read-only flag on Sys_open())
 */

static bool SysIsReadOnly(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return true;

#if defined(__linux__)
	if (fh->is_floppy) {
		if (fh->fd >= 0) {
			struct floppy_drive_struct stat;
			ioctl(fh->fd, FDGETDRVSTAT, &stat);
			return !(stat.flags & FD_DISK_WRITABLE);
		} else
			return true;
	} else
#endif
		return fh->read_only;
}


/*
 *  Check if the given file handle refers to a fixed or a removable disk
 */

static bool SysIsFixedDisk(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return true;

	if (fh->generic_disk)
		return true;

	if (fh->is_file)
		return true;
	else if (fh->is_floppy || fh->is_cdrom)
		return false;
	else
		return true;
}


/*
 *  Check if a disk is inserted in the drive (always true for files)
 */

static bool SysIsDiskInserted(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

	if (fh->generic_disk)
		return true;
	
	if (fh->is_file) {
		return true;

#if defined(__linux__)
	} else if (fh->is_floppy) {
		char block[512];
		lseek(fh->fd, 0, SEEK_SET);
		ssize_t actual = read(fh->fd, block, 512);
		if (actual < 0) {
			close(fh->fd);	// Close and reopen so the driver will see the media change
			fh->fd = open(fh->name, fh->read_only ? O_RDONLY : O_RDWR);
			actual = read(fh->fd, block, 512);
		}
		return actual == 512;
	} else if (fh->is_cdrom) {
#ifdef CDROM_MEDIA_CHANGED
		if (fh->cdrom_cap & CDC_MEDIA_CHANGED) {
			// If we don't do this, all attempts to read from a disc fail
			// once the tray has been opened (altough the TOC reads fine).
			// Can somebody explain this to me?
			if (ioctl(fh->fd, CDROM_MEDIA_CHANGED) == 1) {
				close(fh->fd);
				fh->fd = open(fh->name, O_RDONLY | O_NONBLOCK);
			}
		}
#endif
#ifdef CDROM_DRIVE_STATUS
		if (fh->cdrom_cap & CDC_DRIVE_STATUS) {
			return ioctl(fh->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT) == CDS_DISC_OK;
		}
#endif
		cdrom_tochdr header;
		return ioctl(fh->fd, CDROMREADTOCHDR, &header) == 0;
#elif defined(__FreeBSD__) || defined(__NetBSD__)
	} else if (fh->is_floppy) {
		return false;	//!!
	} else if (fh->is_cdrom) {
		struct ioc_toc_header header;
		return ioctl(fh->fd, CDIOREADTOCHEADER, &header) == 0;
#elif defined __MACOSX__
	} else if (fh->is_cdrom || fh->is_floppy) {
		return fh->is_media_present;
#endif

	} else
		return true;
}


/*
 *  Prevent medium removal (if applicable)
 */

static void SysPreventRemoval(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;

#if defined(__linux__) && defined(CDROM_LOCKDOOR)
	if (fh->is_cdrom)
		ioctl(fh->fd, CDROM_LOCKDOOR, 1);	
#endif
}


/*
 *  Allow medium removal (if applicable)
 */

static void SysAllowRemoval(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;

#if defined(__linux__) && defined(CDROM_LOCKDOOR)
	if (fh->is_cdrom)
		ioctl(fh->fd, CDROM_LOCKDOOR, 0);	
#endif
}


/*
 *  Read CD-ROM TOC (binary MSF format, 804 bytes max.)
 */

static bool SysCDReadTOC(void *arg, uint8 *toc)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

#if defined(BINCUE)
	if (fh->is_bincue)
		return readtoc_bincue(fh->bincue_fd, toc);
#endif

	if (fh->is_cdrom) {

#if defined(__linux__)
		uint8 *p = toc + 2;

		// Header
		cdrom_tochdr header;
		if (ioctl(fh->fd, CDROMREADTOCHDR, &header) < 0)
			return false;
		*p++ = header.cdth_trk0;
		*p++ = header.cdth_trk1;

		// Tracks
		cdrom_tocentry entry;
		for (int i=header.cdth_trk0; i<=header.cdth_trk1; i++) {
			entry.cdte_track = i;
			entry.cdte_format = CDROM_MSF;
			if (ioctl(fh->fd, CDROMREADTOCENTRY, &entry) < 0)
				return false;
			*p++ = 0;
			*p++ = (entry.cdte_adr << 4) | entry.cdte_ctrl;
			*p++ = entry.cdte_track;
			*p++ = 0;
			*p++ = 0;
			*p++ = entry.cdte_addr.msf.minute;
			*p++ = entry.cdte_addr.msf.second;
			*p++ = entry.cdte_addr.msf.frame;
		}

		// Leadout track
		entry.cdte_track = CDROM_LEADOUT;
		entry.cdte_format = CDROM_MSF;
		if (ioctl(fh->fd, CDROMREADTOCENTRY, &entry) < 0)
			return false;
		*p++ = 0;
		*p++ = (entry.cdte_adr << 4) | entry.cdte_ctrl;
		*p++ = entry.cdte_track;
		*p++ = 0;
		*p++ = 0;
		*p++ = entry.cdte_addr.msf.minute;
		*p++ = entry.cdte_addr.msf.second;
		*p++ = entry.cdte_addr.msf.frame;

		// TOC size
		int toc_size = p - toc;
		*toc++ = toc_size >> 8;
		*toc++ = toc_size & 0xff;
		return true;
#elif defined __MACOSX__ && defined MAC_OS_X_VERSION_10_2
		if (fh->is_media_present) {
			extern bool DarwinCDReadTOC(char *name, uint8 *toc);
			return DarwinCDReadTOC(fh->name, toc);
		}
		return false;
#elif defined(__FreeBSD__)
		uint8 *p = toc + 2;

		// Header
		struct ioc_toc_header header;
		if (ioctl(fh->fd, CDIOREADTOCHEADER, &header) < 0)
			return false;
		*p++ = header.starting_track;
		*p++ = header.ending_track;

		// Tracks
		struct ioc_read_toc_single_entry entry;
		for (int i=header.starting_track; i<=header.ending_track; i++) {
			entry.track = i;
			entry.address_format = CD_MSF_FORMAT;
			if (ioctl(fh->fd, CDIOREADTOCENTRY, &entry) < 0)
				return false;
			*p++ = 0;
			*p++ = (entry.entry.addr_type << 4) | entry.entry.control;
			*p++ = entry.entry.track;
			*p++ = 0;
			*p++ = 0;
			*p++ = entry.entry.addr.msf.minute;
			*p++ = entry.entry.addr.msf.second;
			*p++ = entry.entry.addr.msf.frame;
		}

		// Leadout track
		entry.track = CD_TRACK_INFO;
		entry.address_format = CD_MSF_FORMAT;
		if (ioctl(fh->fd, CDIOREADTOCENTRY, &entry) < 0)
			return false;
		*p++ = 0;
		*p++ = (entry.entry.addr_type << 4) | entry.entry.control;
		*p++ = entry.entry.track;
		*p++ = 0;
		*p++ = 0;
		*p++ = entry.entry.addr.msf.minute;
		*p++ = entry.entry.addr.msf.second;
		*p++ = entry.entry.addr.msf.frame;

		// TOC size
		int toc_size = p - toc;
		*toc++ = toc_size >> 8;
		*toc++ = toc_size & 0xff;
		return true;
#elif defined(__NetBSD__)
		uint8 *p = toc + 2;

		// Header
		struct ioc_toc_header header;
		if (ioctl(fh->fd, CDIOREADTOCHEADER, &header) < 0)
			return false;
		*p++ = header.starting_track;
		*p++ = header.ending_track;

		// Tracks (this is nice... :-)
		struct ioc_read_toc_entry entries;
		entries.address_format = CD_MSF_FORMAT;
		entries.starting_track = 1;
		entries.data_len = 800;
		entries.data = (cd_toc_entry *)p;
		if (ioctl(fh->fd, CDIOREADTOCENTRIES, &entries) < 0)
			return false;

		// TOC size
		int toc_size = p - toc;
		*toc++ = toc_size >> 8;
		*toc++ = toc_size & 0xff;
		return true;
#else
		return false;
#endif
	} else
		return false;
}


/*
 *  Read CD-ROM position data (Sub-Q Channel, 16 bytes, see SCSI standard)
 */

static bool SysCDGetPosition(void *arg, uint8 *pos)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

#if defined(BINCUE)
	if (fh->is_bincue)
		return GetPosition_bincue(fh->bincue_fd, pos);
#endif

	if (fh->is_cdrom) {
#if defined(__linux__)
		cdrom_subchnl chan;
		chan.cdsc_format = CDROM_MSF;
		if (ioctl(fh->fd, CDROMSUBCHNL, &chan) < 0)
			return false;
		*pos++ = 0;
		*pos++ = chan.cdsc_audiostatus;
		*pos++ = 0;
		*pos++ = 12;	// Sub-Q data length
		*pos++ = 0;
		*pos++ = (chan.cdsc_adr << 4) | chan.cdsc_ctrl;
		*pos++ = chan.cdsc_trk;
		*pos++ = chan.cdsc_ind;
		*pos++ = 0;
		*pos++ = chan.cdsc_absaddr.msf.minute;
		*pos++ = chan.cdsc_absaddr.msf.second;
		*pos++ = chan.cdsc_absaddr.msf.frame;
		*pos++ = 0;
		*pos++ = chan.cdsc_reladdr.msf.minute;
		*pos++ = chan.cdsc_reladdr.msf.second;
		*pos++ = chan.cdsc_reladdr.msf.frame;
		return true;
#elif defined(__FreeBSD__) || defined(__NetBSD__)
		struct ioc_read_subchannel chan;
		chan.data_format = CD_MSF_FORMAT;
		chan.address_format = CD_MSF_FORMAT;
		chan.track = CD_CURRENT_POSITION;
		if (ioctl(fh->fd, CDIOCREADSUBCHANNEL, &chan) < 0)
			return false;
		*pos++ = 0;
		*pos++ = chan.data->header.audio_status;
		*pos++ = 0;
		*pos++ = 12;	// Sub-Q data length
		*pos++ = 0;
		*pos++ = (chan.data->what.position.addr_type << 4) | chan.data->what.position.control;
		*pos++ = chan.data->what.position.track_number;
		*pos++ = chan.data->what.position.index_number;
		*pos++ = 0;
		*pos++ = chan.data->what.position.absaddr.msf.minute;
		*pos++ = chan.data->what.position.absaddr.msf.second;
		*pos++ = chan.data->what.position.absaddr.msf.frame;
		*pos++ = 0;
		*pos++ = chan.data->what.position.reladdr.msf.minute;
		*pos++ = chan.data->what.position.reladdr.msf.second;
		*pos++ = chan.data->what.position.reladdr.msf.frame;
		return true;
#else
		return false;
#endif
	} else
		return false;
}


/*
 *  Play CD audio
 */

static bool SysCDPlay(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, uint8 end_m, uint8 end_s, uint8 end_f)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

#if defined(BINCUE)
	if (fh->is_bincue)
		return CDPlay_bincue(fh->bincue_fd, start_m, start_s, start_f, end_m, end_s, end_f);
#endif

	if (fh->is_cdrom) {
#if defined(__linux__)
		cdrom_msf play;
		play.cdmsf_min0 = start_m;
		play.cdmsf_sec0 = start_s;
		play.cdmsf_frame0 = start_f;
		play.cdmsf_min1 = end_m;
		play.cdmsf_sec1 = end_s;
		play.cdmsf_frame1 = end_f;
		return ioctl(fh->fd, CDROMPLAYMSF, &play) == 0;
#elif defined(__FreeBSD__) || defined(__NetBSD__)
		struct ioc_play_msf play;
		play.start_m = start_m;
		play.start_s = start_s;
		play.start_f = start_f;
		play.end_m = end_m;
		play.end_s = end_s;
		play.end_f = end_f;
		return ioctl(fh->fd, CDIOCPLAYMSF, &play) == 0;
#else
		return false;
#endif
	} else
		return false;
}


/*
 *  Pause CD audio
 */

static bool SysCDPause(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

#if defined(BINCUE)
	if (fh->is_bincue)
		return CDPause_bincue(fh->bincue_fd);
#endif

	if (fh->is_cdrom) {
#if defined(__linux__)
		return ioctl(fh->fd, CDROMPAUSE) == 0;
#elif defined(__FreeBSD__) || defined(__NetBSD__)
		return ioctl(fh->fd, CDIOCPAUSE) == 0;
#else
		return false;
#endif
	} else
		return false;
}


/*
 *  Resume paused CD audio
 */

static bool SysCDResume(void *arg)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

#if defined(BINCUE)
	if (fh->is_bincue)
		return CDResume_bincue(fh->bincue_fd);
#endif


	if (fh->is_cdrom) {
#if defined(__linux__)
		return ioctl(fh->fd, CDROMRESUME) == 0;
#elif defined(__FreeBSD__) || defined(__NetBSD__)
		return ioctl(fh->fd, CDIOCRESUME) == 0;
#else
		return false;
#endif
	} else
		return false;
}


/*
 *  Stop CD audio
 */

static bool SysCDStop(void *arg, uint8 lead_out_m, uint8 lead_out_s, uint8 lead_out_f)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;

#if defined(BINCUE)
	if (fh->is_bincue)
		return CDStop_bincue(fh->bincue_fd);
#endif


	if (fh->is_cdrom) {
#if defined(__linux__)
		return ioctl(fh->fd, CDROMSTOP) == 0;
#elif defined(__FreeBSD__) || defined(__NetBSD__)
		return ioctl(fh->fd, CDIOCSTOP) == 0;
#else
		return false;
#endif
	} else
		return false;
}


/*
 *  Perform CD audio fast-forward/fast-reverse operation starting from specified address
 */

static bool SysCDScan(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, bool reverse)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return false;
	
#if defined(BINCUE)
	if (fh->is_bincue)
		return CDScan_bincue(fh->bincue_fd,start_m,start_s,start_f,reverse);
#endif
	
	// Not supported outside bincue
	return false;
}


/*
 *  Set CD audio volume (0..255 each channel)
 */

static void SysCDSetVolume(void *arg, uint8 left, uint8 right)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;
	
#if defined(BINCUE)
	if (fh->is_bincue)
		CDSetVol_bincue(fh->bincue_fd,left,right);
#endif

	if (fh->is_cdrom) {
#if defined(__linux__)
		cdrom_volctrl vol;
		vol.channel0 = vol.channel2 = left;
		vol.channel1 = vol.channel3 = right;
		ioctl(fh->fd, CDROMVOLCTRL, &vol);
#elif defined(__FreeBSD__) || defined(__NetBSD__)
		struct ioc_vol vol;
		vol.vol[0] = vol.vol[2] = left;
		vol.vol[1] = vol.vol[3] = right;
		ioctl(fh->fd, CDIOCSETVOL, &vol);
#endif
	}
}


/*
 *  Get CD audio volume (0..255 each channel)
 */

static void SysCDGetVolume(void *arg, uint8 &left, uint8 &right)
{
	mac_file_handle *fh = (mac_file_handle *)arg;
	if (!fh)
		return;

	left = right = 0;
	
#if defined(BINCUE)
	if (fh->is_bincue)
		CDGetVol_bincue(fh->bincue_fd,&left,&right);
#endif
	
	if (fh->is_cdrom) {
#if defined(__linux__)
		cdrom_volctrl vol;
		ioctl(fh->fd, CDROMVOLREAD, &vol);
		left = vol.channel0;
		right = vol.channel1;
#elif defined(__FreeBSD__) || defined(__NetBSD__)
		struct ioc_vol vol;
		ioctl(fh->fd, CDIOCGETVOL, &vol);
		left = vol.vol[0];
		right = vol.vol[1];
#endif
	}
}
/*
 *  ExtFS Unix implementation
 *  Ported from BasiliskII (C) 1997-2008 Christian Bauer
 */

#include <utime.h>
#include "extfs.h"
#include "extfs_defs.h"
#include "cpu_emulation.h"

// Default Finder flags
const uint16 DEFAULT_FINDER_FLAGS = kHasBeenInited;

void extfs_init(void) {}
void extfs_exit(void) {}

void add_path_component(char *path, const char *component)
{
	int l = strlen(path);
	if (l < MAX_PATH_LENGTH-1 && path[l-1] != '/') {
		path[l] = '/';
		path[l+1] = 0;
	}
	strncat(path, component, MAX_PATH_LENGTH-1);
}

/*
 *  Finder info and resource forks are kept in helper files
 *
 *  Finder info:  /path/.finf/file
 *  Resource fork: /path/.rsrc/file
 *
 *  The .finf files store FInfo/DInfo + FXInfo/DXInfo (16+16 bytes)
 */

static void make_helper_path(const char *src, char *dest, const char *add, bool only_dir = false)
{
	dest[0] = 0;
	const char *last_part = strrchr(src, '/');
	if (last_part)
		last_part++;
	else
		last_part = src;
	strncpy(dest, src, last_part-src);
	dest[last_part-src] = 0;
	strncat(dest, add, MAX_PATH_LENGTH-1);
	if (!only_dir)
		strncat(dest, last_part, MAX_PATH_LENGTH-1);
}

static int create_helper_dir(const char *path, const char *add)
{
	char helper_dir[MAX_PATH_LENGTH];
	make_helper_path(path, helper_dir, add, true);
	if (helper_dir[strlen(helper_dir) - 1] == '/')
		helper_dir[strlen(helper_dir) - 1] = 0;
	return mkdir(helper_dir, 0777);
}

static int open_helper(const char *path, const char *add, int flag)
{
	char helper_path[MAX_PATH_LENGTH];
	make_helper_path(path, helper_path, add);
	if ((flag & O_ACCMODE) == O_RDWR || (flag & O_ACCMODE) == O_WRONLY)
		flag |= O_CREAT;
	int fd = open(helper_path, flag, 0666);
	if (fd < 0) {
		if (errno == ENOENT && (flag & O_CREAT)) {
			int ret = create_helper_dir(path, add);
			if (ret < 0)
				return ret;
			fd = open(helper_path, flag, 0666);
		}
	}
	return fd;
}

static int open_finf(const char *path, int flag)
{
	return open_helper(path, ".finf/", flag);
}

static int open_rsrc(const char *path, int flag)
{
	return open_helper(path, ".rsrc/", flag);
}

// Extension → Mac type/creator mapping
struct ext2type {
	const char *ext;
	uint32 type;
	uint32 creator;
};

static const ext2type e2t_translation[] = {
	{".Z", FOURCC('Z','I','V','M'), FOURCC('L','Z','I','V')},
	{".gz", FOURCC('G','z','i','p'), FOURCC('G','z','i','p')},
	{".hqx", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".bin", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".pdf", FOURCC('P','D','F',' '), FOURCC('C','A','R','O')},
	{".ps", FOURCC('T','E','X','T'), FOURCC('t','t','x','t')},
	{".sit", FOURCC('S','I','T','!'), FOURCC('S','I','T','x')},
	{".tar", FOURCC('T','A','R','F'), FOURCC('T','A','R',' ')},
	{".uu", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".uue", FOURCC('T','E','X','T'), FOURCC('S','I','T','x')},
	{".zip", FOURCC('Z','I','P',' '), FOURCC('Z','I','P',' ')},
	{".8svx", FOURCC('8','S','V','X'), FOURCC('S','N','D','M')},
	{".aifc", FOURCC('A','I','F','C'), FOURCC('T','V','O','D')},
	{".aiff", FOURCC('A','I','F','F'), FOURCC('T','V','O','D')},
	{".au", FOURCC('U','L','A','W'), FOURCC('T','V','O','D')},
	{".mid", FOURCC('M','I','D','I'), FOURCC('T','V','O','D')},
	{".midi", FOURCC('M','I','D','I'), FOURCC('T','V','O','D')},
	{".mp2", FOURCC('M','P','G',' '), FOURCC('T','V','O','D')},
	{".mp3", FOURCC('M','P','G',' '), FOURCC('T','V','O','D')},
	{".wav", FOURCC('W','A','V','E'), FOURCC('T','V','O','D')},
	{".bmp", FOURCC('B','M','P','f'), FOURCC('o','g','l','e')},
	{".gif", FOURCC('G','I','F','f'), FOURCC('o','g','l','e')},
	{".lbm", FOURCC('I','L','B','M'), FOURCC('G','K','O','N')},
	{".ilbm", FOURCC('I','L','B','M'), FOURCC('G','K','O','N')},
	{".jpg", FOURCC('J','P','E','G'), FOURCC('o','g','l','e')},
	{".jpeg", FOURCC('J','P','E','G'), FOURCC('o','g','l','e')},
	{".pict", FOURCC('P','I','C','T'), FOURCC('o','g','l','e')},
	{".png", FOURCC('P','N','G','f'), FOURCC('o','g','l','e')},
	{".sgi", FOURCC('.','S','G','I'), FOURCC('o','g','l','e')},
	{".tga", FOURCC('T','P','I','C'), FOURCC('o','g','l','e')},
	{".tif", FOURCC('T','I','F','F'), FOURCC('o','g','l','e')},
	{".tiff", FOURCC('T','I','F','F'), FOURCC('o','g','l','e')},
	{".htm", FOURCC('T','E','X','T'), FOURCC('M','O','S','S')},
	{".html", FOURCC('T','E','X','T'), FOURCC('M','O','S','S')},
	{".txt", FOURCC('T','E','X','T'), FOURCC('t','t','x','t')},
	{".rtf", FOURCC('T','E','X','T'), FOURCC('M','S','W','D')},
	{".c", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".C", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".cc", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".cpp", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".cxx", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".h", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".hh", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".hpp", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".hxx", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".s", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".S", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".i", FOURCC('T','E','X','T'), FOURCC('R','*','c','h')},
	{".mpg", FOURCC('M','P','E','G'), FOURCC('T','V','O','D')},
	{".mpeg", FOURCC('M','P','E','G'), FOURCC('T','V','O','D')},
	{".mov", FOURCC('M','o','o','V'), FOURCC('T','V','O','D')},
	{".fli", FOURCC('F','L','I',' '), FOURCC('T','V','O','D')},
	{".avi", FOURCC('V','f','W',' '), FOURCC('T','V','O','D')},
	{".qxd", FOURCC('X','D','O','C'), FOURCC('X','P','R','3')},
	{".hfv", FOURCC('D','D','i','m'), FOURCC('d','d','s','k')},
	{".dsk", FOURCC('D','D','i','m'), FOURCC('d','d','s','k')},
	{".img", FOURCC('r','o','h','d'), FOURCC('d','d','s','k')},
	{NULL, 0, 0}
};

void get_finfo(const char *path, uint32 finfo, uint32 fxinfo, bool is_dir)
{
	Mac_memset(finfo, 0, SIZEOF_FInfo);
	if (fxinfo)
		Mac_memset(fxinfo, 0, SIZEOF_FXInfo);
	WriteMacInt16(finfo + fdFlags, DEFAULT_FINDER_FLAGS);
	WriteMacInt32(finfo + fdLocation, (uint32)-1);

	int fd = open_finf(path, O_RDONLY);
	if (fd >= 0) {
		ssize_t actual = read(fd, Mac2HostAddr(finfo), SIZEOF_FInfo);
		if (fxinfo)
			actual += read(fd, Mac2HostAddr(fxinfo), SIZEOF_FXInfo);
		close(fd);
		if (actual >= SIZEOF_FInfo)
			return;
	}

	if (!is_dir) {
		int path_len = strlen(path);
		for (int i=0; e2t_translation[i].ext; i++) {
			int ext_len = strlen(e2t_translation[i].ext);
			if (path_len < ext_len)
				continue;
			if (!strcmp(path + path_len - ext_len, e2t_translation[i].ext)) {
				WriteMacInt32(finfo + fdType, e2t_translation[i].type);
				WriteMacInt32(finfo + fdCreator, e2t_translation[i].creator);
				break;
			}
		}
	}
}

void set_finfo(const char *path, uint32 finfo, uint32 fxinfo, bool is_dir)
{
	struct utimbuf times;
	times.actime = MacTimeToTime(ReadMacInt32(finfo - ioFlFndrInfo + ioFlCrDat));
	times.modtime = MacTimeToTime(ReadMacInt32(finfo - ioFlFndrInfo + ioFlMdDat));

	if (utime(path, &times) < 0) {
		D(bug("utime failed on %s\n", path));
	}

	int fd = open_finf(path, O_RDWR);
	if (fd < 0)
		return;
	write(fd, Mac2HostAddr(finfo), SIZEOF_FInfo);
	if (fxinfo)
		write(fd, Mac2HostAddr(fxinfo), SIZEOF_FXInfo);
	close(fd);
}

uint32 get_rfork_size(const char *path)
{
	int fd = open_rsrc(path, O_RDONLY);
	if (fd < 0)
		return 0;
	off_t size = lseek(fd, 0, SEEK_END);
	close(fd);
	return size < 0 ? 0 : size;
}

int open_rfork(const char *path, int flag)
{
	return open_rsrc(path, flag);
}

void close_rfork(const char *path, int fd)
{
	(void)path;
	close(fd);
}

ssize_t extfs_read(int fd, void *buffer, size_t length)
{
	return read(fd, buffer, length);
}

ssize_t extfs_write(int fd, void *buffer, size_t length)
{
	return write(fd, buffer, length);
}

bool extfs_remove(const char *path)
{
	char helper_path[MAX_PATH_LENGTH];
	make_helper_path(path, helper_path, ".finf/", false);
	remove(helper_path);
	make_helper_path(path, helper_path, ".rsrc/", false);
	remove(helper_path);

	if (remove(path) < 0) {
		if (errno == EISDIR || errno == ENOTEMPTY) {
			helper_path[0] = 0;
			strncpy(helper_path, path, MAX_PATH_LENGTH-1);
			add_path_component(helper_path, ".finf");
			rmdir(helper_path);
			helper_path[0] = 0;
			strncpy(helper_path, path, MAX_PATH_LENGTH-1);
			add_path_component(helper_path, ".rsrc");
			rmdir(helper_path);
			return rmdir(path) == 0;
		} else
			return false;
	}
	return true;
}

bool extfs_rename(const char *old_path, const char *new_path)
{
	char old_helper_path[MAX_PATH_LENGTH], new_helper_path[MAX_PATH_LENGTH];
	make_helper_path(old_path, old_helper_path, ".finf/", false);
	make_helper_path(new_path, new_helper_path, ".finf/", false);
	create_helper_dir(new_path, ".finf/");
	rename(old_helper_path, new_helper_path);
	make_helper_path(old_path, old_helper_path, ".rsrc/", false);
	make_helper_path(new_path, new_helper_path, ".rsrc/", false);
	create_helper_dir(new_path, ".rsrc/");
	rename(old_helper_path, new_helper_path);
	return rename(old_path, new_path) == 0;
}

const char *host_encoding_to_macroman(const char *filename)
{
	return filename;
}

const char *macroman_to_host_encoding(const char *filename)
{
	return filename;
}


/*
 *  Platform adapter wrappers
 *  These functions provide the platform_unix_* interface expected by the platform layer
 */

void platform_unix_mount_volume(const char *path)
{
	// TODO: Implement MountVolume
	(void)path;
}


void platform_unix_file_disk_layout(loff_t size, loff_t *start, loff_t *length)
{
	// TODO: Implement FileDiskLayout
	if (start) *start = 0;
	if (length) *length = size;
}

void platform_unix_floppy_init(void)
{
	// TODO: Implement FloppyInit
}

void platform_unix_sys_add_serial_prefs(void)
{
	SysAddSerialPrefs();
}

void platform_unix_sys_add_floppy_prefs(void)
{
	SysAddFloppyPrefs();
}

void platform_unix_sys_add_disk_prefs(void)
{
	SysAddDiskPrefs();
}

void platform_unix_sys_add_cdrom_prefs(void)
{
	SysAddCDROMPrefs();
}

void *platform_unix_sys_open(const char *path, bool read_only, bool no_cache)
{
	return Sys_open(path, read_only, no_cache);
}

void platform_unix_sys_close(void *fh)
{
	Sys_close(fh);
}

size_t platform_unix_sys_read(void *fh, void *buf, loff_t offset, size_t length)
{
	return Sys_read(fh, buf, offset, length);
}

size_t platform_unix_sys_write(void *fh, void *buf, loff_t offset, size_t length)
{
	return Sys_write(fh, buf, offset, length);
}

bool platform_unix_sys_is_readonly(void *fh)
{
	return SysIsReadOnly(fh);
}

bool platform_unix_sys_is_disk_inserted(void *fh)
{
	return SysIsDiskInserted(fh);
}

bool platform_unix_sys_is_fixed_disk(void *fh)
{
	return SysIsFixedDisk(fh);
}

loff_t platform_unix_sys_get_file_size(void *fh)
{
	return SysGetFileSize(fh);
}

void platform_unix_sys_eject(void *fh)
{
	SysEject(fh);
}

void platform_unix_sys_allow_removal(void *fh)
{
	SysAllowRemoval(fh);
}

void platform_unix_sys_prevent_removal(void *fh)
{
	SysPreventRemoval(fh);
}

bool platform_unix_sys_format(void *fh)
{
	return SysFormat(fh);
}

bool platform_unix_sys_cd_get_volume(void *fh, uint8 *left, uint8 *right)
{
	SysCDGetVolume(fh, *left, *right);  // SysCDGetVolume takes references
	return true;
}

bool platform_unix_sys_cd_set_volume(void *fh, uint8 left, uint8 right)
{
	SysCDSetVolume(fh, left, right);
	return true;
}

void platform_unix_sys_cd_pause(void *fh)
{
	SysCDPause(fh);
}

void platform_unix_sys_cd_resume(void *fh)
{
	SysCDResume(fh);
}

bool platform_unix_sys_cd_play(void *fh, uint8 m1, uint8 s1, uint8 f1, uint8 m2, uint8 s2, uint8 f2)
{
	return SysCDPlay(fh, m1, s1, f1, m2, s2, f2);
}

bool platform_unix_sys_cd_stop(void *fh, uint8 m, uint8 s, uint8 f)
{
	return SysCDStop(fh, m, s, f);
}

bool platform_unix_sys_cd_get_position(void *fh, uint8 *pos)
{
	return SysCDGetPosition(fh, pos);
}

bool platform_unix_sys_cd_scan(void *fh, uint8 m, uint8 s, uint8 f, bool reverse)
{
	return SysCDScan(fh, m, s, f, reverse);
}

bool platform_unix_sys_cd_read_toc(void *fh, uint8 *toc)
{
	return SysCDReadTOC(fh, toc);
}
