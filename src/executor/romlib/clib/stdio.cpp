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
#include <strings.h>

using namespace Executor;
using namespace Executor::clib;

namespace Executor
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

/* struct FILE: _cnt, _ptr, _base, _end, _size, _flag, _file: 24 bytes on
   PowerPC, 22 in a CFM-68K program (SC aligns structures to 2 bytes), so
   _iob's stride is the client's. */
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

namespace {
struct FileState
{
    bool writing = false;   /* the buffer holds output */
    bool deleteOnClose = false;
    std::string tmpPath;
};
}

FileState files[NFILE];

/* Descriptors, and the device table they go through, as Apple's StdCLib
   has them (checked on Mac OS 9.0.4: tests/guest/devtest). Five slots, each
   a name and five handlers called through pointers:
     faccess(name, cmd, iob)   F_OPEN (iob: the descriptor being opened),
                               F_DELETE, F_RENAME (arg: the new name), ...:
                               -1 when the name isn't the device's, 0 done,
                               else an errno or 0x40000000 | an OSErr
     close(iob), read(iob), write(iob)
     ioctl(iob, cmd, arg)      FIOLSEEK ({whence, offset} in, the position
                               in the second word out), FIODUPFD, ...
   which answer 0 or an errno. Slot 0 is the file system (FSYS), 1 the
   console (CONS), 2 Dev:Null (SYST). open, faccess, unlink and rename ask
   the slots from the last down; the first not to answer -1 has the name.
   A descriptor is a 20-byte iob: open's flags with the access bits one up
   (1 read, 2 write), an error word, the device's slot, the device's own
   word, and for read and write the count left and the buffer, which the
   handler moves on. The console streams are opened as "dev:console" at the
   first input or output, so a console installed before that (MPW Shell's
   windows, 'ECON') has them. */
constexpr int NDEV = 5;
constexpr int NFD = 64;
alignas(8) uint8_t devTable_[NDEV][24];
alignas(8) uint8_t iobs_[NFD][20];
alignas(8) uint8_t seekArg_[8];
alignas(8) char consoleName_[16];

constexpr uint32_t F_OPEN_ = 0x6400, F_DELETE_ = 0x6401, F_RENAME_ = 0x6402;
constexpr uint32_t FIOLSEEK_ = 0x6600, FIODUPFD_ = 0x6601, FIOINTERACTIVE_ = 0x6602,
                   FIOBUFSIZE_ = 0x6603, FIOREFNUM_ = 0x6605, FIOSETEOF_ = 0x6606,
                   TIOFLUSH_ = 0x7400;
constexpr uint16_t FREAD_ = 1, FWRITE_ = 2;
constexpr uint32_t NOT_MINE = 0xFFFFFFFF;

namespace {
struct Fd
{
    bool open = false;
    bool pending = false;   /* a console stream not opened yet */
};

/* FSYS's own word for a descriptor: as Apple's, a handle to a record that
   starts with the descriptors' count and the File Manager's refnum (MPW
   Shell's FIOFNAME takes the refnum from there); dup shares it. */
struct FsFile
{
    int refs = 0;
    int16_t refNum = 0;
    int oflag = 0;
};
}
alignas(8) uint8_t fsMasters_[NFD][4];
alignas(8) uint8_t fsRecords_[NFD][16];

Fd fds[NFD];
FsFile fsFiles[NFD];
std::string consoleLine[3];
int openingFd = -1;     /* the descriptor faccess(F_OPEN) is for */

uint32_t iobAddr(int fd) { return guest(iobs_[fd]); }
uint16_t iobFlags(uint32_t iob) { return read16(iob); }
uint32_t iobDevice(uint32_t iob) { return read32(iob + 4); }
uint32_t iobCookie(uint32_t iob) { return read32(iob + 8); }
void setIobCookie(uint32_t iob, uint32_t v) { write32(iob + 8, v); }
int32_t iobCount(uint32_t iob) { return (int32_t)read32(iob + 12); }
void setIobCount(uint32_t iob, int32_t v) { write32(iob + 12, (uint32_t)v); }
uint32_t iobBuffer(uint32_t iob) { return read32(iob + 16); }
void setIobBuffer(uint32_t iob, uint32_t v) { write32(iob + 16, v); }

