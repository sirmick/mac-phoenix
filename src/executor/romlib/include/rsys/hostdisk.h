#pragma once

namespace Executor
{
/* The driver behind host-folder volumes (hostdisk.cpp); Basilisk II's .Disk
   uses the same unit. */
enum { kHostDiskRefNum = -63 };

void InitHostDiskDriver();

/* Opens .Disk and queues a fixed drive for a host folder; returns the drive
   number, 0 if that failed. */
INTEGER ROMlib_add_host_drive(const char *path);
}
