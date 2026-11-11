/*
 * screen_widget.cpp - the Mac's screen and its input (screen_widget.h).
 */
#include "screen_widget.h"
#include "emulator.h"
#include "keys.h"

#include "executor_host.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QWheelEvent>

#include <cmath>

ScreenWidget::ScreenWidget(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setMinimumSize(256, 171);
    auto &e = Emulator::instance();
    connect(&e, &Emulator::frameChanged, this, &ScreenWidget::frameChanged);
    connect(&e, &Emulator::cursorChanged, this, &ScreenWidget::setMacCursor);
}

QSize ScreenWidget::sizeHint() const
{
    return macSize_.isValid() && !macSize_.isEmpty() ? macSize_ : QSize(640, 480);
}

QRectF ScreenWidget::target() const
{
    if(macSize_.isEmpty())
        return rect();
    /* each of the frame's pixels a whole number of the display's when
       that nearly fills the window (sharp pixels), else the largest that
       fits (smoothed: paintEvent). Counted in device pixels: at a
       fractional display scale (1.45) a whole number of logical pixels is
       a mix of 1- and 2-pixel-wide ones. */
    double dpr = devicePixelRatioF();
    QSize fs = frameSize_.isEmpty() ? macSize_ : frameSize_;
    double fit = std::min(width() * dpr / fs.width(), height() * dpr / fs.height());
    double whole = std::floor(fit + 1e-9);
    double z = whole >= 1.0 && whole >= 0.8 * fit ? whole : fit;
    double w = fs.width() * z / dpr, h = fs.height() * z / dpr;
    return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
}

void ScreenWidget::frameChanged()
{
    auto &e = Emulator::instance();
    QRegion damage = e.takeDamage();
    QSize fs, ms = e.macSize();
    {
        QMutexLocker lk(&e.frameMutex());
        fs = e.frame().size();
    }
    if(fs != frameSize_ || ms != macSize_)
    {
        frameSize_ = fs;
        macSize_ = ms;
        updateGeometry();
        updateCursor();
        emit geometryChanged();
        update();
        return;
    }
    QRectF t = target();
    double sx = t.width() / std::max(1, fs.width()), sy = t.height() / std::max(1, fs.height());
    QRegion out;
    for(const QRect &r : damage)
        out += QRectF(t.x() + r.x() * sx, t.y() + r.y() * sy, r.width() * sx, r.height() * sy)
                   .toAlignedRect()
                   .adjusted(-1, -1, 1, 1);
    update(out);
}

void ScreenWidget::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);
    auto &e = Emulator::instance();
    QRectF t = target();
    QRegion outside = QRegion(rect()) - QRegion(t.toAlignedRect());
    for(const QRect &r : outside & ev->region())
        p.fillRect(r, Qt::black);
    QMutexLocker lk(&e.frameMutex());
    const QImage &f = e.frame();
    if(f.isNull())
    {
        p.fillRect(t, Qt::black);
        return;
    }
    /* a frame pixel not a whole number of the display's (2x drawn, shown
       at 1.45; 1x filling the window): always smoothed, which keeps 2x
       text and evens out the pixels; whole numbers as the setting says */
    double per = t.width() * devicePixelRatioF() / std::max(1, f.width());
    bool whole = std::fabs(per - std::round(per)) < 0.01 && per >= 0.99;
    p.setRenderHint(QPainter::SmoothPixmapTransform, smooth_ || !whole);
    p.drawImage(t, f, QRectF(f.rect()));
}

void ScreenWidget::resizeEvent(QResizeEvent *)
{
    updateCursor();
}

void ScreenWidget::moveTo(const QPointF &pos)
{
    QRectF t = target();
    if(macSize_.isEmpty() || t.isEmpty())
        return;
    int x = (int)std::floor((pos.x() - t.x()) * macSize_.width() / t.width());
    int y = (int)std::floor((pos.y() - t.y()) * macSize_.height() / t.height());
    executor_host::mouse_moved(x, y);
}

void ScreenWidget::mouseMoveEvent(QMouseEvent *ev)
{
    moveTo(ev->position());
}

void ScreenWidget::mousePressEvent(QMouseEvent *ev)
{
    moveTo(ev->position());
    if(ev->button() == Qt::RightButton && rightControl_ && !controlForRight_)
    {
        /* Control-click: the contextual menu */
        controlForRight_ = true;
        executor_host::key(true, keys::MAC_CONTROL);
    }
    if(ev->button() == Qt::LeftButton || ev->button() == Qt::RightButton)
        executor_host::mouse_button(true);
}

void ScreenWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    moveTo(ev->position());
    if(ev->buttons() & (Qt::LeftButton | Qt::RightButton))
        return;
    executor_host::mouse_button(false);
    if(controlForRight_)
    {
        controlForRight_ = false;
        executor_host::key(false, keys::MAC_CONTROL);
    }
}

void ScreenWidget::wheelEvent(QWheelEvent *ev)
{
    if(!wheel_)
        return;
    moveTo(ev->position());
    /* lines: 120 eighths of a degree a notch, high-resolution wheels and
       touchpads summed until a line's worth */
    wheelRest_ += QPointF(ev->angleDelta()) / 120.0;
    int dh = (int)wheelRest_.x(), dv = (int)wheelRest_.y();
    wheelRest_ -= QPointF(dh, dv);
    if(dh || dv)
        executor_host::wheel(-dh, -dv);
    ev->accept();
}

bool ScreenWidget::event(QEvent *ev)
{
    /* Tab and Shift-Tab are the Mac's, not focus changes */
    if(ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease)
    {
        auto *k = static_cast<QKeyEvent *>(ev);
        if(k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab)
        {
            ev->type() == QEvent::KeyPress ? keyPressEvent(k) : keyReleaseEvent(k);
            return true;
        }
    }
    if(ev->type() == QEvent::ShortcutOverride)
    {
        /* the Mac's keys before the window's shortcuts (but F11) */
        auto *k = static_cast<QKeyEvent *>(ev);
        if(k->key() != Qt::Key_F11)
        {
            ev->accept();
            return true;
        }
    }
    return QWidget::event(ev);
}

void ScreenWidget::keyPressEvent(QKeyEvent *ev)
{
    if(ev->isAutoRepeat())
        return;  /* the Mac repeats keys itself */
    int code = keys::mac_keycode(ev);
    if(code < 0)
        return;
    keysDown_.insert(code);
    executor_host::key(true, (uint8_t)code);
}

void ScreenWidget::keyReleaseEvent(QKeyEvent *ev)
{
    if(ev->isAutoRepeat())
        return;
    int code = keys::mac_keycode(ev);
    if(code < 0)
        return;
    keysDown_.remove(code);
    executor_host::key(false, (uint8_t)code);
}

void ScreenWidget::focusOutEvent(QFocusEvent *)
{
    releaseAll();
}

void ScreenWidget::releaseAll()
{
    for(int code : keysDown_)
        executor_host::key(false, (uint8_t)code);
    keysDown_.clear();
    executor_host::mouse_button(false);
    if(controlForRight_)
    {
        controlForRight_ = false;
        executor_host::key(false, keys::MAC_CONTROL);
    }
}

void ScreenWidget::setMacCursor(QByteArray data, QByteArray mask, int hotX, int hotY, bool visible)
{
    curData_ = data;
    curMask_ = mask;
    hotX_ = hotX;
    hotY_ = hotY;
    curVisible_ = visible;
    updateCursor();
}

/* The Mac's cursor at the size the Mac is shown (sharp pixels: each the
   zoom's square), black where data, white where only mask */
void ScreenWidget::updateCursor()
{
    if(curData_.size() != 32 || curMask_.size() != 32)
        return;
    if(!curVisible_)
    {
        setCursor(Qt::BlankCursor);
        return;
    }
    double zoom = macSize_.isEmpty() ? 1.0 : target().width() / macSize_.width();
    double dpr = devicePixelRatioF();
    int px = std::max(16, (int)std::lround(16 * zoom * dpr));
    QImage img(16, 16, QImage::Format_ARGB32);
    for(int y = 0; y < 16; y++)
    {
        int d = (uint8_t)curData_[2 * y] << 8 | (uint8_t)curData_[2 * y + 1];
        int m = (uint8_t)curMask_[2 * y] << 8 | (uint8_t)curMask_[2 * y + 1];
        for(int x = 0; x < 16; x++)
        {
            int bit = 0x8000 >> x;
            img.setPixel(x, y, (d & bit) ? 0xFF000000 : (m & bit) ? 0xFFFFFFFF : 0x00000000);
        }
    }
    QPixmap pm = QPixmap::fromImage(img.scaled(px, px, Qt::IgnoreAspectRatio, Qt::FastTransformation));
    pm.setDevicePixelRatio(dpr);
    setCursor(QCursor(pm, (int)std::lround(hotX_ * px / 16.0 / dpr), (int)std::lround(hotY_ * px / 16.0 / dpr)));
}
