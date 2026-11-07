/*
 * log_capture.cpp - stderr into the Log view (log_capture.h).
 */
#include "log_capture.h"

#include <QMetaObject>

#include <cstdio>
#include <string>
#include <thread>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#define pipe(fds) _pipe(fds, 65536, _O_BINARY)
#define dup _dup
#define dup2 _dup2
#define read _read
#define write _write
#else
#include <unistd.h>
#endif

LogCapture &LogCapture::instance()
{
    static LogCapture *l = new LogCapture;  /* the reader outlives main() */
    return *l;
}

bool LogCapture::start(const QString &file)
{
    int fds[2];
    if(pipe(fds) != 0)
        return false;
    fflush(stderr);
    int original = dup(2);
    if(original < 0 || dup2(fds[1], 2) < 0)
        return false;
    setvbuf(stderr, nullptr, _IONBF, 0);
    FILE *log = file.isEmpty() ? nullptr : fopen(file.toLocal8Bit().constData(), "a");
    std::thread([this, in = fds[0], original, log] {
        std::string pending;
        char buf[4096];
        for(;;)
        {
            int n = (int)read(in, buf, sizeof buf);
            if(n <= 0)
                break;
            (void)!write(original, buf, n);
            if(log)
            {
                fwrite(buf, 1, n, log);
                fflush(log);
            }
            pending.append(buf, n);
            size_t nl;
            while((nl = pending.find('\n')) != std::string::npos)
            {
                QString text = QString::fromUtf8(pending.data(), (int)nl);
                pending.erase(0, nl + 1);
                QMetaObject::invokeMethod(this, [this, text] { emit line(text); }, Qt::QueuedConnection);
            }
        }
    }).detach();
    return true;
}
