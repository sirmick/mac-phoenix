/*
 * main_window.cpp - the window (main_window.h).
 */
#include "main_window.h"
#include "audio.h"
#include "disks.h"
#include "emulator.h"
#include "keys.h"
#include "log_capture.h"
#include "screen_widget.h"
#include "settings.h"
#include "settings_dialog.h"

#include "executor_host.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>

#include <cstdio>
#include <cstdlib>

MainWindow::MainWindow(Settings &settings, Disks &disks, const Automation &automation,
                       const QStringList &restartArgs)
    : settings_(settings), disks_(disks), automation_(automation), restartArgs_(restartArgs)
{
    setWindowTitle("Executor");
    setAcceptDrops(true);
    screen_ = new ScreenWidget;
    setCentralWidget(screen_);
    mounted_ = settings_.list("disks.mounted");
    buildToolBar();
    buildDisks();
    buildLog();
    appLabel_ = new QLabel;
    cpuLabel_ = new QLabel;
    statusBar()->addWidget(appLabel_, 1);
    statusBar()->addPermanentWidget(cpuLabel_);

    auto &e = Emulator::instance();
    connect(screen_, &ScreenWidget::geometryChanged, this, &MainWindow::sizeToMac);
    connect(&e, &Emulator::finished, this, [this](int rc) {
        fprintf(stderr, "[desktop] Executor finished (%d)\n", rc);
        quit(rc);
    });
    connect(&e, &Emulator::fatal, this, [this](const QString &m) {
        statusBar()->showMessage("Executor stopped: " + m);
    });
    connect(&e, &Emulator::ejected, this, [this](const QString &path) {
        QString image = disks_.imageFor(path);
        fprintf(stderr, "[desktop] ejected %s\n", image.toLocal8Bit().constData());
        mounted_.removeAll(image);
        settings_.set("disks.mounted", QJsonArray::fromStringList(mounted_));
        showDisks();
    });
    connect(&e, &Emulator::additions, this, [this](const QString &path, const QString &json) {
        QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
        QStringList restart;
        for(const QJsonValue &v : o.value("restart").toArray())
            restart << v.toString();
        QString msg = QString("%1: %2 files added to the System Folder")
                          .arg(QFileInfo(disks_.imageFor(path)).fileName())
                          .arg(o.value("copied").toInt());
        if(!restart.isEmpty())
            msg += "; restart for " + restart.join(", ");
        statusBar()->showMessage(msg, 10000);
    });
    connect(&disks_, &Disks::libraryChanged, this, &MainWindow::showDisks);
    /* live settings changed anywhere (the dialog, the tool bar) */
    connect(&settings_, &Settings::changed, this, [this](const QString &key) {
        if(settings_.live(key))
            QMetaObject::invokeMethod(this, [this, key] { applyLive(key); }, Qt::QueuedConnection);
    });

    fitTimer_ = new QTimer(this);
    fitTimer_->setSingleShot(true);
    fitTimer_->setInterval(300);
    connect(fitTimer_, &QTimer::timeout, this, &MainWindow::fitScreen);
    auto *poller = new QTimer(this);
    connect(poller, &QTimer::timeout, this, &MainWindow::poll);
    poller->start(250);
    if(automation_.timeout > 0)
        QTimer::singleShot((int)(automation_.timeout * 1000), this, [this] {
            fprintf(stderr, "[desktop] --timeout: %s\n", shotTaken_ ? "done" : "no screenshot yet");
            quit(shotTaken_ ? 0 : 1);
        });
    applyLive({});
    QByteArray geometry = QByteArray::fromBase64(settings_.desktop("geometry").toString().toLatin1());
    if(!geometry.isEmpty())
    {
        restoreGeometry(geometry);
        sized_ = true;
    }
}

