#pragma once
/*
 * image_io.h - how the File Manager reads and writes disk images
 * (MacPhoenix): the POSIX file of the image, unless the front end's opener
 * gives it another (a front end that keeps the Mac's changes apart from the
 * image: frontends/desktop, its overlays, host/overlay.h).
 *
 * Images are file descriptors to the code that mounts them (drive queue
 * entries, VCBs, HFS+'s FdImageReader); an opener's file gets one too, from
 * a range no real descriptor reaches, and these calls route each to its
 * file. Without an opener, or for a path it declines, they are the POSIX
 * calls they replace: every other front end (the tab, mac-phoenix's child)
 * is unchanged. Not thread-safe beyond what the File Manager needs: one
 * thread at a time.
 */
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>
#ifdef _WIN32
/* mingw's _FILE_OFFSET_BITS=64 makes stat a macro (_stat64): defined
   before stat() below in every file, so all of them name the same function */
#include <sys/stat.h>
#endif

namespace Executor::image_io {

class File
{
public:
    virtual ~File() = default;
    /* Bytes at OFFSET: how many (fewer at the end), -1 on an error */
    virtual ssize_t pread(void *buf, size_t n, uint64_t offset) = 0;
    virtual ssize_t pwrite(const void *buf, size_t n, uint64_t offset) = 0;
    virtual uint64_t size() = 0;
    /* What was written, made lasting (fsync) */
    virtual bool sync() = 0;
};

/* PATH opened for the File Manager, WRITABLE or not; nullptr: not the
   opener's (POSIX opens it). */
using Opener = std::function<std::unique_ptr<File>(const std::string &path, bool writable)>;
void set_opener(Opener opener);

/* As open(2) with FLAGS (O_RDONLY or O_RDWR, and O_BINARY, O_EXCL): a
   descriptor, real or the opener's; -1 and errno. */
int open(const char *path, int flags);
bool is_virtual(int fd);
int close(int fd);
ssize_t pread(int fd, void *buf, size_t n, uint64_t offset);
ssize_t pwrite(int fd, const void *buf, size_t n, uint64_t offset);
/* Its size and whether it is a plain file (an image, not a device);
   false: neither known */
bool stat(int fd, uint64_t *size, bool *regular);
int fsync(int fd);

}  // namespace Executor::image_io
