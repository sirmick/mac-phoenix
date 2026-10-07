#pragma once

// Microsoft supports POSIX's open/close/read/write/stat functions,
// but it's not *quite* compatible.

#ifdef _MSC_VER
#include <io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif
