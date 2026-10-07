#include <base/common.h>
#include <hfs/futzwithdosdisks.h>
#include <file/file.h>
#include <hfs/hfs.h>
#include <prefs/prefs.h>
#include <OSEvent.h>

#if defined(__linux__)
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#endif

using namespace Executor;

/*
 * Public functions:
 * 
 * linuxfloppy_open
 * linuxfloppy_close
 * futzwithdosdisks
 */

/*
 * This code used to live in stdfile.cpp, for no good reason at all.
 * 
 * It has nothing to do with the user interface for picking files.
 * Instead, it has to do with mounting HFS floppies and CD-ROMs.
 */

#if defined(__linux__)
int Executor::linuxfloppy_open(int disk, LONGINT *bsizep,
                               drive_flags_t *flagsp, const char *dname)
{
    int retval;
    struct cdrom_subchnl sub_info;
    bool force_read_only;

    force_read_only = false;
    *flagsp = 0;
#define FLOPPY_PREFIX "/dev/fd"
    if(strncmp(dname, FLOPPY_PREFIX, sizeof(FLOPPY_PREFIX) - 1) == 0)
        *flagsp |= DRIVE_FLAGS_FLOPPY;

    if(!force_read_only)
        retval = open(dname, O_RDWR, 0);
#if !defined(LETGCCWAIL)
    else
        retval = noErr;
#endif
    if(force_read_only || retval < 0)
    {
        retval = open(dname, O_RDONLY, 0);
        if(retval >= 0)
            *flagsp |= DRIVE_FLAGS_LOCKED;
    }

    memset(&sub_info, 0, sizeof sub_info);
    sub_info.cdsc_format = CDROM_MSF;
    if(retval >= 0 && ioctl(retval, CDROMSUBCHNL, &sub_info) != -1)
    {
        switch(sub_info.cdsc_audiostatus)
        {
            case CDROM_AUDIO_PLAY:
            case CDROM_AUDIO_PAUSED:
            case CDROM_AUDIO_COMPLETED:
            case CDROM_AUDIO_ERROR:
                close(retval);
                return -1;
        }
    }

    *bsizep = 512;
    if(retval >= 0)
    {
        /* *ejectablep = true; DRIVE_FLAGS_FIXED not set */
    }

    return retval;
}

int Executor::linuxfloppy_close(int disk)
{
    return close(disk);
}
#endif

typedef struct
{
    const char *dname;
    DrvQExtra *dqp;
    Boolean loaded;
    int fd;
} dosdriveinfo_t;

#define IGNORED (-1) /* doesn't really matter */

#if defined(MSDOS) || defined(CYGWIN32)

#define N_DRIVES (32)

static bool drive_loaded[N_DRIVES] = { 0 };

static char *
drive_name_of(int i)
{
    static char retval[] = "A:";

    retval[0] = 'A' + i;
    return retval;
}

enum
{
    NUM_FLOPPIES = 2,
    NON_FLOPPY_BIT = 0x80
};

static int
fd_of(int i)
{
    int retval;

#if defined(CYGWIN32)
    retval = i;
#else
    if(i < NUM_FLOPPIES)
        retval = i;
    else
        retval = i - NUM_FLOPPIES + NON_FLOPPY_BIT;
#endif
    return retval;
}

#define DRIVE_NAME_OF(x) drive_name_of(x)
#define FD_OF(x) fd_of(x)
#define DRIVE_LOADED(x) drive_loaded[x]

#else

#define DRIVE_NAME_OF(x) drives[x].dname

#define FD_OF(x) drives[x].fd

#define DRIVE_LOADED(x) drives[x].loaded

#endif

void Executor::futzwithdosdisks(void)
{
#if false
    int i, fd;
    GUEST<LONGINT> mess_s;
    LONGINT mess;
    LONGINT blocksize;
    drive_flags_t flags;
#if defined(MSDOS) || defined(CYGWIN32)
/* #warning "We're cheating on DOS drive specs: ejectable, bsize, maxsize, writable" */
#define OPEN_ROUTINE dosdisk_open
#define CLOSE_ROUTINE dosdisk_close
#define EXTRA_CLOSE_PARAM , false
#define MARKER DOSFDBIT
#define EXTRA_PARAM
#define ROMLIB_MACDRIVES ROMlib_macdrives
#elif defined(__linux__)
    static dosdriveinfo_t drives[] = {
        { "/dev/fd0", (DrvQExtra *)0, false, IGNORED },
        { "/dev/cdrom", (DrvQExtra *)0, false, IGNORED },
#if 0
	{ "/dev/fd1", (DrvQExtra *) 0, false, IGNORED },
#endif
    };
#define N_DRIVES std::size(drives)
#define OPEN_ROUTINE linuxfloppy_open
#define CLOSE_ROUTINE linuxfloppy_close
#define EXTRA_CLOSE_PARAM
#define MARKER 0
#define EXTRA_PARAM , (DRIVE_NAME_OF(i))
#define ROMLIB_MACDRIVES (~0)
#endif

    if(!nodrivesearch_p)
    {
        for(i = 0; i < N_DRIVES; ++i)
        {
            if(/* DRIVE_LOADED(i) */ ROMLIB_MACDRIVES & (1 << i))
            {
                if(((fd = OPEN_ROUTINE(FD_OF(i), &blocksize, &flags EXTRA_PARAM)) >= 0)
                   || (flags & DRIVE_FLAGS_FLOPPY))
                {
                    try_to_mount_disk(DRIVE_NAME_OF(i), fd | MARKER, &mess_s,
                                      blocksize, 16 * PHYSBSIZE,
                                      flags, 0);
                    mess = mess_s;
                    if(mess)
                    {
                        if(mess >> 16 == 0)
                        {
                            DRIVE_LOADED(i) = true;
                            PPostEvent(diskEvt, mess, (GUEST<EvQElPtr> *)0);
                            /* TODO: we probably should post if mess returns an
		     error, but I think we get confused if we do */
                        }
                        else
                        {
                            if(!(flags & DRIVE_FLAGS_FLOPPY))
                                CLOSE_ROUTINE(fd EXTRA_CLOSE_PARAM);
                        }
                    }
                    else
                    {
                        if(!(flags & DRIVE_FLAGS_FLOPPY))
                            CLOSE_ROUTINE(fd EXTRA_CLOSE_PARAM);
                    }
                }
            }
        }
    }
#endif
}