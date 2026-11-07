/*
 * emulator.cpp - the Executor session (emulator.h).
 */
#include "emulator.h"

#include "executor_host.h"
#include "phoenix.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <cstring>

namespace session = executor_host::session;

Emulator &Emulator::instance()
{
    static Emulator *e = new Emulator;  /* never destroyed: Executor's threads outlive main() */
    return *e;
}

double Emulator::scale() const
{
    int w = macWidth_;
    QMutexLocker lk(const_cast<QMutex *>(&frameMutex_));
    return w > 0 && !frame_.isNull() ? (double)frame_.width() / w : 1.0;
}

QRegion Emulator::takeDamage()
{
    QMutexLocker lk(&frameMutex_);
    QRegion r = damage_;
    damage_ = QRegion();
    announced_ = false;
    return r;
}

QImage Emulator::frame1x() const
{
    std::vector<uint32_t> px;
    int w, h;
    if(!session::frame_rgba(px, w, h))
        return {};
    return QImage((const uchar *)px.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
}

/* On the render thread: the changed rectangles into frame_ (a new size:
   a new image, the whole frame among the rectangles then). */
void Emulator::onRects(const uint32_t *argb, int width, int height,
                       const std::vector<PhoenixVideoDriver::Rect> &rects)
{
    if(Executor::vdriver)
    {
        macWidth_ = Executor::vdriver->width();
        macHeight_ = Executor::vdriver->height();
    }
    bool post;
    {
        QMutexLocker lk(&frameMutex_);
        if(frame_.width() != width || frame_.height() != height)
        {
            frame_ = QImage(width, height, QImage::Format_RGB32);
            frame_.fill(Qt::black);
            damage_ = QRect(0, 0, width, height);
        }
        for(const auto &r : rects)
        {
            int w = r.right - r.left;
            if(w <= 0)
                continue;
            for(int y = r.top; y < r.bottom; y++)
                memcpy(frame_.scanLine(y) + (size_t)r.left * 4, argb + (size_t)y * width + r.left,
                       (size_t)w * 4);
            damage_ += QRect(r.left, r.top, w, r.bottom - r.top);
        }
        post = !announced_ && !damage_.isEmpty();
        if(post)
            announced_ = true;
    }
    if(post)
        QMetaObject::invokeMethod(this, [this] { emit frameChanged(); }, Qt::QueuedConnection);
}

void Emulator::request(const QString &op, const QString &arg, std::function<void(const QString &)> reply)
{
    QMutexLocker lk(&requestMutex_);
    requests_.push_back({ op, arg, std::move(reply) });
}

/* At an event call of the running application (the Toolbox's thread). */
void Emulator::onEventPoll()
{
    session::event_call_begin();
    std::vector<Request> todo;
    {
        QMutexLocker lk(&requestMutex_);
        todo.swap(requests_);
    }
    for(Request &r : todo)
    {
        std::string op = r.op.toStdString(), arg = r.arg.toStdString();
        auto answer = session::request(op, arg);
        QString json = answer ? QString::fromStdString(*answer) : QStringLiteral("null");
        if(r.reply)
            QMetaObject::invokeMethod(this, [reply = std::move(r.reply), json] { reply(json); },
                                      Qt::QueuedConnection);
    }
    session::event_call_end();
}

void Emulator::start(session::Options o, const session::Setup &setup)
{
    session::Hooks hooks;
    hooks.ejected = [this](const std::string &path) {
        QString p = QString::fromStdString(path);
        QMetaObject::invokeMethod(this, [this, p] { emit ejected(p); }, Qt::QueuedConnection);
    };
    hooks.additions = [this](const std::string &path, const std::string &json) {
        QString p = QString::fromStdString(path), j = QString::fromStdString(json);
        QMetaObject::invokeMethod(this, [this, p, j] { emit additions(p, j); }, Qt::QueuedConnection);
    };
    session::prepare(o, setup, hooks);
    executor_host::Config c = o.config;

    PhoenixVideoDriver::rectsHook = [this](const uint32_t *argb, int w, int h,
                                           const std::vector<PhoenixVideoDriver::Rect> &rects) {
        onRects(argb, w, h, rects);
    };
    c.on_cursor = [this](const uint8_t data[32], const uint8_t mask[32], int hx, int hy, bool visible) {
        QByteArray d((const char *)data, 32), m((const char *)mask, 32);
        QMetaObject::invokeMethod(this, [=] { emit cursorChanged(d, m, hx, hy, visible); },
                                  Qt::QueuedConnection);
    };
    executor_host::set_event_poll_hook([this] { onEventPoll(); });
    executor_host::set_fatal_hook([this](const char *message) {
        QString m = QString::fromUtf8(message);
        QMetaObject::invokeMethod(this, [this, m] { emit fatal(m); }, Qt::QueuedConnection);
    });
    running_ = true;
    thread_ = std::thread([this, c] {
        int rc = executor_host::run(c);
        running_ = false;
        QMetaObject::invokeMethod(this, [this, rc] { emit finished(rc); }, Qt::QueuedConnection);
    });
    thread_.detach();
}
