/*
 * windows_support.cpp - the console and the crash report on Windows
 * (windows_support.h).
 *
 * The crash report is written by a thread of its own, started at install:
 * the crashing thread may hold the C runtime's or the heap's lock (a crash
 * inside malloc, or with the heap corrupted), so it only copies the
 * exception into static storage, wakes the reporter and waits. The reporter
 * writes the text report with Win32 calls and static buffers only, into
 * files opened at install (delete-on-close until a crash keeps them), then
 * the minidump and the dialog, which may need the heap: the crashing thread
 * waits for those a bounded time, so a wedged heap still leaves the text
 * report and ends the process.
 */
#include "windows_support.h"

#include <QDateTime>
#include <QDir>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <typeinfo>

#include <windows.h>
#include <dbghelp.h>

namespace windows_support {

namespace {

/* Set up at install */
wchar_t txtPath[MAX_PATH], dmpPath[MAX_PATH], logPath[MAX_PATH];
HANDLE txtFile = INVALID_HANDLE_VALUE, dmpFile = INVALID_HANDLE_VALUE, logFile = INVALID_HANDLE_VALUE;
HANDLE crashed, textDone, dumpDone, allDone;  /* events */

/* The crash, as the crashing thread leaves it */
std::atomic<bool> crashing{ false };
char why[1024];
EXCEPTION_RECORD record;
CONTEXT context;
EXCEPTION_POINTERS pointers;
DWORD crashThread;

/* The reporter's own buffers */
char text[96 * 1024];
size_t textLen;
char logTail[64 * 1024];

void add(const char *s)
{
    size_t n = strlen(s);
    if(textLen + n > sizeof text)
        n = sizeof text - textLen;
    memcpy(text + textLen, s, n);
    textLen += n;
}

/* "module+0xOFFSET" for a code address, into OUT */
void where(DWORD64 pc, char *out, size_t size)
{
    HMODULE mod = nullptr;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)(uintptr_t)pc, &mod) && mod)
    {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
        const wchar_t *name = path;
        for(DWORD i = 0; i < n; i++)
            if(path[i] == L'\\' || path[i] == L'/')
                name = path + i + 1;
        char utf8[MAX_PATH * 3];
        if(!WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, nullptr, nullptr))
            strcpy(utf8, "?");
        snprintf(out, size, "%s+0x%llx", utf8, (unsigned long long)(pc - (DWORD64)(uintptr_t)mod));
    }
    else
        snprintf(out, size, "0x%llx", (unsigned long long)pc);
}

/* The stack from CTX, unwound with the executables' own unwind data
   (.pdata: every x64 function has it) */
void addStack(CONTEXT ctx)
{
    for(int i = 0; i < 64 && ctx.Rip; i++)
    {
        char at[700], line[760];
        where(ctx.Rip, at, sizeof at);
        snprintf(line, sizeof line, "  #%-2d %s\n", i, at);
        add(line);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
        if(!fn)
        {
            /* a leaf function: the return address is on top of the stack */
            ctx.Rip = *(DWORD64 *)ctx.Rsp;
            ctx.Rsp += 8;
            continue;
        }
        void *handlerData = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handlerData, &frame, nullptr);
    }
}

/* The log file's last 64 KB from a line's start */
void addLogTail()
{
    LARGE_INTEGER size;
    if(logFile == INVALID_HANDLE_VALUE || !GetFileSizeEx(logFile, &size))
    {
        add("(no log file)\n");
        return;
    }
    LARGE_INTEGER from;
    from.QuadPart = size.QuadPart > (LONGLONG)sizeof logTail - 1 ? size.QuadPart - (sizeof logTail - 1) : 0;
    DWORD got = 0;
    SetFilePointerEx(logFile, from, nullptr, FILE_BEGIN);
    ReadFile(logFile, logTail, sizeof logTail - 1, &got, nullptr);
    logTail[got] = 0;
    const char *p = logTail;
    if(from.QuadPart > 0)
        if(const char *nl = strchr(p, '\n'))
            p = nl + 1;
    add(p);
}

void writeAll(HANDLE h, const char *data, size_t n)
{
    while(n > 0)
    {
        DWORD put = 0;
        if(!WriteFile(h, data, (DWORD)n, &put, nullptr) || put == 0)
            return;
        data += put;
        n -= put;
    }
}