uint32_t devAddr(int slot) { return guest(devTable_[slot]); }

/* ---- FILE fields ---- */

uint32_t fileSize = 24;

uint32_t fileAddr(int i) { return guest(iob_) + i * fileSize; }

int fileIndex(uint32_t f)
{
    uint32_t base = guest(iob_);
    if(f < base || f >= base + NFILE * fileSize || (f - base) % fileSize)
        return -1;
    return int((f - base) / fileSize);
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

/* A handler's error: an errno, or an OSErr marked 0x40000000. */
uint32_t osErrResult(OSErr err)
{
    setMacOSErr(err);
    return 0x40000000 | (uint16_t)err;
}

void setResultErrno(uint32_t r)
{
    setErrno((r & 0xFFFF0000) == 0x40000000 ? errnoFromOSErr((int16_t)r) : (int)r);
}

int newFd(int from = 0)
{
    for(int i = std::max(from, 0); i < NFD; i++)
        if(!fds[i].open && !fds[i].pending)
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

bool sameName(uint32_t name, const char *what)
{
    return !strcasecmp(host<char>(name), what);
}

/* ---- FSYS: files, through the File Manager ---- */

FsFile *fsFile(uint32_t iob)
{
    uint32_t c = iobCookie(iob), base = guest(fsMasters_);
    if(c < base || c >= base + 4 * NFD || (c - base) % 4)
        return nullptr;
    int k = (c - base) / 4;
    return fsFiles[k].refs > 0 ? &fsFiles[k] : nullptr;
}

/* The record a program sees, after a change. */
void fsPublish(FsFile *f)
{
    int k = int(f - fsFiles);
    write32(guest(fsMasters_[k]), f->refs > 0 ? guest(fsRecords_[k]) : 0);
    memset(fsRecords_[k], 0, sizeof fsRecords_[k]);
    write16(guest(fsRecords_[k]), (uint16_t)f->refs);
    write16(guest(fsRecords_[k]) + 2, (uint16_t)f->refNum);
}

uint32_t fsOpen(uint32_t name, uint32_t iob)
{
    const char *path = host<char>(name);
    uint16_t fl = iobFlags(iob);
    int oflag = (fl & ~3) | ((fl & 3) - 1);
    int k = 0;
    while(k < NFD && fsFiles[k].refs > 0)
        k++;
    if(k == NFD)
        return 24;   /* EMFILE */
    OSErr err = makeSpec(path);
    if(err == fnfErr && (oflag & O_CREAT_))
    {
        /* As Apple's StdCLib in an application: SimpleText's. */
        err = C_FSpCreate((FSSpecPtr)specBuf_, "ttxt"_4, "TEXT"_4, smSystemScript);
        if(err == noErr)
            oflag &= ~O_EXCL_;
    }
    else if(err == noErr && (oflag & O_CREAT_) && (oflag & O_EXCL_))
        return 17;   /* EEXIST */
    if(err != noErr)
        return osErrResult(err);
    SignedByte perm = (oflag & 3) == O_RDONLY_ ? fsRdPerm : fsRdWrPerm;
    GUEST<int16_t> *ref = (GUEST<int16_t> *)cell_;
    err = (oflag & O_RSRC_) ? C_FSpOpenRF((FSSpecPtr)specBuf_, perm, ref)
                            : C_FSpOpenDF((FSSpecPtr)specBuf_, perm, ref);
    if(err != noErr)
        return osErrResult(err);
    fsFiles[k] = { 1, *ref, oflag };
    fsPublish(&fsFiles[k]);
    if((oflag & O_TRUNC_) && (oflag & 3) != O_RDONLY_)
        SetEOF(*ref, 0);
    setIobCookie(iob, guest(fsMasters_[k]));
    return 0;
}

uint32_t fsDelete(const char *path)
{
    OSErr err = makeSpec(path);
    if(err == noErr)
        err = C_FSpDelete((FSSpecPtr)specBuf_);
    return err == noErr ? 0 : osErrResult(err);
}

uint32_t fsRename(const char *from, const char *to)
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
            if(memcmp(specBuf_, specBuf2_, 6))
                return 18;   /* EXDEV */
            err = C_FSpRename((FSSpecPtr)specBuf_, specBuf2_ + 6);
        }
    }
    return err == noErr ? 0 : osErrResult(err);
}

