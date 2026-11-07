/*
 * image_io.cpp - disk images' files, POSIX or the opener's (image_io.h).
 */
#include <hfs/image_io.h>

#include <cerrno>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <rsys/win32_posix.h>
#endif

namespace Executor::image_io {

namespace {

/* The opener's descriptors: from here up (DOSFDBIT and ASPIFDBIT, hfs.h,
   are above; no process has this many files open) */
constexpr int FIRST = 0x10000000;

Opener opener;
std::map<int, std::unique_ptr<File>> files;
int next_fd = FIRST;

File *file(int fd)
{
    auto it = files.find(fd);
    return it == files.end() ? nullptr : it->second.get();
}

}

void set_opener(Opener o)
{
    opener = std::move(o);
}

int open(const char *path, int flags)
{
    if(opener)
    {
        if(auto f = opener(path, (flags & O_ACCMODE) != O_RDONLY))
        {
            int fd = next_fd++;
            files[fd] = std::move(f);
            return fd;
        }
    }
#ifdef _WIN32
    return win32_open_utf8(path, flags, 0000);
#else
    return ::open(path, flags, 0000);
#endif
}

bool is_virtual(int fd)
{
    return file(fd) != nullptr;
}

int close(int fd)
{
    auto it = files.find(fd);
    if(it == files.end())
        return ::close(fd);
    it->second->sync();
    files.erase(it);
    return 0;
}

ssize_t pread(int fd, void *buf, size_t n, uint64_t offset)
{
    if(File *f = file(fd))
        return f->pread(buf, n, offset);
#ifdef _WIN32
    return ::pread(fd, buf, n, (int64_t)offset);
#else
    return ::pread(fd, buf, n, (off_t)offset);
#endif
}

ssize_t pwrite(int fd, const void *buf, size_t n, uint64_t offset)
{
    if(File *f = file(fd))
        return f->pwrite(buf, n, offset);
#ifdef _WIN32
    return ::pwrite(fd, buf, n, (int64_t)offset);
#else
    return ::pwrite(fd, buf, n, (off_t)offset);
#endif
}

bool stat(int fd, uint64_t *size, bool *regular)
{
    if(File *f = file(fd))
    {
        *size = f->size();
        *regular = true;
        return true;
    }
#ifdef _WIN32
    struct _stat64 st;
    if(_fstat64(fd, &st) != 0)
#else
    struct stat st;
    if(fstat(fd, &st) != 0)
#endif
        return false;
    *regular = S_ISREG(st.st_mode);
    *size = (uint64_t)st.st_size;
    return true;
}

int fsync(int fd)
{
    if(File *f = file(fd))
        return f->sync() ? 0 : -1;
    return ::fsync(fd);
}

}  // namespace Executor::image_io