void MainWindow::buildToolBar()
{
    auto *bar = addToolBar("Executor");
    bar->setObjectName("toolbar");
    bar->setMovable(false);
    bar->addAction("Settings…", this, &MainWindow::openSettings);
    bar->addAction("Disks", this, [this] { disksDock_->setVisible(!disksDock_->isVisible()); });
    bar->addSeparator();

    /* the screen size: the schema's sizes, and Auto (the window's) */
    QJsonObject screen = settings_.property("machine.screen");
    sizeBox_ = new QComboBox;
    sizeBox_->addItem("Auto (window size)", "auto");
    QJsonArray examples = screen.value("examples").toArray(), labels = screen.value("x-labels").toArray();
    for(int i = 0; i < examples.size(); i++)
        sizeBox_->addItem(i < labels.size() ? labels[i].toString() : examples[i].toString(), examples[i].toString());
    sizeBox_->setToolTip(screen.value("description").toString());
    auto syncSize = [this] {
        QString want = settings_.boolean("display.fit_window") ? "auto" : settings_.str("machine.screen");
        int at = sizeBox_->findData(want);
        if(at < 0)
        {
            sizeBox_->addItem(want, want);
            at = sizeBox_->count() - 1;
        }
        QSignalBlocker b(sizeBox_);
        sizeBox_->setCurrentIndex(at);
    };
    syncSize();
    connect(sizeBox_, &QComboBox::activated, this, [this](int i) {
        QString v = sizeBox_->itemData(i).toString();
        if(v == "auto")
            settings_.set("display.fit_window", true);
        else
        {
            settings_.set("display.fit_window", false);
            settings_.set("machine.screen", v);
        }
    });
    connect(&settings_, &Settings::changed, sizeBox_, [syncSize](const QString &key) {
        if(key == "machine.screen" || key == "display.fit_window")
            syncSize();
    });
    bar->addWidget(sizeBox_);

    hidpiBox_ = new QCheckBox("HiDPI");
    hidpiBox_->setToolTip(settings_.property("display.scale").value("description").toString());
    connect(hidpiBox_, &QCheckBox::clicked, this, [this](bool on) { settings_.set("display.scale", on ? 2 : 1); });
    bar->addWidget(hidpiBox_);
    bar->addSeparator();
    bar->addAction("Screenshot", this, &MainWindow::screenshot);
    auto *full = bar->addAction("Fullscreen", this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    full->setShortcut(Qt::Key_F11);
    addAction(full);
    bar->addAction("Log", this, [this] { logDock_->setVisible(!logDock_->isVisible()); });
    bar->addSeparator();
    bar->addAction("Restart", this, [this] {
        if(QMessageBox::question(this, "Restart", "Restart Executor? (Like switching a Mac off and on.)")
           == QMessageBox::Yes)
            restart();
    });
}

void MainWindow::buildDisks()
{
    disksDock_ = new QDockWidget("Disks", this);
    disksDock_->setObjectName("disks");
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *help = new QLabel(settings_.group("disks").value("description").toString()
                            + " Tick a disk to put it in the Mac, untick to eject it.");
    help->setWordWrap(true);
    v->addWidget(help);
    diskList_ = new QListWidget;
    v->addWidget(diskList_, 1);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton("Add Image…");
    auto *folder = new QPushButton("Add Folder…");
    auto *revert = new QPushButton("Revert");
    auto *remove = new QPushButton("Remove");
    revert->setToolTip("Throw away the Mac's changes to the selected disk (not while it's in the Mac)");
    buttons->addWidget(add);
    buttons->addWidget(folder);
    buttons->addWidget(revert);
    buttons->addWidget(remove);
    v->addLayout(buttons);
    disksDock_->setWidget(w);
    addDockWidget(Qt::RightDockWidgetArea, disksDock_);
    disksDock_->hide();
    /* shown: a third of the window at most, the Mac keeps the rest */
    connect(disksDock_, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if(shown)
            resizeDocks({ disksDock_ }, { std::min(320, width() / 3) }, Qt::Horizontal);
    });

    connect(add, &QPushButton::clicked, this, [this] {
        for(const QString &f : QFileDialog::getOpenFileNames(this, "Add disk images", QString(),
                                                              "Disk images (*.img *.image *.dsk *.hfs *.iso *.toast *.cdr);;All files (*)"))
            disks_.addImage(f);
    });
    connect(folder, &QPushButton::clicked, this, [this] {
        QString d = QFileDialog::getExistingDirectory(this, "Add a folder of disk images");
        if(!d.isEmpty())
            disks_.addFolder(d);
    });
    connect(revert, &QPushButton::clicked, this, [this] {
        auto *item = diskList_->currentItem();
        if(!item)
            return;
        QString image = item->data(Qt::UserRole).toString();
        if(mounted_.contains(image))
            statusBar()->showMessage("Eject it first", 5000);
        else if(QMessageBox::question(this, "Revert", "Throw away the Mac's changes to " + item->text() + "?")
                == QMessageBox::Yes)
        {
            disks_.revert(image);
            showDisks();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        auto *item = diskList_->currentItem();
        if(item && !mounted_.contains(item->data(Qt::UserRole).toString()))
            disks_.removeImage(item->data(Qt::UserRole).toString());
    });
    connect(diskList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        QString image = item->data(Qt::UserRole).toString();
        bool in = mounted_.contains(image) || pendingMounts_.contains(image);
        if(item->checkState() == Qt::Checked && !in)
            mount(image);
        else if(item->checkState() == Qt::Unchecked && in)
            eject(image);
    });
    showDisks();
}