uint32_t fsysFaccess(uint32_t name, uint32_t cmd, uint32_t arg)
{
    switch(cmd)
    {
        case F_OPEN_: return fsOpen(name, arg);
        case F_DELETE_: return fsDelete(host<char>(name));
        case F_RENAME_: return fsRename(host<char>(name), host<char>(arg));
    }
    return 22;
}

uint32_t fsysClose(uint32_t iob, uint32_t, uint32_t)
{
    FsFile *f = fsFile(iob);
    if(!f)
        return 9;
    if(--f->refs > 0)
    {
        fsPublish(f);
        return 0;
    }
    OSErr err = FSClose(f->refNum);
    *f = FsFile();
    fsPublish(f);
    return err == noErr ? 0 : (setMacOSErr(err), (uint32_t)errnoFromOSErr(err));
}

uint32_t fsysTransfer(uint32_t iob, bool writing)
{
    FsFile *f = fsFile(iob);
    if(!f)
        return 9;
    int32_t n = iobCount(iob);
    if(n <= 0)
        return 0;
    if(writing && (f->oflag & O_APPEND_))
        SetFPos(f->refNum, fsFromLEOF, 0);
    GUEST<LONGINT> *count = (GUEST<LONGINT> *)cell_;
    *count = n;
    OSErr err = writing ? FSWrite(f->refNum, count, host(iobBuffer(iob)))
                        : FSRead(f->refNum, count, host(iobBuffer(iob)));
    int32_t done = *count;
    setIobCount(iob, n - done);
    setIobBuffer(iob, iobBuffer(iob) + done);
    if(err != noErr && !(err == eofErr && !writing))
    {
        setMacOSErr(err);
        return errnoFromOSErr(err);
    }
    return 0;
}

uint32_t fsysRead(uint32_t iob, uint32_t, uint32_t) { return fsysTransfer(iob, false); }
uint32_t fsysWrite(uint32_t iob, uint32_t, uint32_t) { return fsysTransfer(iob, true); }

uint32_t fsSeek(FsFile *f, uint32_t arg)
{
    int32_t whence = (int32_t)read32(arg), offset = (int32_t)read32(arg + 4);
    GUEST<LONGINT> *pos = (GUEST<LONGINT> *)cell_;
    LONGINT target;
    if(whence == 0)
        target = offset;
    else if(whence == 1)
    {
        GetFPos(f->refNum, pos);
        target = *pos + offset;
    }
    else if(whence == 2)
    {
        GetEOF(f->refNum, pos);
        target = *pos + offset;
    }
    else
        return 22;
    if(target < 0)
        return 22;
    /* Past the end: the file grows, as lseek and a write would have it. */
    GetEOF(f->refNum, pos);
    if(target > *pos && (f->oflag & 3) != O_RDONLY_)
        SetEOF(f->refNum, target);
    OSErr err = SetFPos(f->refNum, fsFromStart, target);
    if(err != noErr && err != eofErr)
    {
        setMacOSErr(err);
        return errnoFromOSErr(err);
    }
    GetFPos(f->refNum, pos);
    write32(arg, 0);
    write32(arg + 4, (uint32_t)(LONGINT)*pos);
    return 0;
}

uint32_t fsysIoctl(uint32_t iob, uint32_t cmd, uint32_t arg)
{
    FsFile *f = fsFile(iob);
    if(!f)
        return 9;
    switch(cmd)
    {
        case FIOLSEEK_:
            return fsSeek(f, arg);
        case FIODUPFD_:
            f->refs++;
            fsPublish(f);
            return 0;
        case FIOREFNUM_:
            if(arg)
                write16(arg, (uint16_t)f->refNum);
            return 0;
        case FIOSETEOF_:
        {
            OSErr err = SetEOF(f->refNum, (LONGINT)arg);
            return err == noErr ? 0 : (setMacOSErr(err), (uint32_t)errnoFromOSErr(err));
        }
    }
    /* FIOINTERACTIVE, FIOBUFSIZE, FIOFNAME too: Apple's answers EINVAL. */
    return 22;
}

/* ---- CONS: the console streams, Executor's log ---- */

int consWhich(uint32_t iob) { return (int)iobCookie(iob) - 1; }

