/* MacPhoenix: StdCLib's input and output for PowerPC fragments
 * (docs/executor/PPCLibs.md): stdio.h and the MPW low-level calls (fcntl.h,
 * ioctl.h: open, read, write, lseek, faccess). Files go through the File
 * Manager, by Mac path (full, or partial from the default directory). The
 * console streams have no window: what goes to stdout and stderr is
 * Executor's log, line by line; stdin is at its end.
 *
 * A FILE is the structure stdio.h declares, in _iob[40]: getc and putc are
 * macros on its _cnt and _ptr, calling _filbuf and _flsbuf when the buffer
 * runs out. Reading, _cnt counts what is left in the buffer; writing fully
 * buffered, the room left; line-buffered or unbuffered it stays 0, so every
 * character comes here. '\n' is CR (13) in MPW's C. */

#include "clib.h"

#include <base/traps.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <ScriptMgr.h>
#include <ToolboxEvent.h>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace Executor;
using namespace Executor::clib;

namespace
{

const char LIB[] = "StdCLib";

constexpr int NFILE = 40;
constexpr int BUFSIZ_ = 1024;
constexpr int EOF_ = -1;

/* _flag (stdio.h) */
constexpr uint16_t IOFBF = 0x00, IOREAD = 0x01, IOWRT = 0x02, IONBF = 0x04, IOMYBUF = 0x08,
                   IOEOF = 0x10, IOERR = 0x20, IOLBF = 0x40, IORW = 0x80;

/* open() (fcntl.h) */
constexpr int O_RDONLY_ = 0, O_WRONLY_ = 1, O_RDWR_ = 2, O_APPEND_ = 8, O_RSRC_ = 0x10,
              O_CREAT_ = 0x100, O_TRUNC_ = 0x200, O_EXCL_ = 0x400;

/* struct FILE: _cnt, _ptr, _base, _end, _size, _flag, _file; 24 bytes. */
alignas(8) uint8_t iob_[NFILE][24];
alignas(8) uint8_t lastbuf_[4];
alignas(8) uint8_t buffers_[NFILE][BUFSIZ_];
alignas(8) uint8_t unbufChar_[NFILE][4];
alignas(8) uint8_t specBuf_[80];         /* an FSSpec the File Manager fills */
alignas(8) uint8_t specBuf2_[80];
alignas(8) uint8_t cell_[8];             /* counts and positions */
alignas(8) uint8_t nameBuf_[260];
alignas(8) uint8_t finfo_[16];
alignas(8) char tmpnamBuf_[32];

struct FileState
{
    bool writing = false;   /* the buffer holds output */
    bool deleteOnClose = false;
    std::string tmpPath;
};
FileState files[NFILE];

/* Descriptors. */
struct Fd
{
    bool open = false;
    int console = -1;       /* 0 stdin, 1 stdout, 2 stderr */
    int16_t refNum = 0;
    int flags = 0;
};
constexpr int NFD = 64;
Fd fds[NFD];
std::string consoleLine[3];

/* ---- FILE fields ---- */

uint32_t fileAddr(int i) { return guest(iob_[i]); }

int fileIndex(uint32_t f)
{
    uint32_t base = guest(iob_);
    if(f < base || f >= base + NFILE * 24 || (f - base) % 24)
        return -1;
    return int((f - base) / 24);
}

int32_t cnt(uint32_t f) { return (int32_t)read32(f); }
void setCnt(uint32_t f, int32_t v) { write32(f, (uint32_t)v); }
uint32_t ptr(uint32_t f) { return read32(f + 4); }
void setPtr(uint32_t f, uint32_t v) { write32(f + 4, v); }
uint32_t base(uint32_t f) { return read32(f + 8); }
void setBase(uint32_t f, uint32_t v) { write32(f + 8, v); }
void setEnd(uint32_t f, uint32_t v) { write32(f + 12, v); }
uint16_t size(uint32_t f) { return read16(f + 16); }
void setSize(uint32_t f, uint16_t v) { write16(f + 16, v); }
uint16_t flag(uint32_t f) { return read16(f + 18); }
void setFlag(uint32_t f, uint16_t v) { write16(f + 18, v); }
void orFlag(uint32_t f, uint16_t v) { setFlag(f, flag(f) | v); }
int fileFd(uint32_t f) { return (int16_t)read16(f + 20); }
void setFileFd(uint32_t f, int fd) { write16(f + 20, (uint16_t)fd); }

bool inUse(uint32_t f) { return flag(f) & (IOREAD | IOWRT | IORW); }

/* ---- descriptors ---- */

int newFd(int from = 0)
{
    for(int i = std::max(from, 0); i < NFD; i++)
        if(!fds[i].open)
            return i;
    return -1;
}

void consoleWrite(int which, const uint8_t *p, uint32_t n)
{
    std::string &line = consoleLine[which];
    for(uint32_t i = 0; i < n; i++)
    {
        char c = (char)p[i];
        if(c == '\r' || c == '\n')
        {
            fprintf(stderr, "[%s] %s\n", which == 2 ? "stderr" : "stdout", line.c_str());
            line.clear();
        }
        else
            line += c;
    }
}

int32_t fdWrite(int fd, uint32_t buf, uint32_t n)
{
    if(fd < 0 || fd >= NFD || !fds[fd].open)
    {
        setErrno(9);
        return -1;
    }
    Fd &d = fds[fd];
    if(d.console >= 0)
    {
        if(d.console == 0)
        {
            setErrno(9);
            return -1;
        }
        consoleWrite(d.console, host(buf), n);
        return (int32_t)n;
    }
    if((d.flags & 3) == O_RDONLY_)
    {
        setErrno(9);
        return -1;
    }
    if(d.flags & O_APPEND_)
        SetFPos(d.refNum, fsFromLEOF, 0);
    if(!n)
        return 0;
    GUEST<LONGINT> *count = (GUEST<LONGINT> *)cell_;
    *count = (LONGINT)n;
    OSErr err = FSWrite(d.refNum, count, host(buf));
    if(err != noErr)
    {
        setMacOSErr(err);
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    return (int32_t)*count;
}

int32_t fdRead(int fd, uint32_t buf, uint32_t n)
{
    if(fd < 0 || fd >= NFD || !fds[fd].open)
    {
        setErrno(9);
        return -1;
    }
    Fd &d = fds[fd];
    if(d.console >= 0)
        return 0;
    if((d.flags & 3) == O_WRONLY_)
    {
        setErrno(9);
        return -1;
    }
    if(!n)
        return 0;
    GUEST<LONGINT> *count = (GUEST<LONGINT> *)cell_;
    *count = (LONGINT)n;
    OSErr err = FSRead(d.refNum, count, host(buf));
    if(err != noErr && err != eofErr)
    {
        setMacOSErr(err);
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    return (int32_t)*count;
}

int32_t fdSeek(int fd, int32_t offset, int whence)
{
    if(fd < 0 || fd >= NFD || !fds[fd].open)
    {
        setErrno(9);
        return -1;
    }
    Fd &d = fds[fd];
    if(d.console >= 0)
    {
        setErrno(29);   /* ESPIPE */
        return -1;
    }
    int16_t mode = whence == 0 ? fsFromStart : whence == 1 ? fsFromMark : whence == 2 ? fsFromLEOF : -1;
    if(mode < 0)
    {
        setErrno(22);
        return -1;
    }
    GUEST<LONGINT> *pos = (GUEST<LONGINT> *)cell_;
    LONGINT target;
    if(mode == fsFromStart)
        target = offset;
    else if(mode == fsFromMark)
    {
        GetFPos(d.refNum, pos);
        target = *pos + offset;
    }
    else
    {
        GetEOF(d.refNum, pos);
        target = *pos + offset;
    }
    if(target < 0)
    {
        setErrno(22);
        return -1;
    }
    /* Past the end: the file grows, as lseek and a write would have it. */
    GetEOF(d.refNum, pos);
    if(target > *pos && (d.flags & 3) != O_RDONLY_)
        SetEOF(d.refNum, target);
    OSErr err = SetFPos(d.refNum, fsFromStart, target);
    if(err != noErr && err != eofErr)
    {
        setMacOSErr(err);
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    GetFPos(d.refNum, pos);
    return *pos;
}

int fdClose(int fd)
{
    if(fd < 0 || fd >= NFD || !fds[fd].open)
    {
        setErrno(9);
        return -1;
    }
    Fd &d = fds[fd];
    int result = 0;
    if(d.console < 0)
    {
        /* Descriptors made by dup share the File Manager's path. */
        int sharing = 0;
        for(int i = 0; i < NFD; i++)
            if(i != fd && fds[i].open && fds[i].console < 0 && fds[i].refNum == d.refNum)
                sharing++;
        if(!sharing)
        {
            OSErr err = FSClose(d.refNum);
            if(err != noErr)
            {
                setMacOSErr(err);
                setErrno(errnoFromOSErr(err));
                result = -1;
            }
        }
    }
    else if(d.console > 0 && !consoleLine[d.console].empty())
    {
        uint8_t cr = '\r';
        consoleWrite(d.console, &cr, 1);
    }
    d = Fd();
    return result;
}

/* A C path as an FSSpec in specBuf_ (or `into`). */
OSErr makeSpec(const char *path, uint8_t *into = specBuf_)
{
    size_t n = strlen(path);
    if(n > 255)
        return bdNamErr;
    nameBuf_[0] = (uint8_t)n;
    memcpy(nameBuf_ + 1, path, n);
    return C_FSMakeFSSpec(0, 0, nameBuf_, (FSSpecPtr)into);
}

int fdOpen(const char *path, int oflag)
{
    int fd = newFd(3);
    if(fd < 0)
    {
        setErrno(24);
        return -1;
    }
    OSErr err = makeSpec(path);
    if(err == fnfErr && (oflag & O_CREAT_))
    {
        /* As Apple's StdCLib in an application: SimpleText's. */
        err = C_FSpCreate((FSSpecPtr)specBuf_, "ttxt"_4, "TEXT"_4, smSystemScript);
        if(err == noErr)
            oflag &= ~O_EXCL_;
    }
    else if(err == noErr && (oflag & O_CREAT_) && (oflag & O_EXCL_))
    {
        setErrno(17);
        return -1;
    }
    if(err != noErr)
    {
        setMacOSErr(err);
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    SignedByte perm = (oflag & 3) == O_RDONLY_ ? fsRdPerm : fsRdWrPerm;
    GUEST<int16_t> *ref = (GUEST<int16_t> *)cell_;
    err = (oflag & O_RSRC_) ? C_FSpOpenRF((FSSpecPtr)specBuf_, perm, ref)
                            : C_FSpOpenDF((FSSpecPtr)specBuf_, perm, ref);
    if(err != noErr)
    {
        setMacOSErr(err);
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    Fd &d = fds[fd];
    d.open = true;
    d.console = -1;
    d.refNum = *ref;
    d.flags = oflag;
    if((oflag & O_TRUNC_) && (oflag & 3) != O_RDONLY_)
        SetEOF(d.refNum, 0);
    return fd;
}

/* ---- buffers ---- */

void ensureBuffer(uint32_t f)
{
    if(base(f))
        return;
    int i = fileIndex(f);
    if(flag(f) & IONBF)
    {
        setBase(f, guest(unbufChar_[i]));
        setSize(f, 1);
    }
    else
    {
        setBase(f, guest(buffers_[i]));
        setSize(f, BUFSIZ_);
        orFlag(f, IOMYBUF);
    }
    setEnd(f, base(f) + size(f));
    setPtr(f, base(f));
    setCnt(f, 0);
}

/* Output in the buffer goes to the file. */
int flushOut(uint32_t f)
{
    int i = fileIndex(f);
    int result = 0;
    if(files[i].writing && base(f))
    {
        uint32_t n = ptr(f) - base(f);
        if(n && fdWrite(fileFd(f), base(f), n) != (int32_t)n)
        {
            orFlag(f, IOERR);
            result = EOF_;
        }
        setPtr(f, base(f));
        bool full = !(flag(f) & (IOLBF | IONBF));
        setCnt(f, full ? size(f) : 0);
    }
    return result;
}

/* Input read ahead is given back: the file's mark moves back over it. */
void dropIn(uint32_t f)
{
    int i = fileIndex(f);
    if(!files[i].writing && cnt(f) > 0)
    {
        Fd &d = fds[fileFd(f)];
        if(d.open && d.console < 0)
            fdSeek(fileFd(f), -cnt(f), 1);
    }
    if(!files[i].writing)
    {
        setCnt(f, 0);
        if(base(f))
            setPtr(f, base(f));
    }
}

int filbuf(uint32_t f)
{
    int i = fileIndex(f);
    if(i < 0 || !(flag(f) & (IOREAD | IORW)))
    {
        if(i >= 0)
            orFlag(f, IOERR);
        return EOF_;
    }
    if(files[i].writing)
    {
        flushOut(f);
        files[i].writing = false;
    }
    /* Reading from the console: what was written to it goes out first. */
    if(fds[fileFd(f)].console == 0)
        flushOut(fileAddr(1));
    ensureBuffer(f);
    int32_t n = fdRead(fileFd(f), base(f), size(f));
    if(n <= 0)
    {
        orFlag(f, n == 0 ? IOEOF : IOERR);
        setCnt(f, 0);
        setPtr(f, base(f));
        return EOF_;
    }
    setPtr(f, base(f) + 1);
    setCnt(f, n - 1);
    return host(base(f))[0];
}

int flsbuf(int c, uint32_t f)
{
    int i = fileIndex(f);
    if(i < 0 || !(flag(f) & (IOWRT | IORW)))
    {
        if(i >= 0)
            orFlag(f, IOERR);
        return EOF_;
    }
    if(!files[i].writing)
    {
        dropIn(f);
        files[i].writing = true;
        if(base(f))
            setPtr(f, base(f));
    }
    ensureBuffer(f);
    uint32_t used = ptr(f) - base(f);
    if(used >= size(f))
    {
        if(flushOut(f))
            return EOF_;
        used = 0;
    }
    host(ptr(f))[0] = (uint8_t)c;
    setPtr(f, ptr(f) + 1);
    used++;
    uint16_t fl = flag(f);
    if((fl & IONBF) || ((fl & IOLBF) && (c == '\r' || used >= size(f))) || used >= size(f))
    {
        if(flushOut(f))
            return EOF_;
    }
    else
        setCnt(f, (fl & IOLBF) ? 0 : int32_t(size(f) - used));
    return (uint8_t)c;
}

/* getc and putc as the macros have them. */
int getC(uint32_t f)
{
    int32_t n = cnt(f) - 1;
    setCnt(f, n);
    if(n >= 0)
    {
        uint32_t p = ptr(f);
        setPtr(f, p + 1);
        return host(p)[0];
    }
    setCnt(f, 0);
    return filbuf(f);
}

int putC(int c, uint32_t f)
{
    int32_t n = cnt(f) - 1;
    setCnt(f, n);
    if(n >= 0)
    {
        uint32_t p = ptr(f);
        setPtr(f, p + 1);
        host(p)[0] = (uint8_t)c;
        return (uint8_t)c;
    }
    setCnt(f, 0);
    return flsbuf(c, f);
}

int ungetC(int c, uint32_t f)
{
    int i = fileIndex(f);
    if(c == EOF_ || i < 0 || !(flag(f) & (IOREAD | IORW)))
        return EOF_;
    if(files[i].writing)
    {
        flushOut(f);
        files[i].writing = false;
        setCnt(f, 0);
    }
    ensureBuffer(f);
    if(ptr(f) <= base(f))
    {
        if(cnt(f) > 0)
        {
            if((uint32_t)cnt(f) >= size(f))
                return EOF_;
            /* Make room at the front. */
            memmove(host(base(f)) + 1, host(base(f)), cnt(f));
            setPtr(f, base(f) + 1);
        }
        else
            setPtr(f, base(f) + 1);
    }
    setPtr(f, ptr(f) - 1);
    host(ptr(f))[0] = (uint8_t)c;
    setCnt(f, cnt(f) + 1);
    setFlag(f, flag(f) & ~IOEOF);
    return (uint8_t)c;
}

/* ---- streams ---- */

uint32_t findFile()
{
    for(int i = 3; i < NFILE; i++)
        if(!inUse(fileAddr(i)))
            return fileAddr(i);
    return 0;
}

bool parseMode(const char *mode, int &oflag, uint16_t &fl)
{
    bool plus = strchr(mode, '+') != nullptr;
    switch(mode[0])
    {
        case 'r': oflag = plus ? O_RDWR_ : O_RDONLY_; fl = plus ? IORW : IOREAD; break;
        case 'w': oflag = (plus ? O_RDWR_ : O_WRONLY_) | O_CREAT_ | O_TRUNC_; fl = plus ? IORW : IOWRT; break;
        case 'a': oflag = (plus ? O_RDWR_ : O_WRONLY_) | O_CREAT_ | O_APPEND_; fl = plus ? IORW : IOWRT; break;
        default: return false;
    }
    return true;
}

void setupFile(uint32_t f, int fd, uint16_t fl)
{
    int i = fileIndex(f);
    memset(iob_[i], 0, 24);
    files[i] = FileState();
    setFileFd(f, fd);
    setFlag(f, fl);
}

uint32_t openStream(const char *path, const char *mode, uint32_t f)
{
    int oflag;
    uint16_t fl;
    if(!parseMode(mode, oflag, fl))
    {
        setErrno(22);
        return 0;
    }
    int fd = fdOpen(path, oflag);
    if(fd < 0)
        return 0;
    setupFile(f, fd, fl);
    return f;
}

int closeStream(uint32_t f)
{
    int i = fileIndex(f);
    if(i < 0 || !inUse(f))
        return EOF_;
    int result = flushOut(f);
    if(fdClose(fileFd(f)) < 0)
        result = EOF_;
    if(files[i].deleteOnClose && makeSpec(files[i].tmpPath.c_str()) == noErr)
        C_FSpDelete((FSSpecPtr)specBuf_);
    memset(iob_[i], 0, 24);
    files[i] = FileState();
    return result;
}

int flushStream(uint32_t f)
{
    int i = fileIndex(f);
    if(i < 0 || !inUse(f))
        return EOF_;
    if(files[i].writing)
        return flushOut(f);
    dropIn(f);
    return 0;
}

int seekStream(uint32_t f, int32_t offset, int whence)
{
    int i = fileIndex(f);
    if(i < 0 || !inUse(f))
        return -1;
    if(whence == 1 && !files[i].writing)
        offset -= cnt(f);
    if(files[i].writing)
        flushOut(f);
    else
    {
        setCnt(f, 0);
        if(base(f))
            setPtr(f, base(f));
    }
    if(flag(f) & IORW)
        files[i].writing = false;
    setFlag(f, flag(f) & ~IOEOF);
    return fdSeek(fileFd(f), offset, whence) < 0 ? -1 : 0;
}

int32_t tellStream(uint32_t f)
{
    int i = fileIndex(f);
    if(i < 0 || !inUse(f))
        return -1;
    int32_t pos = fdSeek(fileFd(f), 0, 1);
    if(pos < 0)
        return -1;
    if(files[i].writing)
        return pos + int32_t(base(f) ? ptr(f) - base(f) : 0);
    return pos - std::max<int32_t>(cnt(f), 0);
}

/* ---- the exports ---- */

int32_t c_filbuf(uint32_t f) { return filbuf(f); }
int32_t c_flsbuf(uint32_t c, uint32_t f) { return flsbuf((uint8_t)c, f); }
int32_t c_fgetc(uint32_t f) { return fileIndex(f) < 0 ? EOF_ : getC(f); }
int32_t c_fputc(int32_t c, uint32_t f) { return fileIndex(f) < 0 ? EOF_ : putC((uint8_t)c, f); }
int32_t c_getchar() { return getC(fileAddr(0)); }
int32_t c_putchar(int32_t c) { return putC((uint8_t)c, fileAddr(1)); }
int32_t c_ungetc(int32_t c, uint32_t f) { return ungetC(c, f); }

char *c_fgets(char *s, int32_t n, uint32_t f)
{
    if(n <= 0 || fileIndex(f) < 0)
        return nullptr;
    int32_t i = 0;
    while(i < n - 1)
    {
        int c = getC(f);
        if(c == EOF_)
            break;
        s[i++] = (char)c;
        if(c == '\r')
            break;
    }
    if(i == 0)
        return nullptr;
    s[i] = 0;
    return s;
}

char *c_gets(char *s)
{
    uint32_t f = fileAddr(0);
    int32_t i = 0;
    int c;
    while((c = getC(f)) != EOF_ && c != '\r')
        s[i++] = (char)c;
    if(c == EOF_ && i == 0)
        return nullptr;
    s[i] = 0;
    return s;
}

int32_t c_fputs(const char *s, uint32_t f)
{
    if(fileIndex(f) < 0)
        return EOF_;
    for(; *s; s++)
        if(putC((uint8_t)*s, f) == EOF_)
            return EOF_;
    return 0;
}

int32_t c_puts(const char *s)
{
    uint32_t f = fileAddr(1);
    if(c_fputs(s, f) == EOF_ || putC('\r', f) == EOF_)
        return EOF_;
    return 0;
}

int32_t c_getw(uint32_t f)
{
    uint32_t w = 0;
    for(int i = 0; i < 4; i++)
    {
        int c = getC(f);
        if(c == EOF_)
            return EOF_;
        w = (w << 8) | (uint32_t)c;
    }
    return (int32_t)w;
}

int32_t c_putw(int32_t w, uint32_t f)
{
    for(int i = 3; i >= 0; i--)
        if(putC((uint8_t)((uint32_t)w >> (8 * i)), f) == EOF_)
            return EOF_;
    return 0;
}

uint32_t c_fread(uint32_t dst, uint32_t sz, uint32_t n, uint32_t f)
{
    if(!sz || !n || fileIndex(f) < 0)
        return 0;
    uint64_t total = (uint64_t)sz * n, done = 0;
    uint8_t *out = host(dst);
    while(done < total)
    {
        if(cnt(f) > 0 && !files[fileIndex(f)].writing)
        {
            uint32_t take = (uint32_t)std::min<uint64_t>((uint64_t)cnt(f), total - done);
            memcpy(out + done, host(ptr(f)), take);
            setPtr(f, ptr(f) + take);
            setCnt(f, cnt(f) - take);
            done += take;
            continue;
        }
        int c = getC(f);
        if(c == EOF_)
            break;
        out[done++] = (uint8_t)c;
    }
    return (uint32_t)(done / sz);
}

uint32_t c_fwrite(uint32_t src, uint32_t sz, uint32_t n, uint32_t f)
{
    if(!sz || !n || fileIndex(f) < 0)
        return 0;
    uint64_t total = (uint64_t)sz * n, done = 0;
    const uint8_t *in = host(src);
    while(done < total)
    {
        if(putC(in[done], f) == EOF_)
            break;
        done++;
    }
    return (uint32_t)(done / sz);
}

uint32_t c_fopen(const char *path, const char *mode)
{
    uint32_t f = findFile();
    if(!f)
    {
        setErrno(24);
        return 0;
    }
    return openStream(path, mode, f);
}

uint32_t c_freopen(const char *path, const char *mode, uint32_t f)
{
    if(fileIndex(f) < 0)
        return 0;
    if(inUse(f))
        closeStream(f);
    return openStream(path, mode, f);
}

uint32_t c_fdopen(int32_t fd, const char *mode)
{
    int oflag;
    uint16_t fl;
    if(fd < 0 || fd >= NFD || !fds[fd].open || !parseMode(mode, oflag, fl))
    {
        setErrno(22);
        return 0;
    }
    uint32_t f = findFile();
    if(!f)
    {
        setErrno(24);
        return 0;
    }
    setupFile(f, fd, fl);
    return f;
}

int32_t c_fclose(uint32_t f) { return closeStream(f); }

int32_t c_fflush(uint32_t f)
{
    if(!f)
    {
        int result = 0;
        for(int i = 0; i < NFILE; i++)
            if(inUse(fileAddr(i)) && flushStream(fileAddr(i)))
                result = EOF_;
        return result;
    }
    return flushStream(f);
}

int32_t c_fseek(uint32_t f, int32_t offset, int32_t whence) { return seekStream(f, offset, whence); }
int32_t c_ftell(uint32_t f) { return tellStream(f); }
void c_rewind(uint32_t f)
{
    seekStream(f, 0, 0);
    if(fileIndex(f) >= 0)
        setFlag(f, flag(f) & ~IOERR);
}
int32_t c_fgetpos(uint32_t f, uint32_t pos)
{
    int32_t p = tellStream(f);
    if(p < 0)
        return -1;
    write32(pos, (uint32_t)p);
    return 0;
}
int32_t c_fsetpos(uint32_t f, uint32_t pos) { return seekStream(f, (int32_t)read32(pos), 0); }

void c_clearerr(uint32_t f)
{
    if(fileIndex(f) >= 0)
        setFlag(f, flag(f) & ~(IOERR | IOEOF));
}
int32_t c_feof(uint32_t f) { return fileIndex(f) >= 0 ? (flag(f) & IOEOF) : 0; }
int32_t c_ferror(uint32_t f) { return fileIndex(f) >= 0 ? (flag(f) & IOERR) : 0; }

int32_t c_setvbuf(uint32_t f, uint32_t buf, int32_t mode, uint32_t sz)
{
    if(fileIndex(f) < 0 || (mode != IOFBF && mode != IOLBF && mode != IONBF))
        return -1;
    flushStream(f);
    uint16_t fl = flag(f) & ~(IOLBF | IONBF | IOMYBUF);
    setFlag(f, fl | (uint16_t)mode);
    setBase(f, 0);
    if(mode != IONBF && buf && sz)
    {
        setBase(f, buf);
        setSize(f, (uint16_t)std::min<uint32_t>(sz, 0xFFFF));
        setEnd(f, buf + size(f));
        setPtr(f, buf);
    }
    else
        ensureBuffer(f);
    setCnt(f, 0);
    return 0;
}

void c_setbuf(uint32_t f, uint32_t buf)
{
    c_setvbuf(f, buf, buf ? IOFBF : IONBF, BUFSIZ_);
}

int32_t c_remove(const char *path)
{
    OSErr err = makeSpec(path);
    if(err == noErr)
        err = C_FSpDelete((FSSpecPtr)specBuf_);
    if(err != noErr)
    {
        setMacOSErr(err);
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    return 0;
}

int32_t c_rename(const char *from, const char *to)
{
    OSErr err = makeSpec(from);
    if(err == noErr)
    {
        err = makeSpec(to, specBuf2_);
        if(err == noErr)
            err = dupFNErr;
        else if(err == fnfErr)
        {
            /* The same folder: a rename. Elsewhere: not done. */
            uint8_t *a = specBuf_, *b = specBuf2_;
            if(memcmp(a, b, 6))
            {
                setErrno(18);
                return -1;
            }
            err = C_FSpRename((FSSpecPtr)specBuf_, specBuf2_ + 6);
        }
    }
    if(err != noErr)
    {
        setMacOSErr(err);
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    return 0;
}

char *c_tmpnam(char *s)
{
    static int counter = 0;
    int n = counter++;
    char name[32];
    snprintf(name, sizeof name, "tmp.%c%c%ca%06d", 'a' + n % 26, 'a' + n / 26 % 26,
             'a' + n / 676 % 26, (int)(C_TickCount() % 1000000));
    char *out = s ? s : tmpnamBuf_;
    strcpy(out, name);
    return out;
}

uint32_t c_tmpfile()
{
    char name[32];
    c_tmpnam(name);
    uint32_t f = c_fopen(name, "w+");
    if(f)
    {
        files[fileIndex(f)].deleteOnClose = true;
        files[fileIndex(f)].tmpPath = name;
    }
    return f;
}

char *c_mktemp(char *tmpl)
{
    size_t n = strlen(tmpl), x = n;
    while(x > 0 && tmpl[x - 1] == 'X')
        x--;
    static int counter = 0;
    for(int attempt = 0; attempt < 26 * 26; attempt++)
    {
        uint32_t v = (uint32_t)(C_TickCount() + counter++);
        for(size_t i = n; i > x; i--)
        {
            tmpl[i - 1] = char('0' + v % 10);
            v /= 10;
        }
        if(x > 0 && n > x)
            tmpl[x] = char('a' + attempt % 26);
        if(makeSpec(tmpl) == fnfErr)
            return tmpl;
    }
    tmpl[0] = 0;
    return tmpl;
}

void c_fsetfileinfo(const char *path, uint32_t creator, uint32_t type)
{
    if(makeSpec(path) != noErr)
        return;
    FInfo *fi = (FInfo *)finfo_;
    if(C_FSpGetFInfo((FSSpecPtr)specBuf_, fi) != noErr)
        return;
    fi->fdCreator = creator;
    fi->fdType = type;
    C_FSpSetFInfo((FSSpecPtr)specBuf_, fi);
}

void c_perror(const char *s)
{
    std::string line;
    if(s && *s)
        line = std::string(s) + ": ";
    line += errorMessage(getErrno());
    line += '\r';
    uint32_t f = fileAddr(2);
    for(char c : line)
        putC((uint8_t)c, f);
}

/* Low-level I/O */
int32_t c_open(const char *path, int32_t oflag) { return fdOpen(path, oflag); }
int32_t c_creat(const char *path) { return fdOpen(path, O_WRONLY_ | O_CREAT_ | O_TRUNC_); }
int32_t c_close(int32_t fd) { return fdClose(fd); }
int32_t c_read(int32_t fd, uint32_t buf, uint32_t n) { return fdRead(fd, buf, n); }
int32_t c_write(int32_t fd, uint32_t buf, uint32_t n) { return fdWrite(fd, buf, n); }
int32_t c_lseek(int32_t fd, int32_t off, int32_t whence) { return fdSeek(fd, off, whence); }

int32_t dupFd(int fd, int from)
{
    if(fd < 0 || fd >= NFD || !fds[fd].open)
    {
        setErrno(9);
        return -1;
    }
    int n = newFd(from);
    if(n < 0)
    {
        setErrno(24);
        return -1;
    }
    fds[n] = fds[fd];
    return n;
}

int32_t c_dup(int32_t fd) { return dupFd(fd, 0); }

int32_t c_fcntl(int32_t fd, uint32_t cmd, int32_t arg)
{
    if(cmd == 0)   /* F_DUPFD */
        return dupFd(fd, arg);
    setErrno(22);
    return -1;
}

int32_t c_ioctl(int32_t fd, uint32_t cmd, uint32_t arg)
{
    if(fd < 0 || fd >= NFD || !fds[fd].open)
    {
        setErrno(9);
        return -1;
    }
    Fd &d = fds[fd];
    switch(cmd)
    {
        case ('f' << 8) | 0x02:   /* FIOINTERACTIVE */
            return d.console >= 0 ? 0 : -1;
        case ('f' << 8) | 0x03:   /* FIOBUFSIZE */
            if(arg)
                write32(arg, BUFSIZ_);
            return 0;
        case ('f' << 8) | 0x05:   /* FIOREFNUM */
            if(d.console >= 0)
                break;
            if(arg)
                write16(arg, (uint16_t)d.refNum);
            return 0;
        case ('f' << 8) | 0x06:   /* FIOSETEOF */
            if(d.console >= 0)
                break;
            return SetEOF(d.refNum, (LONGINT)arg) == noErr ? 0 : -1;
        case ('t' << 8) | 0x00:   /* TIOFLUSH */
            return d.console >= 0 ? 0 : -1;
    }
    setErrno(22);
    return -1;
}

int32_t c_faccess(const char *path, uint32_t cmd, uint32_t arg)
{
    switch(cmd)
    {
        case ('d' << 8) | 0x01:   /* F_DELETE */
            return c_remove(path);
        case ('d' << 8) | 0x02:   /* F_RENAME */
            return c_rename(path, host<char>(arg));
    }
    setErrno(22);
    return -1;
}

int32_t c_access(const char *path, int32_t)
{
    OSErr err = makeSpec(path);
    if(err != noErr)
    {
        setErrno(errnoFromOSErr(err));
        return -1;
    }
    return 0;
}

/* ---- formatted I/O ---- */

uint32_t raw_printf(PowerCore &cpu)
{
    CallVarArgs args(cpu, 1);
    std::string s = formatPrintf(host<char>(cpu.r[3]), args);
    uint32_t f = fileAddr(1);
    for(char c : s)
        putC((uint8_t)c, f);
    cpu.r[3] = (uint32_t)s.size();
    return cpu.lr;
}

int32_t writeFormatted(uint32_t f, const std::string &s)
{
    if(fileIndex(f) < 0)
        return -1;
    for(char c : s)
        if(putC((uint8_t)c, f) == EOF_)
            return -1;
    return (int32_t)s.size();
}

uint32_t raw_fprintf(PowerCore &cpu)
{
    CallVarArgs args(cpu, 2);
    std::string s = formatPrintf(host<char>(cpu.r[4]), args);
    cpu.r[3] = (uint32_t)writeFormatted(cpu.r[3], s);
    return cpu.lr;
}

uint32_t raw_sprintf(PowerCore &cpu)
{
    uint32_t dst = cpu.r[3];
    CallVarArgs args(cpu, 2);
    std::string s = formatPrintf(host<char>(cpu.r[4]), args);
    memcpy(host(dst), s.c_str(), s.size() + 1);
    cpu.r[3] = (uint32_t)s.size();
    return cpu.lr;
}

uint32_t raw_vprintf(PowerCore &cpu)
{
    ListVarArgs args(cpu.r[4]);
    std::string s = formatPrintf(host<char>(cpu.r[3]), args);
    cpu.r[3] = (uint32_t)writeFormatted(fileAddr(1), s);
    return cpu.lr;
}

uint32_t raw_vfprintf(PowerCore &cpu)
{
    ListVarArgs args(cpu.r[5]);
    std::string s = formatPrintf(host<char>(cpu.r[4]), args);
    cpu.r[3] = (uint32_t)writeFormatted(cpu.r[3], s);
    return cpu.lr;
}

uint32_t raw_vsprintf(PowerCore &cpu)
{
    uint32_t dst = cpu.r[3];
    ListVarArgs args(cpu.r[5]);
    std::string s = formatPrintf(host<char>(cpu.r[4]), args);
    memcpy(host(dst), s.c_str(), s.size() + 1);
    cpu.r[3] = (uint32_t)s.size();
    return cpu.lr;
}

struct StringSource : CharSource
{
    const uint8_t *p;
    explicit StringSource(const char *s) : p((const uint8_t *)s) {}
    int get() override { return *p ? *p++ : -1; }
    void unget(int c) override { if(c >= 0) p--; }
};

struct FileSource : CharSource
{
    uint32_t f;
    explicit FileSource(uint32_t file) : f(file) {}
    int get() override { return getC(f); }
    void unget(int c) override { if(c >= 0) ungetC(c, f); }
};

uint32_t raw_sscanf(PowerCore &cpu)
{
    StringSource in(host<char>(cpu.r[3]));
    CallVarArgs args(cpu, 2);
    cpu.r[3] = (uint32_t)formatScanf(in, host<char>(cpu.r[4]), args);
    return cpu.lr;
}

uint32_t raw_fscanf(PowerCore &cpu)
{
    if(fileIndex(cpu.r[3]) < 0)
    {
        cpu.r[3] = (uint32_t)EOF_;
        return cpu.lr;
    }
    FileSource in(cpu.r[3]);
    CallVarArgs args(cpu, 2);
    cpu.r[3] = (uint32_t)formatScanf(in, host<char>(cpu.r[4]), args);
    return cpu.lr;
}

uint32_t raw_scanf(PowerCore &cpu)
{
    FileSource in(fileAddr(0));
    CallVarArgs args(cpu, 1);
    cpu.r[3] = (uint32_t)formatScanf(in, host<char>(cpu.r[3]), args);
    return cpu.lr;
}

/* stdio's internals some programs call */
int32_t c_xflsbuf(uint32_t f) { return fileIndex(f) < 0 ? EOF_ : flushOut(f); }
int32_t c_wrtchk(uint32_t f)
{
    int i = fileIndex(f);
    if(i < 0 || !(flag(f) & (IOWRT | IORW)))
        return EOF_;
    if(!files[i].writing)
    {
        dropIn(f);
        files[i].writing = true;
    }
    ensureBuffer(f);
    return 0;
}
void c_bufsync(uint32_t) {}
void c_coExit() {}

}

/* ---- start and end ---- */

void Executor::clib::stdioStart()
{
    for(auto &d : fds)
        d = Fd();
    for(int i = 0; i < 3; i++)
    {
        fds[i].open = true;
        fds[i].console = i;
        fds[i].flags = i == 0 ? O_RDONLY_ : O_WRONLY_;
        consoleLine[i].clear();
    }
    memset(iob_, 0, sizeof iob_);
    for(auto &s : files)
        s = FileState();
    setupFile(fileAddr(0), 0, IOREAD);
    /* As Apple's: stdout fully buffered, stderr line-buffered. */
    setupFile(fileAddr(1), 1, IOWRT);
    setupFile(fileAddr(2), 2, IOWRT | IOLBF);
    write32(guest(lastbuf_), fileAddr(NFILE - 1));
}

void Executor::clib::stdioFlushAll()
{
    for(int i = 0; i < NFILE; i++)
        if(inUse(fileAddr(i)))
            flushStream(fileAddr(i));
}

void Executor::clib::stdioCloseAll()
{
    for(int i = 0; i < NFILE; i++)
        if(inUse(fileAddr(i)))
            closeStream(fileAddr(i));
    for(int i = 0; i < NFD; i++)
        if(fds[i].open)
            fdClose(i);
}

void Executor::clib::addStdio()
{
    exportData(LIB, "_iob", iob_);
    exportData(LIB, "_lastbuf", lastbuf_);

    exportFn(LIB, "_filbuf", &c_filbuf);
    exportFn(LIB, "_flsbuf", &c_flsbuf);
    exportFn(LIB, "_xflsbuf", &c_xflsbuf);
    exportFn(LIB, "_wrtchk", &c_wrtchk);
    exportFn(LIB, "_bufsync", &c_bufsync);
    exportFn(LIB, "_coExit", &c_coExit);
    exportFn(LIB, "fgetc", &c_fgetc);
    exportFn(LIB, "getc", &c_fgetc);
    exportFn(LIB, "fputc", &c_fputc);
    exportFn(LIB, "putc", &c_fputc);
    exportFn(LIB, "getchar", &c_getchar);
    exportFn(LIB, "putchar", &c_putchar);
    exportFn(LIB, "ungetc", &c_ungetc);
    exportFn(LIB, "fgets", &c_fgets);
    exportFn(LIB, "gets", &c_gets);
    exportFn(LIB, "fputs", &c_fputs);
    exportFn(LIB, "puts", &c_puts);
    exportFn(LIB, "getw", &c_getw);
    exportFn(LIB, "putw", &c_putw);
    exportFn(LIB, "fread", &c_fread);
    exportFn(LIB, "fwrite", &c_fwrite);
    exportFn(LIB, "fopen", &c_fopen);
    exportFn(LIB, "freopen", &c_freopen);
    exportFn(LIB, "fdopen", &c_fdopen);
    exportFn(LIB, "fclose", &c_fclose);
    exportFn(LIB, "fflush", &c_fflush);
    exportFn(LIB, "fseek", &c_fseek);
    exportFn(LIB, "ftell", &c_ftell);
    exportFn(LIB, "rewind", &c_rewind);
    exportFn(LIB, "fgetpos", &c_fgetpos);
    exportFn(LIB, "fsetpos", &c_fsetpos);
    exportFn(LIB, "clearerr", &c_clearerr);
    exportFn(LIB, "feof", &c_feof);
    exportFn(LIB, "ferror", &c_ferror);
    exportFn(LIB, "setvbuf", &c_setvbuf);
    exportFn(LIB, "setbuf", &c_setbuf);
    exportFn(LIB, "remove", &c_remove);
    exportFn(LIB, "unlink", &c_remove);
    exportFn(LIB, "rename", &c_rename);
    exportFn(LIB, "tmpnam", &c_tmpnam);
    exportFn(LIB, "tmpfile", &c_tmpfile);
    exportFn(LIB, "mktemp", &c_mktemp);
    exportFn(LIB, "fsetfileinfo", &c_fsetfileinfo);
    exportFn(LIB, "perror", &c_perror);

    exportFn(LIB, "open", &c_open);
    exportFn(LIB, "creat", &c_creat);
    exportFn(LIB, "close", &c_close);
    exportFn(LIB, "read", &c_read);
    exportFn(LIB, "write", &c_write);
    exportFn(LIB, "lseek", &c_lseek);
    exportFn(LIB, "dup", &c_dup);
    exportFn(LIB, "fcntl", &c_fcntl);
    exportFn(LIB, "ioctl", &c_ioctl);
    exportFn(LIB, "faccess", &c_faccess);
    exportFn(LIB, "_faccess", &c_faccess);
    exportFn(LIB, "access", &c_access);

    exportRaw(LIB, "printf", &raw_printf);
    exportRaw(LIB, "fprintf", &raw_fprintf);
    exportRaw(LIB, "sprintf", &raw_sprintf);
    exportRaw(LIB, "vprintf", &raw_vprintf);
    exportRaw(LIB, "vfprintf", &raw_vfprintf);
    exportRaw(LIB, "vsprintf", &raw_vsprintf);
    exportRaw(LIB, "scanf", &raw_scanf);
    exportRaw(LIB, "fscanf", &raw_fscanf);
    exportRaw(LIB, "sscanf", &raw_sscanf);
}
