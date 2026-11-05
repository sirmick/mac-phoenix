/* files.c — the File Manager, on the boot volume (its root) and on the
 * volume the application is on (its folder; a disk image in
 * tools/bench/run.py), if that is another: 4 MB written and read back in
 * 4 KB and 64 KB blocks (open to close), 1,000 opens and closes, 200
 * creates and deletes. */

#include "bench.h"

#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <string.h>

#pragma segment Files

enum { kFileSize = 4L * 1024 * 1024, kBufSize = 65536 };

static short vol;
static long dir;
static char *buf;
static const unsigned char kName[] = "\pMacPhoenixBench.tmp";

static long vol_setup(int boot)
{
    HParamBlockRec pb;
    FSSpec spec;
    long sysdir;
    short sysvol;

    if(boot)
    {
        if(FindFolder(kOnSystemDisk, kSystemFolderType, false, &sysvol, &sysdir) != noErr)
        {
            bench_skip("no System Folder");
            return 1;
        }
        vol = sysvol;
        dir = fsRtDirID;
    }
    else
    {
        if(FindFolder(kOnSystemDisk, kSystemFolderType, false, &sysvol, &sysdir) == noErr
           && sysvol == bench_vref)
        {
            bench_skip("on the boot volume");
            return 1;
        }
        vol = bench_vref;
        dir = bench_dirid;
    }
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioVRefNum = vol;
    if(PBHGetVInfoSync(&pb) != noErr)
    {
        bench_skip("no volume information");
        return 1;
    }
    if((unsigned long)pb.volumeParam.ioVFrBlk * pb.volumeParam.ioVAlBlkSiz < kFileSize + 1024L * 1024)
    {
        bench_skip("not 5 MB free");
        return 1;
    }
    if((buf = NewPtr(kBufSize)) == NULL)
    {
        bench_skip("no memory");
        return 1;
    }
    memset(buf, 0x5A, kBufSize);
    FSMakeFSSpec(vol, dir, kName, &spec);
    FSpDelete(&spec);
    if(FSpCreate(&spec, 'MPbn', 'BINA', 0) != noErr)
    {
        DisposePtr(buf);
        bench_skip("can't create a file");
        return 1;
    }
    return 0;
}

static long setup_boot(void) { return vol_setup(1); }
static long setup_disk(void) { return vol_setup(0); }

static long vol_done(void)
{
    FSSpec spec;

    FSMakeFSSpec(vol, dir, kName, &spec);
    FSpDelete(&spec);
    DisposePtr(buf);
    return 0;
}

static long transfer(long block, int write)
{
    FSSpec spec;
    short ref;
    long n, count, total = 0;

    FSMakeFSSpec(vol, dir, kName, &spec);
    if(FSpOpenDF(&spec, fsRdWrPerm, &ref) != noErr)
        return -1;
    for(n = 0; n < kFileSize / block; n++)
    {
        count = block;
        if((write ? FSWrite(ref, &count, buf) : FSRead(ref, &count, buf)) != noErr)
            break;
        total += count;
    }
    FSClose(ref);
    FlushVol(NULL, vol);
    return total;
}

static long write_4k(void) { return transfer(4096, 1); }
static long write_64k(void) { return transfer(65536, 1); }
static long read_4k(void) { return transfer(4096, 0); }
static long read_64k(void) { return transfer(65536, 0); }

static long openclose(void)
{
    FSSpec spec;
    short ref;
    int i;

    FSMakeFSSpec(vol, dir, kName, &spec);
    for(i = 0; i < 1000; i++)
        if(FSpOpenDF(&spec, fsRdPerm, &ref) == noErr)
            FSClose(ref);
    return 0;
}

static long createdelete(void)
{
    FSSpec spec;
    int i;

    FSMakeFSSpec(vol, dir, "\pMacPhoenixBench.new", &spec);
    for(i = 0; i < 200; i++)
    {
        FSpCreate(&spec, 'MPbn', 'BINA', 0);
        FSpDelete(&spec);
    }
    return 0;
}

/* the read tests read what the write tests wrote: written first, outside
 * the timing */
static long written(long setup)
{
    if(setup)
        return 1;
    if(transfer(65536, 1) != kFileSize)
    {
        vol_done();
        bench_skip("can't write the file");
        return 1;
    }
    return 0;
}

static long setup_boot_r(void) { return written(setup_boot()); }
static long setup_disk_r(void) { return written(setup_disk()); }

void add_file_tests(void)
{
    bench_add("file_write_4k_boot", "4MB", write_4k, setup_boot, vol_done);
    bench_add("file_write_64k_boot", "4MB", write_64k, setup_boot, vol_done);
    bench_add("file_read_4k_boot", "4MB", read_4k, setup_boot_r, vol_done);
    bench_add("file_read_64k_boot", "4MB", read_64k, setup_boot_r, vol_done);
    bench_add("file_openclose_boot", "1000", openclose, setup_boot, vol_done);
    bench_add("file_create_boot", "200", createdelete, setup_boot, vol_done);
    bench_add("file_write_4k_disk", "4MB", write_4k, setup_disk, vol_done);
    bench_add("file_write_64k_disk", "4MB", write_64k, setup_disk, vol_done);
    bench_add("file_read_4k_disk", "4MB", read_4k, setup_disk_r, vol_done);
    bench_add("file_read_64k_disk", "4MB", read_64k, setup_disk_r, vol_done);
    bench_add("file_openclose_disk", "1000", openclose, setup_disk, vol_done);
    bench_add("file_create_disk", "200", createdelete, setup_disk, vol_done);
}
