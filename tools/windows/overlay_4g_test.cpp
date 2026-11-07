// Offsets past 4 GB on Windows (cmake/ExecutorWindows.cmake's
// _FILE_OFFSET_BITS=64, host/overlay.cpp's truncate_fd): an overlay over a
// sparse 6 GB image, a write at 4.5 GB read back, the overlay file grown
// past 4 GB with a torn tail and cut back by load(). Under Wine:
//   tools/windows/overlay_4g_test.sh
#include "overlay.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <unistd.h>
#include <windows.h>
#include <winioctl.h>
using executor_host::Overlay;
static int fails;
#define CHECK(c) do { if(c) printf("ok   %s\n", #c); else { printf("FAIL %s\n", #c); fails++; } } while(0)
int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *img = "Z:\\work\\base.img", *ovl = "Z:\\work\\base.overlay";
    const long long G = 1ll << 30, size = 6 * G, mark = 5 * G + 12345, at = 4 * G + G / 2 + 777;
    DeleteFileA(img); DeleteFileA(ovl);
    int fd = _open(img, _O_RDWR | _O_CREAT | _O_BINARY, 0644);
    CHECK(_chsize_s(fd, size) == 0);
    struct stat st; fstat(fd, &st);
    CHECK(st.st_size == size);
    CHECK(lseek(fd, (off_t)mark, SEEK_SET) == mark);
    CHECK(write(fd, "MARKER", 6) == 6);
    _close(fd);
    FILE *f = fopen(img, "rb"); char b6[7] = {};
    CHECK(fseeko(f, (off_t)mark, SEEK_SET) == 0 && fread(b6, 1, 6, f) == 6 && !strcmp(b6, "MARKER"));
    fclose(f);

    std::string err;
    auto o = Overlay::open(img, ovl, true, &err);
    CHECK(o != nullptr);
    if(!o) { printf("%s\n", err.c_str()); return 1; }
    CHECK(o->size() == (uint64_t)size);
    char buf[16] = {};
    CHECK(o->pread(buf, 6, mark) == 6 && !memcmp(buf, "MARKER", 6));
    CHECK(o->pwrite("OVERLAY", 7, at) == 7);
    CHECK(o->sync());
    o.reset();
    o = Overlay::open(img, ovl, true, &err);
    CHECK(o != nullptr);
    memset(buf, 0, sizeof buf);
    CHECK(o->pread(buf, 7, at) == 7 && !memcmp(buf, "OVERLAY", 7));
    memset(buf, 0, sizeof buf);
    CHECK(o->pread(buf, 6, mark) == 6 && !memcmp(buf, "MARKER", 6));
    o.reset();
    // the image itself never written at the overlay's offset
    fd = _open(img, _O_RDONLY | _O_BINARY);
    lseek(fd, (off_t)at, SEEK_SET); memset(buf, 1, sizeof buf); read(fd, buf, 7);
    CHECK(!memcmp(buf, "\0\0\0\0\0\0\0", 7));
    _close(fd);
    // the overlay file grown past 4 GB with a torn (zero) tail: load() cuts
    // it back to its last commit (truncate_fd from > 4 GB)
    fd = _open(ovl, _O_RDWR | _O_BINARY);
    fstat(fd, &st); long long good = st.st_size;
    CHECK(_chsize_s(fd, 5 * G + 3) == 0 && fstat(fd, &st) == 0 && st.st_size == 5 * G + 3);
    _close(fd);
    o = Overlay::open(img, ovl, true, &err);
    CHECK(o != nullptr);
    memset(buf, 0, sizeof buf);
    CHECK(o && o->pread(buf, 7, at) == 7 && !memcmp(buf, "OVERLAY", 7));
    o.reset();
    fd = _open(ovl, _O_RDONLY | _O_BINARY);
    fstat(fd, &st);
    printf("     overlay %lld bytes after the cut (was %lld before the tail)\n", (long long)st.st_size, good);
    CHECK(st.st_size == good);
    _close(fd);
    DeleteFileA(img); DeleteFileA(ovl);
    printf("%d failed\n", fails);
    return fails != 0;
}
