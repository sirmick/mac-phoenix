/*
 * image_io.cpp - disk images' files, POSIX or the opener's (image_io.h).
 */
#include <hfs/image_io.h>

#include <cerrno>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

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
    return ::open(path, flags, 0000);
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
    return ::pread(fd, buf, n, (off_t)offset);
}

ssize_t pwrite(int fd, const void *buf, size_t n, uint64_t offset)
{
    if(File *f = file(fd))
        return f->pwrite(buf, n, offset);
    return ::pwrite(fd, buf, n, (off_t)offset);
}

bool stat(int fd, uint64_t *size, bool *regular)
{
    if(File *f = file(fd))
    {
        *size = f->size();
        *regular = true;
        return true;
    }
    struct stat st;
    if(fstat(fd, &st) != 0)
        return false;
    *regular = S_ISREG(st.st_mode);
    *size = (uint64_t)st.st_size;
    return true;
}

int fsync(int fd)
{
    if(File *f = file(fd))
        return f->sync() ? 0 : -1;
#ifdef _WIN32
    return 0;
#else
    return ::fsync(fd);
#endif
}

}  // namespace Executor::image_io