uint32_t consFaccess(uint32_t name, uint32_t cmd, uint32_t arg)
{
    if(!sameName(name, "dev:console"))
        return NOT_MINE;
    if(cmd != F_OPEN_)
        return 22;
    int which = openingFd >= 0 && openingFd < 3 ? openingFd
                                                : (iobFlags(arg) & FWRITE_) ? 1 : 0;
    setIobCookie(arg, which + 1);
    return 0;
}

uint32_t consClose(uint32_t iob, uint32_t, uint32_t)
{
    int which = consWhich(iob);
    if(which > 0 && which < 3 && !consoleLine[which].empty())
    {
        uint8_t cr = '\r';
        consoleWrite(which, &cr, 1);
    }
    return 0;
}

uint32_t consRead(uint32_t, uint32_t, uint32_t)
{
    return 0;   /* stdin is at its end */
}

uint32_t consWrite(uint32_t iob, uint32_t, uint32_t)
{
    int which = consWhich(iob);
    int32_t n = iobCount(iob);
    if(n > 0)
        consoleWrite(which == 2 ? 2 : 1, host(iobBuffer(iob)), (uint32_t)n);
    setIobBuffer(iob, iobBuffer(iob) + std::max<int32_t>(n, 0));
    setIobCount(iob, 0);
    return 0;
}

uint32_t consIoctl(uint32_t, uint32_t cmd, uint32_t)
{
    switch(cmd)
    {
        case FIODUPFD_:
        case TIOFLUSH_:
            return 0;
        case FIOLSEEK_:
            return 29;   /* ESPIPE */
    }
    return 22;
}

/* ---- SYST: Dev:Null, and Dev:StdIn, Dev:StdOut, Dev:StdErr ---- */

uint32_t callHandler(uint32_t entry, int which, uint32_t a, uint32_t b = 0, uint32_t c = 0);

/* Dev:StdOut and the like: the console stream's device counts another
   descriptor (FIODUPFD) and the new one shares its device and word. */
uint32_t openStd(int which, uint32_t iob)
{
    if(!fds[which].open)
        return 9;   /* EBADF */
    uint32_t from = iobAddr(which);
    uint32_t r = callHandler(iobDevice(from), 4 /* ioctl */, from, FIODUPFD_, 0);
    if(r)
        return r;
    write32(iob + 4, iobDevice(from));
    setIobCookie(iob, iobCookie(from));
    return 0;
}

uint32_t systFaccess(uint32_t name, uint32_t cmd, uint32_t arg)
{
    static const char *const stdNames[3] = { "dev:stdin", "dev:stdout", "dev:stderr" };
    for(int i = 0; i < 3; i++)
        if(sameName(name, stdNames[i]))
            return cmd == F_OPEN_ ? openStd(i, arg) : 22;
    if(!sameName(name, "dev:null"))
        return NOT_MINE;
    return cmd == F_OPEN_ ? 0 : 22;
}

uint32_t systNothing(uint32_t, uint32_t, uint32_t) { return 0; }

uint32_t systWrite(uint32_t iob, uint32_t, uint32_t)
{
    setIobBuffer(iob, iobBuffer(iob) + std::max<int32_t>(iobCount(iob), 0));
    setIobCount(iob, 0);
    return 0;
}

uint32_t systIoctl(uint32_t, uint32_t cmd, uint32_t)
{
    return cmd == FIODUPFD_ ? 0 : 22;
}

/* ---- the table ---- */

enum { P_FACCESS, P_CLOSE, P_READ, P_WRITE, P_IOCTL };
using HostHandler = uint32_t (*)(uint32_t, uint32_t, uint32_t);

namespace {
struct BuiltinDevice
{
    OSType name;
    const char *procNames[5];
    HostHandler procs[5];
    uint32_t ppc[5];     /* the handlers as a program calls them */
    uint32_t m68k[5];
};
}

BuiltinDevice builtinDevices[3] = {
    { "FSYS"_4, { "FSYS faccess", "FSYS close", "FSYS read", "FSYS write", "FSYS ioctl" },
      { fsysFaccess, fsysClose, fsysRead, fsysWrite, fsysIoctl }, {}, {} },
    { "CONS"_4, { "CONS faccess", "CONS close", "CONS read", "CONS write", "CONS ioctl" },
      { consFaccess, consClose, consRead, consWrite, consIoctl }, {}, {} },
    { "SYST"_4, { "SYST faccess", "SYST close", "SYST read", "SYST write", "SYST ioctl" },
      { systFaccess, systNothing, systNothing, systWrite, systIoctl }, {}, {} },
};