void MainWindow::showDisks()
{
    QSignalBlocker b(diskList_);
    QString current = diskList_->currentItem() ? diskList_->currentItem()->data(Qt::UserRole).toString() : QString();
    diskList_->clear();
    QString system = settings_.str("machine.system");
    QList<DiskInfo> lib = disks_.library();
    /* disks in the Mac that aren't in the library (the command line's) too */
    for(const QString &m : mounted_)
        if(std::none_of(lib.begin(), lib.end(), [&](const DiskInfo &d) { return d.path == m; }))
            lib << disks_.info(m);
    for(const DiskInfo &d : lib)
    {
        QString text = d.title;
        if(d.path == system)
            text += "  (the System)";
        if(disks_.hasChanges(d.path))
            text += "  — changed";
        auto *item = new QListWidgetItem(text, diskList_);
        item->setData(Qt::UserRole, d.path);
        item->setToolTip(d.path + (d.note.isEmpty() ? QString() : "\n" + d.note));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(mounted_.contains(d.path) || pendingMounts_.contains(d.path) ? Qt::Checked : Qt::Unchecked);
        if(d.path == current)
            diskList_->setCurrentItem(item);
    }
}

void MainWindow::mount(const QString &image)
{
    QString err;
    QString path = disks_.mountPath(image, &err);
    if(path.isEmpty())
    {
        statusBar()->showMessage(err, 8000);
        showDisks();
        return;
    }
    QString op = Disks::isCD(image) ? "mount-cd"
               : disks_.additionsFor(image, settings_.str("machine.system")) ? "mount-additions" : "mount";
    pendingMounts_ << image;
    Emulator::instance().request(op, path, [this, image](const QString &r) {
        pendingMounts_.removeAll(image);
        fprintf(stderr, "[desktop] mount %s: %s\n", image.toLocal8Bit().constData(), r.toLocal8Bit().constData());
        if(r == "0")
        {
            mounted_ << image;
            settings_.set("disks.mounted", QJsonArray::fromStringList(mounted_));
        }
        else
            statusBar()->showMessage(QFileInfo(image).fileName() + ": not mounted ("
                                         + (r == "-56" ? "no Mac volume on it" : r == "-1" ? "can't open it" : r) + ")",
                                     8000);
        showDisks();
    });
}

void MainWindow::eject(const QString &image)
{
    QString path = Disks::isCD(image) ? image : disks_.mountPath(image);
    Emulator::instance().request("eject", path, [this, image](const QString &r) {
        fprintf(stderr, "[desktop] eject %s: %s\n", image.toLocal8Bit().constData(), r.toLocal8Bit().constData());
        if(r != "0")
        {
            statusBar()->showMessage(QFileInfo(image).fileName() + " is in use: quit what has files open on it, then eject it",
                                     8000);
            showDisks();
        }
        /* gone from mounted_ when Hooks::ejected says so */
    });
}

void MainWindow::buildLog()
{
    logDock_ = new QDockWidget("Log", this);
    logDock_->setObjectName("log");
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFont("monospace"));
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    logDock_->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    logDock_->hide();
    connect(&LogCapture::instance(), &LogCapture::line, log_, &QPlainTextEdit::appendPlainText);
}

/* x-choices "systems": Executor's own, and the library's System disks */
SettingsDialog::Choices MainWindow::systemChoices()
{
    return [this](const QString &name) {
        QList<QPair<QString, QString>> out;
        if(name == "systems")
        {
            out << qMakePair(QString(), QString("Executor's own"));
            for(const DiskInfo &d : disks_.library())
                if(!d.system.isEmpty())
                    out << qMakePair(d.path, d.title);
        }
        return out;
    };
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(settings_, systemChoices(), this);
    connect(&dialog, &SettingsDialog::applied, this, &MainWindow::settingChanged);
    dialog.exec();
}

