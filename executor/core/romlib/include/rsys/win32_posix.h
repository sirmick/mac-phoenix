#pragma once
/*
 * win32_posix.h - the POSIX file calls Executor's files use that the
 * Windows C runtime lacks or does differently (MacPhoenix, the Windows
 * build): positioned reads and writes, fsync, and open() of a UTF-8 path
 * in binary mode. Nothing on other systems.
 */
#ifdef _WIN32

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <sys/types.h>
#include <windows.h>
#undef pascal  /* minwindef.h: a calling convention; a name in Executor */

/* At OFFSET without moving the descriptor's position for anyone else's
   positioned calls: ReadFile/WriteFile with the offset in an OVERLAPPED */
inline ssize_t pread(int fd, void *buf, size_t n, int64_t offset)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if(h == INVALID_HANDLE_VALUE)
    {
        errno = EBADF;
        return -1;
    }
    OVERLAPPED ov = {};
    ov.Offset = (DWORD)offset;
    ov.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
    DWORD got = 0;
    if(!ReadFile(h, buf, (DWORD)n, &got, &ov))
    {
        if(GetLastError() == ERROR_HANDLE_EOF)
            return 0;
        errno = EIO;
        return -1;
    }
    return (ssize_t)got;
}

inline ssize_t pwrite(int fd, const void *buf, size_t n, int64_t offset)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if(h == INVALID_HANDLE_VALUE)
    {
        errno = EBADF;
        return -1;
    }
    OVERLAPPED ov = {};
    ov.Offset = (DWORD)offset;
    ov.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
    DWORD put = 0;
    if(!WriteFile(h, buf, (DWORD)n, &put, &ov))
    {
        errno = GetLastError() == ERROR_DISK_FULL ? ENOSPC : EIO;
        return -1;
    }
    return (ssize_t)put;
}

inline int fsync(int fd)
{
    return _commit(fd);
}

/* PATH in UTF-8 (what Executor's strings are), always binary */
inline int win32_open_utf8(const char *path, int flags, int mode = 0666)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if(n <= 0)
        return _open(path, flags | O_BINARY, mode);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w.data(), n);
    return _wopen(w.c_str(), flags | O_BINARY, mode);
}

inline std::wstring win32_wide(const std::string &utf8)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if(n > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
    return w;
}

/* rename(2) over an existing file (Windows' rename() refuses); neither
   may be open */
inline int win32_replace_file(const std::string &from, const std::string &to)
{
    if(MoveFileExW(win32_wide(from).c_str(), win32_wide(to).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 0;
    errno = EACCES;
    return -1;
}

inline int win32_unlink_utf8(const char *path)
{
    return _wunlink(win32_wide(path).c_str());
}

#endif