/* A device's handler: Executor's own in C++, a program's through its
   pointer. */
uint32_t callHandler(uint32_t entry, int which, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t proc = read32(entry + 4 + 4 * which);
    for(const BuiltinDevice &d : builtinDevices)
        if(proc && (proc == d.ppc[which] || proc == d.m68k[which]))
            return d.procs[which](a, b, c);
    if(!proc)
        return which == P_FACCESS ? NOT_MINE : 22;
    return callClient(proc, { a, b, c });
}

/* faccess(name, cmd, arg) on the devices from the last slot down: the
   first that knows the name answers (NOT_MINE if none does). */
uint32_t askDevices(uint32_t name, uint32_t cmd, uint32_t arg, uint32_t *entry = nullptr)
{
    for(int slot = NDEV - 1; slot >= 0; slot--)
    {
        uint32_t e = devAddr(slot);
        if(!read32(e))
            continue;
        uint32_t r = callHandler(e, P_FACCESS, name, cmd, arg);
        if(r != NOT_MINE)
        {
            if(entry)
                *entry = e;
            return r;
        }
    }
    return NOT_MINE;
}

/* Opens name (a guest C string) as descriptor fd. */
int openOn(int fd, uint32_t name, int oflag)
{
    uint32_t iob = iobAddr(fd);
    memset(host(iob), 0, 20);
    write16(iob, (uint16_t)((oflag & ~3) | ((oflag & 3) + 1)));
    fds[fd].pending = true;   /* taken while the devices look */
    int saved = openingFd;
    openingFd = fd;
    uint32_t entry = 0;
    uint32_t r = askDevices(name, F_OPEN_, iob, &entry);
    openingFd = saved;
    fds[fd].pending = false;
    if(r == 0)
    {
        if(!iobDevice(iob))
            write32(iob + 4, entry);
        fds[fd].open = true;
        return fd;
    }
    memset(host(iob), 0, 20);
    setResultErrno(r == NOT_MINE ? 2 : r);
    return -1;
}

/* The console streams, opened at the first input or output. */
void ensureStd()
{
    for(int i = 0; i < 3; i++)
        if(fds[i].pending && openingFd < 0)
        {
            fds[i].pending = false;
            int saved = getErrno();
            openOn(i, guest(consoleName_), i == 0 ? O_RDONLY_ : O_WRONLY_);
            setErrno(saved);
        }
}

bool fdValid(int fd)
{
    ensureStd();
    if(fd < 0 || fd >= NFD || !fds[fd].open)
    {
        setErrno(9);
        return false;
    }
    return true;
}

int32_t fdTransfer(int fd, uint32_t buf, uint32_t n, bool writing)
{
    if(!fdValid(fd))
        return -1;
    uint32_t iob = iobAddr(fd);
    if(!(iobFlags(iob) & (writing ? FWRITE_ : FREAD_)))
    {
        setErrno(9);
        return -1;
    }
    setIobCount(iob, (int32_t)n);
    setIobBuffer(iob, buf);
    uint32_t r = callHandler(iobDevice(iob), writing ? P_WRITE : P_READ, iob);
    if(r)
    {
        setErrno((int)r);
        return -1;
    }
    return (int32_t)n - iobCount(iob);
}

int32_t fdWrite(int fd, uint32_t buf, uint32_t n) { return fdTransfer(fd, buf, n, true); }
int32_t fdRead(int fd, uint32_t buf, uint32_t n) { return fdTransfer(fd, buf, n, false); }

int32_t fdIoctl(int fd, uint32_t cmd, uint32_t arg)
{
    if(!fdValid(fd))
        return -1;
    uint32_t iob = iobAddr(fd);
    uint32_t r = callHandler(iobDevice(iob), P_IOCTL, iob, cmd, arg);
    if(r)
    {
        setErrno((int)r);
        return -1;
    }
    return 0;
}