/* Keep a file opened delete-on-close (FileDispositionInfo) */
void keep(HANDLE h)
{
    FILE_DISPOSITION_INFO keep = { FALSE };
    SetFileInformationByHandle(h, FileDispositionInfo, &keep, sizeof keep);
}

void toUtf8(const wchar_t *w, char *out, size_t size)
{
    if(!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, nullptr, nullptr))
        out[0] = 0;
}

DWORD WINAPI reporter(void *)
{
    WaitForSingleObject(crashed, INFINITE);
    char txt8[MAX_PATH * 3], dmp8[MAX_PATH * 3], log8[MAX_PATH * 3], line[1400];
    toUtf8(txtPath, txt8, sizeof txt8);
    toUtf8(dmpPath, dmp8, sizeof dmp8);
    toUtf8(logPath, log8, sizeof log8);

    /* 1. the text report and a line in the log: no heap */
    snprintf(line, sizeof line, "[desktop] CRASH: %s\n[desktop] crash report: %s and %s\n", why, txt8, dmp8);
    if(logFile != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER end = {};
        SetFilePointerEx(logFile, end, nullptr, FILE_END);
        writeAll(logFile, line, strlen(line));
    }
    add("Executor " EXECUTOR_VERSION " crashed: ");
    add(why);
    add("\n\nStack of the crashing thread (tools/windows/symbolize.sh names the functions):\n");
    addStack(context);
    snprintf(line, sizeof line, "\nThe log's last lines (%s):\n\n", log8);
    add(line);
    addLogTail();
    if(txtFile != INVALID_HANDLE_VALUE)
    {
        writeAll(txtFile, text, textLen);
        keep(txtFile);
        CloseHandle(txtFile);
    }
    SetEvent(textDone);

    /* 2. the minidump: dbghelp may need the heap (the crashing thread waits
       for it a bounded time) */
    if(dmpFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei = {};
        mei.ThreadId = crashThread;
        mei.ExceptionPointers = &pointers;
        mei.ClientPointers = FALSE;
        /* threads, their stacks and the memory they point at; not the
           guest's memory (hundreds of MB) */
        auto type = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                    MiniDumpWithUnloadedModules);
        if(MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dmpFile, type, &mei, nullptr, nullptr))
            keep(dmpFile);
        CloseHandle(dmpFile);
    }
    SetEvent(dumpDone);

    /* 3. the dialog (none offscreen: tests, automation) */
    char qpa[32];
    DWORD n = GetEnvironmentVariableA("QT_QPA_PLATFORM", qpa, sizeof qpa);
    if(!(n > 0 && n < sizeof qpa && strcmp(qpa, "offscreen") == 0))
    {
        static wchar_t msg[4 * MAX_PATH + 300];
        _snwprintf(msg, sizeof msg / sizeof msg[0],
                   L"Executor stopped because of an error.\n\nA crash report was written to:\n\n%ls\n%ls\n\n"
                   L"Please send these files, with the log (%ls).",
                   txtPath, dmpPath, logPath);
        MessageBoxW(nullptr, msg, L"Executor crashed", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    SetEvent(allDone);
    return 0;
}

/* From the crashing thread: hand over and wait. The text report is heap
   free (10 s is plenty); the dump gets 30 s; the dialog as long as it is
   up, once the dump came back (else the heap is wedged: no dialog will
   either) */
void handOff()
{
    if(crashing.exchange(true))
    {
        /* a second crash while the first is reported */
        WaitForSingleObject(allDone, 60000);
        TerminateProcess(GetCurrentProcess(), 3);
    }
    crashThread = GetCurrentThreadId();
    SetEvent(crashed);
    WaitForSingleObject(textDone, 10000);
    if(WaitForSingleObject(dumpDone, 30000) == WAIT_OBJECT_0)
        WaitForSingleObject(allDone, INFINITE);
}

const char *exceptionName(DWORD code)
{
    switch(code)
    {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data";
        default: return "exception";
    }
}

LONG WINAPI onException(EXCEPTION_POINTERS *ep)
{
    if(crashing.load())
        handOff();  /* the reporter itself, or a second crash */
    /* copies only: the exception record and context are valid while this
       thread waits, but kept anyway, as the terminate path keeps its own */
    record = *ep->ExceptionRecord;
    context = *ep->ContextRecord;
    pointers = { &record, &context };
    /* snprintf of numbers into a static buffer: no heap, no stream lock */
    char at[700];
    where((DWORD64)record.ExceptionAddress, at, sizeof at);
    if(record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        snprintf(why, sizeof why, "%s (0x%08lx) %s address 0x%llx at %s", exceptionName(record.ExceptionCode),
                 record.ExceptionCode, record.ExceptionInformation[0] == 1 ? "writing" : "reading",
                 (unsigned long long)record.ExceptionInformation[1], at);
    else
        snprintf(why, sizeof why, "%s (0x%08lx) at %s", exceptionName(record.ExceptionCode), record.ExceptionCode,
                 at);
    handOff();
    return EXCEPTION_EXECUTE_HANDLER;
}

/* No exception (std::terminate, abort()): this thread's context as the
   crash, for the stack and the minidump */
[[noreturn]] void crashHere()
{
    RtlCaptureContext(&context);
    record = {};
    record.ExceptionCode = 0xE0000001; /* ours: a crash with no exception */
    record.ExceptionAddress = (void *)context.Rip;
    pointers = { &record, &context };
    handOff();
    TerminateProcess(GetCurrentProcess(), 3);
    for(;;)
        ;
}

void onTerminate()
{
    strcpy(why, "std::terminate");
    if(auto e = std::current_exception())
    {
        try
        {
            std::rethrow_exception(e);
        }
        catch(const std::exception &x)
        {
            snprintf(why, sizeof why, "std::terminate: uncaught %s: %s", typeid(x).name(), x.what());
        }
        catch(...)
        {
            strcpy(why, "std::terminate: an uncaught exception");
        }
    }
    crashHere();
}

void onAbort(int)
{
    strcpy(why, "abort()");
    crashHere();
}

/* PATH created now, deleted when the process ends unless keep() */
HANDLE openPending(const wchar_t *path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if(h == INVALID_HANDLE_VALUE)
        return h;
    FILE_DISPOSITION_INFO del = { TRUE };
    if(!SetFileInformationByHandle(h, FileDispositionInfo, &del, sizeof del))
    {
        CloseHandle(h);
        DeleteFileW(path);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

}

QString describe()
{
    return QString("code page %1%2").arg(GetACP()).arg(GetACP() == CP_UTF8 ? " (UTF-8)" : " (not UTF-8: the manifest's activeCodePage didn't apply)");
}

void attach_console()
{
    if(!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole())
        return;
    SetConsoleOutputCP(CP_UTF8);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

void install_crash_handler(const QString &logs, const QString &log)
{
    /* this run's report files, made now (the heap may be unusable later) */
    QString base = QDir::toNativeSeparators(logs) + "\\crash-" +
                   QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-" +
                   QString::number(GetCurrentProcessId());
    wcsncpy(txtPath, (base + ".txt").toStdWString().c_str(), MAX_PATH - 1);
    wcsncpy(dmpPath, (base + ".dmp").toStdWString().c_str(), MAX_PATH - 1);
    wcsncpy(logPath, QDir::toNativeSeparators(log).toStdWString().c_str(), MAX_PATH - 1);
    txtFile = openPending(txtPath);
    dmpFile = openPending(dmpPath);
    logFile = CreateFileW(logPath, GENERIC_READ | FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    crashed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    textDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    dumpDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    allDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CloseHandle(CreateThread(nullptr, 256 * 1024, reporter, nullptr, 0, nullptr));

    SetUnhandledExceptionFilter(onException);
    std::set_terminate(onTerminate);
    signal(SIGABRT, onAbort);
    /* no Windows Error Reporting dialog on top of ours */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

void crash_now(bool heapLocked)
{
    fprintf(stderr, "[desktop] --crash-test%s: crashing now\n", heapLocked ? " heap" : "");
    fflush(stderr);
    Sleep(200);  /* the log's reader writes that line */
    if(heapLocked)
        HeapLock(GetProcessHeap());  /* as a crash inside malloc would leave it */
    volatile int *p = nullptr;
    *p = 42;
    for(;;)
        ;
}

}
