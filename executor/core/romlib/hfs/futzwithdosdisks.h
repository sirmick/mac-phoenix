#pragma once

/*
 * Apologies for the strange file name.
 * Once I've figured out what it is really supposed to do,
 * I might rename it to something sensible, but for now it's named after
 * the strangest function it exports.   -- autc04, June 2018
 *
 * This code used to live in stdfile.h/stdfile.cpp, for no good reason at all.
 * 
 * It has nothing to do with the user interface for picking files.
 * Instead, it has to do with mounting HFS floppies and CD-ROMs.
 */

#include <base/common.h>
#include <hfs/drive_flags.h>

namespace Executor
{
    extern void futzwithdosdisks(void);
#if defined(__linux__)
    extern int linuxfloppy_open(int disk, LONGINT *bsizep,
                                drive_flags_t *flagsp, const char *dname);
    extern int linuxfloppy_close(int disk);
#endif
}
