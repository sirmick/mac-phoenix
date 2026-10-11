/*
 * guest_watch.cpp - a write watchpoint on guest memory (see guest_watch.h).
 */
#include "include/guest_watch.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__linux__) && defined(__x86_64__)
#include <ucontext.h>
#define GUEST_WATCH_SUPPORTED 1
#include "include/crash_handler.h"
#else
#define GUEST_WATCH_SUPPORTED 0
#endif

namespace
{
bool installed;
uintptr_t page_lo, page_hi;    /* the protected page(s) */
uintptr_t watch_lo, watch_hi;  /* the watched bytes */
uint32_t watch_guest;
size_t watch_len;
GuestWatchHooks hooks;
uint8_t last_value[16];
unsigned long n_writes;

#if GUEST_WATCH_SUPPORTED
struct sigaction prev_segv, prev_trap;

/* Per thread: a write let through, waiting for the trap after it. */
thread_local bool pending;
thread_local uintptr_t pending_addr;
thread_local uintptr_t pending_host_pc;

void protect(bool on)
{
    mprotect((void *)page_lo, page_hi - page_lo, on ? PROT_READ : PROT_READ | PROT_WRITE);
}

void chain(const struct sigaction &prev, int sig, siginfo_t *si, void *uc)
{
    if(prev.sa_flags & SA_SIGINFO)
        prev.sa_sigaction(sig, si, uc);
    else if(prev.sa_handler == SIG_DFL)
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }
    else if(prev.sa_handler != SIG_IGN)
        prev.sa_handler(sig);
}

/* A system call writing into the protected page fails with EFAULT rather
 * than faulting, so the read family is interposed: the page is opened for
 * the call and the watched bytes compared after it. The symbols are the
 * binary's own, so this covers the emulator's reads, not a shared
 * library's. */
void hex(const uint8_t *p, size_t n, char *out);

bool overlaps(const void *buf, size_t n)
{
    uintptr_t b = (uintptr_t)buf;
    return installed && n && b < page_hi && b + n > page_lo;
}

void report_syscall_write(const char *what, int fd)
{
    if(memcmp((const void *)watch_lo, last_value, watch_len) == 0)
        return;
    char was[40], now[40];
    hex(last_value, watch_len, was);
    hex((const uint8_t *)watch_lo, watch_len, now);
    memcpy(last_value, (const void *)watch_lo, watch_len);
    n_writes++;
    fprintf(stderr, "\n[watch] #%lu guest $%08X (%zu bytes) written: %s -> %s; by %s(fd %d)\n",
            n_writes, watch_guest, watch_len, was, now, what, fd);
    if(hooks.describe)
        hooks.describe(stderr);
    print_backtrace("watch");
    fflush(stderr);
}

template<typename F, typename... Args>
ssize_t through(const char *what, F real, int fd, const void *buf, size_t n, Args... args)
{
    if(!overlaps(buf, n))
        return real(fd, (void *)buf, n, args...);
    protect(false);
    ssize_t r = real(fd, (void *)buf, n, args...);
    protect(true);
    report_syscall_write(what, fd);
    return r;
}

void hex(const uint8_t *p, size_t n, char *out)
{
    for(size_t i = 0; i < n; i++)
        sprintf(out + 2 * i, "%02x", p[i]);
    out[2 * n] = 0;
}

void on_segv(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    uintptr_t a = (uintptr_t)si->si_addr;
    if(installed && !pending && a >= page_lo && a < page_hi)
    {
        /* Let this one write through, then trap after the instruction. */
        pending = true;
        pending_addr = a;
        pending_host_pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
        protect(false);
        uc->uc_mcontext.gregs[REG_EFL] |= 0x100;  /* TF */
        return;
    }
    chain(prev_segv, sig, si, uc_);
}