void MainWindow::settingChanged(const QStringList &keys)
{
    QStringList titles;
    for(const QString &k : keys)
        if(!settings_.live(k))
            titles << settings_.property(k).value("title").toString();
    if(!titles.isEmpty()
       && QMessageBox::question(this, "Restart", titles.join(", ") + ": Executor restarts to apply. Restart now?")
              == QMessageBox::Yes)
        restart();
}

/* A live setting changed (KEY), or all of them as they are now (empty:
   the start, Executor not running yet) */
void MainWindow::applyLive(const QString &key)
{
    keys::set_command_key(settings_.str("input.command_key"));
    screen_->setRightClickControl(settings_.str("input.right_click") == "control");
    screen_->setWheelScroll(settings_.boolean("input.wheel_scroll"));
    screen_->setSmooth(settings_.boolean("display.smoothing"));
    audio::set_enabled(settings_.boolean("sound.enabled"));
    audio::set_volume(settings_.integer("sound.volume") / 100.0);
    auto &e = Emulator::instance();
    if(!e.running())
        return;
    if(key == "display.fit_window" || key == "machine.screen")
    {
        if(settings_.boolean("display.fit_window"))
            fitTimer_->start();
        else
            e.request("resize", settings_.str("machine.screen"));
    }
    if(key == "display.scale")
    {
        int scale = settings_.integer("display.scale");
        if(scale == 0)
            scale = std::max(1, (int)std::lround(devicePixelRatioF()));
        e.request("scale", QString::number(scale), [this](const QString &r) {
            QJsonObject o = QJsonDocument::fromJson(r.toUtf8()).object();
            if(!o.value("ok").toBool())
            {
                statusBar()->showMessage("HiDPI needs Executor built with -DEXECUTOR_SKIA=ON", 8000);
                QSignalBlocker b(hidpiBox_);
                hidpiBox_->setChecked(false);
            }
        });
    }
}

/* Auto screen size: the Mac's screen follows the window */
void MainWindow::fitScreen()
{
    if(!settings_.boolean("display.fit_window") || !Emulator::instance().running())
        return;
    QSize s = screen_->size();
    Emulator::instance().request("resize", QString("%1x%2").arg(s.width() & ~7).arg(s.height()));
}

/* The window at the Mac's size the first time (and when it changes, unless
   the window decides it or it's full screen) */
void MainWindow::sizeToMac()
{
    QSize mac = Emulator::instance().macSize();
    bool scaleOn = Emulator::instance().scale() > 1.01;
    {
        QSignalBlocker b(hidpiBox_);
        hidpiBox_->setChecked(scaleOn);
    }
    if(mac.isEmpty() || isFullScreen() || isMaximized() || settings_.boolean("display.fit_window"))
        return;
    QSize extra = size() - screen_->size();
    QSize want = mac + extra;
    if(QScreen *s = screen())
        want = want.boundedTo(s->availableSize());
    if(!sized_ || screen_->size() != mac)
        resize(want);
    sized_ = true;
}

void MainWindow::changeEvent(QEvent *ev)
{
    if(ev->type() == QEvent::ActivationChange && !isActiveWindow())
        screen_->releaseAll();
    QMainWindow::changeEvent(ev);
}

void MainWindow::screenshot()
{
    auto &e = Emulator::instance();
    QImage shot1x = e.frame1x();
    if(shot1x.isNull())
        return;
    QString dir = settings_.dataDir() + "/screenshots";
    QDir().mkpath(dir);
    QString base = dir + "/executor-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    shot1x.save(base + ".png");
    QString saved = base + ".png";
    if(e.scale() > 1.01)
    {
        QMutexLocker lk(&e.frameMutex());
        QImage scaled = e.frame().copy();
        lk.unlock();
        scaled.save(base + "-hidpi.png");
        QApplication::clipboard()->setImage(scaled);
    }
    else
        QApplication::clipboard()->setImage(shot1x);
    fprintf(stderr, "[desktop] screenshot %s\n", saved.toLocal8Bit().constData());
    statusBar()->showMessage("Screenshot: " + saved + " (and the clipboard)", 8000);
}

/* Four times a second: the front application, the CPU meter, the
   automation's screenshot */