int32_t fdSeek(int fd, int32_t offset, int whence)
{
    write32(guest(seekArg_), (uint32_t)whence);
    write32(guest(seekArg_) + 4, (uint32_t)offset);
    if(fdIoctl(fd, FIOLSEEK_, guest(seekArg_)) < 0)
        return -1;
    return (int32_t)read32(guest(seekArg_) + 4);
}

int fdClose(int fd)
{
    if(!fdValid(fd))
        return -1;
    uint32_t iob = iobAddr(fd);
    uint32_t r = callHandler(iobDevice(iob), P_CLOSE, iob);
    fds[fd] = Fd();
    memset(host(iob), 0, 20);
    if(r)
    {
        setErrno((int)r);
        return -1;
    }
    return 0;
}

int fdOpen(const char *path, int oflag)
{
    ensureStd();
    int fd = newFd(3);
    if(fd < 0)
    {
        setErrno(24);
        return -1;
    }
    return openOn(fd, guest(path), oflag);
}

/* faccess on the devices: delete, rename and the rest. */
int32_t devFaccess(uint32_t name, uint32_t cmd, uint32_t arg)
{
    uint32_t r = askDevices(name, cmd, arg);
    if(r == 0)
        return 0;
    setResultErrno(r == NOT_MINE ? 2 : r);
    return -1;
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
        /* As Apple's stdio: the size the device asks for (FIOBUFSIZE; the
           file system answers EINVAL: BUFSIZ), and whether it is
           interactive, asked once. */
        uint16_t sz = BUFSIZ_;
        int saved = getErrno();
        if(fdIoctl(fileFd(f), FIOBUFSIZE_, guest(cell_)) == 0)
        {
            int32_t want = (int32_t)read32(guest(cell_));
            if(want > 0 && want < BUFSIZ_)
                sz = (uint16_t)want;
        }
        fdIoctl(fileFd(f), FIOINTERACTIVE_, guest(cell_));
        setErrno(saved);
        setBase(f, guest(buffers_[i]));
        setSize(f, sz);
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
        /* A device that can't seek (the console) keeps it. */
        int saved = getErrno();
        fdSeek(fileFd(f), -cnt(f), 1);
        setErrno(saved);
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
    /* Reading stdin: what was written to stdout goes out first. */
    if(fileFd(f) == 0)
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

/* The first free FILE: stdin, stdout or stderr's own once closed, as
   Apple's stdio has it (MPW Shell redirects stdout by fclose(stdout),
   moving the file to descriptor 1 and fdopen(1, "w"), then writes to
   stdout). */
uint32_t findFile()
{
    for(int i = 0; i < NFILE; i++)
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
    memset(host(fileAddr(i)), 0, fileSize);
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
    memset(host(fileAddr(i)), 0, fileSize);
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
    if(!parseMode(mode, oflag, fl) || !fdValid(fd))
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

int32_t c_remove(const char *path) { return devFaccess(guest(path), F_DELETE_, 0); }

int32_t c_rename(const char *from, const char *to)
{
    return devFaccess(guest(from), F_RENAME_, guest(to));
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
    if(!fdValid(fd))
        return -1;
    int n = newFd(from);
    if(n < 0)
    {
        setErrno(24);
        return -1;
    }
    /* The device counts it (FIODUPFD), then the iob is copied. */
    uint32_t iob = iobAddr(fd);
    uint32_t r = callHandler(iobDevice(iob), P_IOCTL, iob, FIODUPFD_, 0);
    if(r)
    {
        setErrno((int)r);
        return -1;
    }
    memcpy(host(iobAddr(n)), host(iob), 20);
    fds[n].open = true;
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
    if(cmd == FIOLSEEK_ || cmd == FIODUPFD_)
    {
        setErrno(22);   /* lseek's and dup's */
        return -1;
    }
    return fdIoctl(fd, cmd, arg);
}

int32_t c_faccess(const char *path, uint32_t cmd, uint32_t arg)
{
    return devFaccess(guest(path), cmd, arg);
}

/* The device table (_addDevHandler, _getDevHandler): a slot's name and
   handlers. Apple's answers the slot, or -1 past the fifth. */
int32_t c_addDevHandler(int32_t slot, uint32_t name, uint32_t p1, uint32_t p2, uint32_t p3,
                        uint32_t p4, uint32_t p5)
{
    if(slot < 0 || slot >= NDEV)
        return -1;
    uint32_t e = devAddr(slot);
    write32(e, name);
    const uint32_t procs[5] = { p1, p2, p3, p4, p5 };
    for(int i = 0; i < 5; i++)
        write32(e + 4 + 4 * i, procs[i]);
    return slot;
}

int32_t c_getDevHandler(int32_t slot, uint32_t name, uint32_t p1, uint32_t p2, uint32_t p3,
                        uint32_t p4, uint32_t p5)
{
    if(slot < 0 || slot >= NDEV || !read32(devAddr(slot)))
        return -1;
    uint32_t e = devAddr(slot);
    if(name)
        write32(name, read32(e));
    const uint32_t outs[5] = { p1, p2, p3, p4, p5 };
    for(int i = 0; i < 5; i++)
        if(outs[i])
            write32(outs[i], read32(e + 4 + 4 * i));
    return 0;
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

/* Each once, on a call's arguments from either processor (CallArgs); the
   raw_ routines are the PowerPC entries, CFM-68K's are registered in
   addStdio. */

int32_t writeFormatted(uint32_t f, const std::string &s)
{
    if(fileIndex(f) < 0)
        return -1;
    for(char c : s)
        if(putC((uint8_t)c, f) == EOF_)
            return -1;
    return (int32_t)s.size();
}

uint32_t f_printf(const CallArgs &a)
{
    CallVarArgs args(a, 1);
    std::string s = formatPrintf(host<char>(a.word(0)), args);
    uint32_t f = fileAddr(1);
    for(char c : s)
        putC((uint8_t)c, f);
    return (uint32_t)s.size();
}

uint32_t f_fprintf(const CallArgs &a)
{
    CallVarArgs args(a, 2);
    std::string s = formatPrintf(host<char>(a.word(1)), args);
    return (uint32_t)writeFormatted(a.word(0), s);
}

uint32_t f_sprintf(const CallArgs &a)
{
    uint32_t dst = a.word(0);
    CallVarArgs args(a, 2);
    std::string s = formatPrintf(host<char>(a.word(1)), args);
    memcpy(host(dst), s.c_str(), s.size() + 1);
    return (uint32_t)s.size();
}

bool isM68K(const CallArgs &a) { return a.address(0) != 0; }

uint32_t f_vprintf(const CallArgs &a)
{
    ListVarArgs args(a.word(1), isM68K(a));
    std::string s = formatPrintf(host<char>(a.word(0)), args);
    return (uint32_t)writeFormatted(fileAddr(1), s);
}

uint32_t f_vfprintf(const CallArgs &a)
{
    ListVarArgs args(a.word(2), isM68K(a));
    std::string s = formatPrintf(host<char>(a.word(1)), args);
    return (uint32_t)writeFormatted(a.word(0), s);
}

uint32_t f_vsprintf(const CallArgs &a)
{
    uint32_t dst = a.word(0);
    ListVarArgs args(a.word(2), isM68K(a));
    std::string s = formatPrintf(host<char>(a.word(1)), args);
    memcpy(host(dst), s.c_str(), s.size() + 1);
    return (uint32_t)s.size();
}

namespace {
struct StringSource : CharSource
{
    const uint8_t *p;
    explicit StringSource(const char *s) : p((const uint8_t *)s) {}
    int get() override { return *p ? *p++ : -1; }
    void unget(int c) override { if(c >= 0) p--; }
};
}


namespace {
struct FileSource : CharSource
{
    uint32_t f;
    explicit FileSource(uint32_t file) : f(file) {}
    int get() override { return getC(f); }
    void unget(int c) override { if(c >= 0) ungetC(c, f); }
};
}


uint32_t f_sscanf(const CallArgs &a)
{
    StringSource in(host<char>(a.word(0)));
    CallVarArgs args(a, 2);
    return (uint32_t)formatScanf(in, host<char>(a.word(1)), args);
}

uint32_t f_fscanf(const CallArgs &a)
{
    if(fileIndex(a.word(0)) < 0)
        return (uint32_t)EOF_;
    FileSource in(a.word(0));
    CallVarArgs args(a, 2);
    return (uint32_t)formatScanf(in, host<char>(a.word(1)), args);
}

uint32_t f_scanf(const CallArgs &a)
{
    FileSource in(fileAddr(0));
    CallVarArgs args(a, 1);
    return (uint32_t)formatScanf(in, host<char>(a.word(0)), args);
}

#define PPC_ENTRY(NAME) \
    uint32_t raw_##NAME(PowerCore &cpu) { cpu.r[3] = f_##NAME(PPCCallArgs(cpu)); return cpu.lr; }
PPC_ENTRY(printf)
PPC_ENTRY(fprintf)
PPC_ENTRY(sprintf)
PPC_ENTRY(vprintf)
PPC_ENTRY(vfprintf)
PPC_ENTRY(vsprintf)
PPC_ENTRY(sscanf)
PPC_ENTRY(fscanf)
PPC_ENTRY(scanf)
#undef PPC_ENTRY

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

void Executor::clib::stdioStart(bool m68k)
{
    fileSize = m68k ? 22 : 24;
    for(auto &d : fds)
        d = Fd();
    for(auto &f : fsFiles)
        f = FsFile();
    memset(fsMasters_, 0, sizeof fsMasters_);
    memset(fsRecords_, 0, sizeof fsRecords_);
    memset(iobs_, 0, sizeof iobs_);
    openingFd = -1;
    /* Apple's three devices, their handlers callable from the program's
       processor; the console streams open at the first input or output. */
    memset(devTable_, 0, sizeof devTable_);
    for(int slot = 0; slot < 3; slot++)
    {
        const BuiltinDevice &d = builtinDevices[slot];
        write32(devAddr(slot), d.name);
        for(int i = 0; i < 5; i++)
            write32(devAddr(slot) + 4 + 4 * i, m68k ? d.m68k[i] : d.ppc[i]);
    }
    strcpy(consoleName_, "dev:console");
    for(int i = 0; i < 3; i++)
    {
        fds[i].pending = true;
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
    for(auto &d : fds)
        d = Fd();
}

void Executor::clib::addStdio()
{
    for(BuiltinDevice &d : builtinDevices)
        for(int i = 0; i < 5; i++)
        {
            HostHandler h = d.procs[i];
            d.ppc[i] = guest(builtinlibs::makePPCRoutine(d.procNames[i], [h](PowerCore &cpu) {
                EM_A7 = cpu.r[1];
                cpu.r[3] = h(cpu.r[3], cpu.r[4], cpu.r[5]);
                return cpu.lr;
            }));
            d.m68k[i] = guest(builtinlibs::makeCFM68KRoutine(d.procNames[i], [h](syn68k_addr_t) {
                CFM68KCallArgs a(EM_A7);
                EM_D0 = h(a.word(0), a.word(1), a.word(2));
                syn68k_addr_t ret = read32(EM_A7);
                EM_A7 += 4;
                return ret;
            }));
        }



    exportRaw(LIB, "printf", &raw_printf);
    exportRaw(LIB, "fprintf", &raw_fprintf);
    exportRaw(LIB, "sprintf", &raw_sprintf);
    exportRaw(LIB, "vprintf", &raw_vprintf);
    exportRaw(LIB, "vfprintf", &raw_vfprintf);
    exportRaw(LIB, "vsprintf", &raw_vsprintf);
    exportRaw(LIB, "scanf", &raw_scanf);
    exportRaw(LIB, "fscanf", &raw_fscanf);
    exportRaw(LIB, "sscanf", &raw_sscanf);
    /* CFM-68K (NuStdCLib): the variadic ones leave their arguments to the
       caller, the v ones pop theirs. */
    exportRaw68k(LIB, "printf", &f_printf);
    exportRaw68k(LIB, "fprintf", &f_fprintf);
    exportRaw68k(LIB, "sprintf", &f_sprintf);
    exportRaw68k(LIB, "vprintf", &f_vprintf, 2);
    exportRaw68k(LIB, "vfprintf", &f_vfprintf, 3);
    exportRaw68k(LIB, "vsprintf", &f_vsprintf, 3);
    exportRaw68k(LIB, "scanf", &f_scanf);
    exportRaw68k(LIB, "fscanf", &f_fscanf);
    exportRaw68k(LIB, "sscanf", &f_sscanf);
}

/* The data exports' storage (StdCLib.yaml, `data:`). */
namespace Executor
{
void *data_iob() { return iob_; }
void *data_lastbuf() { return lastbuf_; }
}