void on_trap(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    if(!pending)
    {
        chain(prev_trap, sig, si, uc_);
        return;
    }
    pending = false;
    uc->uc_mcontext.gregs[REG_EFL] &= ~(greg_t)0x100;
    protect(true);
    /* The faulting address is the first byte of the access; a write that
     * starts up to 15 bytes below the watched bytes can still cover them,
     * so compare the bytes rather than trust the address. */
    if(pending_addr + 16 >= watch_lo && pending_addr < watch_hi
       && memcmp((const void *)watch_lo, last_value, watch_len) != 0)
    {
        char was[40], now[40];
        hex(last_value, watch_len, was);
        hex((const uint8_t *)watch_lo, watch_len, now);
        memcpy(last_value, (const void *)watch_lo, watch_len);
        n_writes++;
        fprintf(stderr, "\n[watch] #%lu guest $%08X (%zu bytes) written: %s -> %s; host pc %p, access at host %p\n",
                n_writes, watch_guest, watch_len, was, now, (void *)pending_host_pc, (void *)pending_addr);
        if(hooks.describe)
            hooks.describe(stderr);
        print_backtrace("watch");
        fflush(stderr);
    }
}
#endif
}  // namespace

#if GUEST_WATCH_SUPPORTED
extern "C" ssize_t read(int fd, void *buf, size_t n)
{
    static auto real = (ssize_t(*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
    return through("read", real, fd, buf, n);
}

extern "C" ssize_t pread(int fd, void *buf, size_t n, off_t off)
{
    static auto real = (ssize_t(*)(int, void *, size_t, off_t))dlsym(RTLD_NEXT, "pread");
    return through("pread", real, fd, buf, n, off);
}

extern "C" ssize_t pread64(int fd, void *buf, size_t n, off64_t off)
{
    static auto real = (ssize_t(*)(int, void *, size_t, off64_t))dlsym(RTLD_NEXT, "pread64");
    return through("pread64", real, fd, buf, n, off);
}
#endif

bool guest_watch_install(void *host_addr, size_t len, uint32_t guest_addr,
                         const GuestWatchHooks &h)
{
#if GUEST_WATCH_SUPPORTED
    if(installed || !host_addr || len == 0 || len > sizeof last_value)
        return false;
    const uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    watch_lo = (uintptr_t)host_addr;
    watch_hi = watch_lo + len;
    watch_guest = guest_addr;
    watch_len = len;
    hooks = h;
    page_lo = watch_lo & ~(page - 1);
    page_hi = (watch_hi + page - 1) & ~(page - 1);
    memcpy(last_value, host_addr, len);

    struct sigaction sa = {};
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGSEGV, &sa, &prev_segv) != 0)
        return false;
    sa.sa_sigaction = on_trap;
    if(sigaction(SIGTRAP, &sa, &prev_trap) != 0)
    {
        sigaction(SIGSEGV, &prev_segv, nullptr);
        return false;
    }
    if(mprotect((void *)page_lo, page_hi - page_lo, PROT_READ) != 0)
    {
        fprintf(stderr, "[watch] cannot protect the page at %p: %s\n", (void *)page_lo, strerror(errno));
        sigaction(SIGSEGV, &prev_segv, nullptr);
        sigaction(SIGTRAP, &prev_trap, nullptr);
        return false;
    }
    installed = true;
    char v[40];
    hex(last_value, len, v);
    fprintf(stderr, "[watch] guest $%08X (%zu bytes, host %p) holds %s; reporting every write\n",
            guest_addr, len, host_addr, v);
    return true;
#else
    (void)host_addr; (void)len; (void)guest_addr; (void)h;
    fprintf(stderr, "[watch] guest write watchpoints need Linux x86-64\n");
    return false;
#endif
}

void guest_watch_remove()
{
#if GUEST_WATCH_SUPPORTED
    if(!installed)
        return;
    installed = false;
    protect(false);
    sigaction(SIGSEGV, &prev_segv, nullptr);
    sigaction(SIGTRAP, &prev_trap, nullptr);
    fprintf(stderr, "[watch] guest $%08X: %lu writes reported\n", watch_guest, n_writes);
#endif
}

bool guest_watch_parse(const char *spec, uint32_t *addr, size_t *len)
{
    if(!spec || !*spec)
        return false;
    char *end = nullptr;
    const char *p = spec;
    if(p[0] == '$')
        p++;
    else if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    unsigned long a = strtoul(p, &end, 16);
    if(end == p)
        return false;
    size_t n = 4;
    if(*end == ':')
    {
        char *e2 = nullptr;
        n = strtoul(end + 1, &e2, 10);
        if(e2 == end + 1 || *e2 || n == 0 || n > 16)
            return false;
    }
    else if(*end)
        return false;
    *addr = (uint32_t)a;
    *len = n;
    return true;
}
