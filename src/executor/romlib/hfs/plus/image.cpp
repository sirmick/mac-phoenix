#include "image.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

using namespace Executor::hfsplus;

bool ImageReader::zero(uint64_t offset, uint64_t n)
{
    static const uint8_t zeros[64 * 1024] = {};
    for(uint64_t end = offset + n; offset < end; offset += sizeof zeros)
        if(!write(offset, zeros, size_t(std::min<uint64_t>(sizeof zeros, end - offset))))
            return false;
    return true;
}

FdImageReader::FdImageReader(int fd, bool owns, bool writable)
    : fd_(fd), owns_(owns), writable_(writable)
{
    struct stat st;
    if(fstat(fd_, &st) == 0)
        size_ = (uint64_t)st.st_size;
    if(!size_)                                  /* a device: its end */
    {
        off_t end = lseek(fd_, 0, SEEK_END);
        if(end > 0)
            size_ = (uint64_t)end;
    }
}

FdImageReader::~FdImageReader()
{
    if(owns_ && fd_ >= 0)
        close(fd_);
}

std::shared_ptr<FdImageReader> FdImageReader::open(const std::string& path, std::string *error)
{
    int fd = ::open(path.c_str(), O_RDONLY | O_BINARY);
    if(fd < 0)
    {
        if(error)
            *error = path + ": " + strerror(errno);
        return nullptr;
    }
    return std::make_shared<FdImageReader>(fd, true);
}

bool FdImageReader::read(uint64_t offset, void *buf, size_t n)
{
    auto *p = static_cast<uint8_t *>(buf);
    while(n)
    {
        ssize_t got = pread(fd_, p, n, (off_t)offset);
        if(got < 0 && errno == EINTR)
            continue;
        if(got <= 0)
            return false;
        p += got;
        offset += (uint64_t)got;
        n -= (size_t)got;
    }
    return true;
}

bool FdImageReader::write(uint64_t offset, const void *buf, size_t n)
{
    if(!writable_)
        return false;
    auto *p = static_cast<const uint8_t *>(buf);
    while(n)
    {
        ssize_t put = pwrite(fd_, p, n, (off_t)offset);
        if(put < 0 && errno == EINTR)
            continue;
        if(put <= 0)
            return false;
        p += put;
        offset += (uint64_t)put;
        n -= (size_t)put;
    }
    return true;
}

bool FdImageReader::zero(uint64_t offset, uint64_t n)
{
    if(!writable_)
        return false;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if(offset + n <= size_
       && fallocate(fd_, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, (off_t)offset, (off_t)n) == 0)
        return true;
#endif
    return ImageReader::zero(offset, n);
}

void FdImageReader::sync()
{
#ifndef __EMSCRIPTEN__
    if(writable_)
        fsync(fd_);
#endif
}