void MainWindow::poll()
{
    auto &e = Emulator::instance();
    if(!e.running())
        return;
    std::string app = executor_host::current_app_name();
    if(QString::fromStdString(app) != app_)
    {
        app_ = QString::fromStdString(app);
        appLabel_->setText(app_);
        setWindowTitle(app_.isEmpty() ? "Executor" : app_ + " — Executor");
    }
    double busy = executor_host::cpu_busy();
    if(busy >= 0)
        cpuLabel_->setText(QString("CPU %1%").arg((int)std::lround(busy * 100)));
    if(!idleSeen_ && executor_host::app_idle() && !app.empty() && app != "mac-phoenix")
    {
        idleSeen_ = true;
        fprintf(stderr, "[desktop] %s is in its event loop\n", app.c_str());
        for(const QString &r : automation_.requests)
        {
            QString op = r.section(' ', 0, 0), arg = r.section(' ', 1);
            if(op == "ui")
            {
                /* the window's own: "settings" (the dialog, grabbed beside
                   the screenshot as PNG-settings.png), "disks", "log" */
                if(arg == "disks")
                    disksDock_->show();
                else if(arg == "log")
                    logDock_->show();
                else if(arg == "settings")
                {
                    auto *dialog = new SettingsDialog(settings_, systemChoices(), this);
                    dialog->show();
                    QString png = automation_.screenshot;
                    png.insert(png.lastIndexOf('.') < 0 ? png.size() : png.lastIndexOf('.'), "-settings");
                    QTimer::singleShot(500, dialog, [dialog, png] { dialog->grab().save(png); });
                }
                continue;
            }
            if(op == "mount" || op == "eject")
            {
                /* by the image, as the Disks panel does it */
                QString image = QFileInfo(arg).absoluteFilePath();
                disks_.addImage(image);
                op == "mount" ? mount(image) : eject(image);
                continue;
            }
            e.request(op, arg, [r](const QString &reply) {
                fprintf(stderr, "[desktop] request %s: %s\n", r.toLocal8Bit().constData(),
                        reply.toLocal8Bit().constData());
            });
        }
        if(!automation_.screenshot.isEmpty())
            QTimer::singleShot((int)(automation_.screenshotDelay * 1000), this, [this] {
                QImage shot = Emulator::instance().frame1x();
                bool ok = !shot.isNull() && shot.save(automation_.screenshot);
                fprintf(stderr, "[desktop] screenshot %s: %s (%dx%d, app %s)\n",
                        automation_.screenshot.toLocal8Bit().constData(), ok ? "written" : "FAILED",
                        shot.width(), shot.height(), app_.toLocal8Bit().constData());
                /* and the window as shown: the frame path through the widget */
                QString window = automation_.screenshot;
                window.insert(window.lastIndexOf('.') < 0 ? window.size() : window.lastIndexOf('.'), "-window");
                ok = grab().save(window) && ok;
                /* and Executor's frame at the display scale, if above 1 */
                auto &e = Emulator::instance();
                if(e.scale() > 1.01)
                {
                    QString hidpi = automation_.screenshot;
                    hidpi.insert(hidpi.lastIndexOf('.') < 0 ? hidpi.size() : hidpi.lastIndexOf('.'), "-hidpi");
                    QMutexLocker lk(&e.frameMutex());
                    e.frame().save(hidpi);
                }
                shotTaken_ = ok;
                if(automation_.quitAfter)
                    quit(ok ? 0 : 1);
            });
    }
}

void MainWindow::quit(int code)
{
    if(quitting_)
        return;
    quitting_ = true;
    settings_.setDesktop("geometry", QString::fromLatin1(saveGeometry().toBase64()));
    auto done = [code] {
        audio::close();
        fflush(stdout);
        fflush(stderr);
        std::_Exit(code);
    };
    auto &e = Emulator::instance();
    if(!e.running())
        done();
    /* the volumes' changes written out at the Mac's next event call; at
       most a few seconds (a Mac stuck in a modal loop doesn't make one) */
    e.request("flush", {}, [done](const QString &r) {
        fprintf(stderr, "[desktop] %s volumes flushed\n", r.toLocal8Bit().constData());
        done();
    });
    QTimer::singleShot(3000, this, [done] {
        fprintf(stderr, "[desktop] no event call to flush at: quitting\n");
        done();
    });
}

void MainWindow::restart()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), restartArgs_);
    quit(0);
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    ev->ignore();
    quit(0);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    if(ev->mimeData()->hasUrls())
        ev->acceptProposedAction();
}

/* Disk images dropped on the window: into the library and the Mac */
void MainWindow::dropEvent(QDropEvent *ev)
{
    for(const QUrl &u : ev->mimeData()->urls())
    {
        QString f = u.toLocalFile();
        if(f.isEmpty() || !Disks::isImage(f))
            continue;
        disks_.addImage(f);
        if(!mounted_.contains(f))
            mount(f);
    }
}
