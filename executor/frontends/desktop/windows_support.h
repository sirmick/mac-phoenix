#pragma once
/*
 * windows_support.h - what the desktop application does only on Windows
 * (windows_support.cpp): a console for the log, and a crash report.
 *
 * The executable is a GUI one (no console of its own): --console attaches
 * to the console it was started from (cmd, PowerShell) or opens one, so
 * the log shows there as it is written, as well as in the log file.
 *
 * A crash (an unhandled exception, std::terminate, abort()) writes
 * crash-STAMP.dmp (a minidump) and crash-STAMP.txt (what happened, the
 * crashing thread's stack as module+offset, the log's last lines) to the
 * logs folder, and a dialog says where they are (none with
 * QT_QPA_PLATFORM=offscreen). tools/windows/symbolize.sh turns the stack
 * into function names with the build's unstripped executable.
 */
#include <QString>

namespace windows_support {

/* Before anything is written to stderr */
void attach_console();

/* LOGS the logs folder, LOG the log file whose tail goes in the report */
void install_crash_handler(const QString &logs, const QString &log);

/* What the log should say about this Windows: the code page (65001, UTF-8,
   from the manifest; anything else and non-ASCII Mac names can't be host
   paths) */
QString describe();

/* --crash-test: crash now, as a bad pointer would (the handler's test) */
[[noreturn]] void crash_now();

}
