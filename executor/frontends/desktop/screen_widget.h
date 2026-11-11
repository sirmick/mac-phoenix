#pragma once
/*
 * screen_widget.h - the Mac's screen in the window, and its input.
 *
 * Paints Emulator::frame(), only what changed (frameChanged: the damage,
 * in frame pixels, mapped to the widget). The frame is the Mac's screen
 * times the display scale (--executor-scale, Executor's own sharp
 * drawing, with Skia); the widget shows it at the window's size, keeping
 * the aspect, centred (logical pixels: on a 2x display a 2x frame is 1:1).
 * Mouse, wheel and keys go to executor_host in Mac pixels and Mac key
 * codes (keys.h); the pointer is the host's, shaped as the Mac's cursor.
 */
#include <QByteArray>
#include <QSet>
#include <QWidget>

class ScreenWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScreenWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    void setSmooth(bool on) { smooth_ = on; update(); }
    void setRightClickControl(bool on) { rightControl_ = on; }
    void setWheelScroll(bool on) { wheel_ = on; }
    /* The Mac's screen as shown (widget coordinates) */
    QRectF target() const;

public slots:
    void frameChanged();
    void setMacCursor(QByteArray data, QByteArray mask, int hotX, int hotY, bool visible);
    /* Every key and the button up (the window lost the focus) */
    void releaseAll();

signals:
    /* The Mac's screen changed size or scale: the window may follow */
    void geometryChanged();
    /* the widget's size changed (Auto screen size follows it) */
    void resized();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool focusNextPrevChild(bool) override { return false; }  // Tab is the Mac's
    bool event(QEvent *) override;

private:
    void moveTo(const QPointF &pos);
    void updateCursor();

    QSize frameSize_, macSize_;
    bool smooth_ = false, rightControl_ = true, wheel_ = true;
    bool controlForRight_ = false;
    QPointF wheelRest_;
    QSet<int> keysDown_;
    QByteArray curData_, curMask_;
    int hotX_ = 0, hotY_ = 0;
    bool curVisible_ = true;
};
