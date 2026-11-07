#pragma once
/*
 * emulator.h - the Executor session of the desktop application.
 *
 * Executor's state is the process's: one session a process (run() is not
 * re-entrant), so Emulator is a singleton. start() runs
 * executor_host::run() on a front-end thread of its own: it is
 * PhoenixVideoDriver's render loop, while the Toolbox runs on the thread
 * run() makes. The render loop's changed rectangles (rectsHook) are copied
 * into frame() and announced with frameChanged(), one at a time (the GUI
 * takes what accumulated). Requests (host/session.h request(): mount,
 * eject, resize, scale, flush, ...) wait for the running application's
 * next event call, where the Toolbox's world is consistent; replies come
 * back on the GUI thread.
 */
#include "session.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRegion>
#include <QString>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

class Emulator : public QObject
{
    Q_OBJECT
public:
    static Emulator &instance();

    /* Executor on a session::Options (session::parse_args) and the
       session's Setup. Once. */
    void start(executor_host::session::Options options, const executor_host::session::Setup &setup);
    bool running() const { return running_; }

    /* At the next event call: OP ARG (session.h request()), REPLY on the
       GUI thread with the JSON answer ("null" for an op host/ doesn't know). */
    void request(const QString &op, const QString &arg = {},
                 std::function<void(const QString &)> reply = {});

    /* The frame as last published: Mac pixels times the display scale
       (scale()), 0xFFRRGGBB (QImage::Format_RGB32). Lock frameMutex(). */
    QMutex &frameMutex() { return frameMutex_; }
    const QImage &frame() const { return frame_; }
    /* The Mac's screen in its own pixels (what clicks are in) and the
       display scale frames are drawn at. Any thread. */
    QSize macSize() const { return QSize(macWidth_, macHeight_); }
    double scale() const;
    /* Changed since the last call (frame pixels), clearing it. GUI thread. */
    QRegion takeDamage();

    /* The 1x frame (no cursor), for screenshots. Null before the first. */
    QImage frame1x() const;

signals:
    void frameChanged();
    /* The cursor (16x16, 1 bit a pixel, rows MSB first; mask opaque) */
    void cursorChanged(QByteArray data, QByteArray mask, int hotX, int hotY, bool visible);
    void ejected(QString path);
    void additions(QString path, QString json);
    void fatal(QString message);
    void finished(int code);

private:
    Emulator() = default;
    void onRects(const uint32_t *argb, int width, int height,
                 const std::vector<PhoenixVideoDriver::Rect> &rects);
    void onEventPoll();

    struct Request
    {
        QString op, arg;
        std::function<void(const QString &)> reply;
    };

    std::thread thread_;
    std::atomic<bool> running_{false};
    QMutex requestMutex_;
    std::vector<Request> requests_;

    QMutex frameMutex_;
    QImage frame_;
    QRegion damage_;
    bool announced_ = false;    // frameChanged() posted, not yet taken
    std::atomic<int> macWidth_{0}, macHeight_{0};
};
