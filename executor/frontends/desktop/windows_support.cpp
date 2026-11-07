/*
 * windows_support.cpp - the console and the crash report on Windows
 * (windows_support.h).
 */
#include "windows_support.h"

#include <QDateTime>
#include <QDir>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <typeinfo>

#include <windows.h>
#include <dbghelp.h>

namespace windows_support {

namespace {

std::wstring logsDir, logFile;
std::atomic<bool> crashing{ false };

std::wstring wide(const QString &s)
{
    return s.toStdWString();
}

std::string narrow(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if(n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

/* "module+0xOFFSET" for a code address */
std::string where(DWORD64 pc)
{
    HMODULE mod = nullptr;
    char buf[600];
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)(uintptr_t)pc, &mod) && mod)
    {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
        const wchar_t *name = path;
        for(DWORD i = 0; i < n; i++)
            if(path[i] == L'\\' || path[i] == L'/')
                name = path + i + 1;
        snprintf(buf, sizeof buf, "%s+0x%llx", narrow(name).c_str(),
                 (unsigned long long)(pc - (DWORD64)(uintptr_t)mod));
    }
    else
        snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)pc);
    return buf;
}

/* The stack from CONTEXT, unwound with the executable's own unwind data
   (.pdata: every x64 function has it) */
std::string stack(CONTEXT ctx)
{
    std::string out;
    for(int i = 0; i < 64 && ctx.Rip; i++)
    {
        char line[700];
        snprintf(line, sizeof line, "  #%-2d %s\n", i, where(ctx.Rip).c_str());
        out += line;
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
    return out;
}

/* The log file's last LIMIT bytes, from a line's start */
std::string logTail(size_t limit)
{
    FILE *f = _wfopen(logFile.c_str(), L"rb");
    if(!f)
        return "(no log file)\n";
    _fseeki64(f, 0, SEEK_END);
    long long size = _ftelli64(f);
    long long from = size > (long long)limit ? size - (long long)limit : 0;
    _fseeki64(f, from, SEEK_SET);
    std::string s(size - from, '\0');
    s.resize(fread(s.data(), 1, s.size(), f));
    fclose(f);
    if(from > 0)
    {
        size_t nl = s.find('\n');
        if(nl != std::string::npos)
            s.erase(0, nl + 1);
    }
    return s;
}

void writeFile(const std::wstring &path, const std::string &text)
{
    FILE *f = _wfopen(path.c_str(), L"wb");
    if(f)
    {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
}

/* The report for WHY, at CTX (the crash's, or here), with the exception
   (null: none) for the minidump; then the dialog. The process ends after. */
void report(const char *why, CONTEXT *ctx, EXCEPTION_POINTERS *ep)
{
    if(crashing.exchange(true))
    {
        /* a second crash while reporting the first */
        TerminateProcess(GetCurrentProcess(), 3);
    }
    fprintf(stderr, "[desktop] CRASH: %s\n", why);
    fflush(stderr);
    Sleep(300); /* the log's reader thread writes what was still in the pipe */

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[64];
    swprintf(stamp, 64, L"crash-%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    std::wstring base = logsDir + L"\\" + stamp;
    std::wstring dmp = base + L".dmp", txt = base + L".txt";

    bool dumped = false;
    HANDLE h = CreateFileW(dmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(h != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei = {};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        /* threads, their stacks and the memory they point at; not the
           guest's memory (hundreds of MB) */
        auto type = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                    MiniDumpWithUnloadedModules);
        dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, ep ? &mei : nullptr,
                                   nullptr, nullptr);
        CloseHandle(h);
        if(!dumped)
            DeleteFileW(dmp.c_str());
    }

    std::string text = "Executor " EXECUTOR_VERSION " crashed: ";
    text += why;
    text += "\n\nStack of the crashing thread (tools/windows/symbolize.sh names the functions):\n";
    text += stack(*ctx);
    text += "\nThe log's last lines (" + narrow(logFile.c_str()) + "):\n\n";
    text += logTail(64 * 1024);
    writeFile(txt, text);
    fprintf(stderr, "[desktop] crash report: %s%s\n", narrow(txt.c_str()).c_str(),
            dumped ? (" and " + narrow(dmp.c_str())).c_str() : "");
    fflush(stderr);

    const char *qpa = getenv("QT_QPA_PLATFORM");
    if(!qpa || strcmp(qpa, "offscreen") != 0)
    {
        std::wstring msg = L"Executor stopped because of an error.\n\nA crash report was written to:\n\n" + txt +
                           L"\n";
        if(dumped)
            msg += dmp + L"\n";
        msg += L"\nPlease send these files, with the log (" + logFile + L").";
        MessageBoxW(nullptr, msg.c_str(), L"Executor crashed", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
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
    char why[200];
    auto *rec = ep->ExceptionRecord;
    if(rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
        snprintf(why, sizeof why, "%s (0x%08lx) %s address 0x%llx at %s", exceptionName(rec->ExceptionCode),
                 rec->ExceptionCode, rec->ExceptionInformation[0] == 1 ? "writing" : "reading",
                 (unsigned long long)rec->ExceptionInformation[1], where((DWORD64)rec->ExceptionAddress).c_str());
    else
        snprintf(why, sizeof why, "%s (0x%08lx) at %s", exceptionName(rec->ExceptionCode), rec->ExceptionCode,
                 where((DWORD64)rec->ExceptionAddress).c_str());
    CONTEXT ctx = *ep->ContextRecord;
    report(why, &ctx, ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* Here, as a minidump wants it (no exception: the current thread's
   context, which RtlCaptureContext gives) */
[[noreturn]] void reportHere(const char *why)
{
    CONTEXT ctx;
    RtlCaptureContext(&ctx);
    EXCEPTION_RECORD rec = {};
    rec.ExceptionCode = 0xE0000001; /* ours: a crash with no exception */
    rec.ExceptionAddress = (void *)ctx.Rip;
    EXCEPTION_POINTERS ep = { &rec, &ctx };
    report(why, &ctx, &ep);
    TerminateProcess(GetCurrentProcess(), 3);
    for(;;)
        ;
}

void onTerminate()
{
    std::string why = "std::terminate";
    if(auto e = std::current_exception())
    {
        try
        {
            std::rethrow_exception(e);
        }
        catch(const std::exception &x)
        {
            why += std::string(": uncaught ") + typeid(x).name() + ": " + x.what();
        }
        catch(...)
        {
            why += ": an uncaught exception";
        }
    }
    reportHere(why.c_str());
}

void onAbort(int)
{
    reportHere("abort()");
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
    logsDir = wide(QDir::toNativeSeparators(logs));
    logFile = wide(QDir::toNativeSeparators(log));
    SetUnhandledExceptionFilter(onException);
    std::set_terminate(onTerminate);
    signal(SIGABRT, onAbort);
    /* no Windows Error Reporting dialog on top of ours */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

void crash_now()
{
    fprintf(stderr, "[desktop] --crash-test: crashing now\n");
    fflush(stderr);
    volatile int *p = nullptr;
    *p = 42;
    for(;;)
        ;
}

}
