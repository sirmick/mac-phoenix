#pragma once
/*
 * main_window.h - the desktop application's window: the Mac's screen, a
 * tool bar (Settings, Disks, screen size, HiDPI, Screenshot, Fullscreen,
 * Log, Restart), the Disks and Log panels, the front application and the
 * CPU meter in the status bar.
 */
#include "settings_dialog.h"

#include <QMainWindow>
#include <QStringList>

class Disks;
class ScreenWidget;
class Settings;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTimer;

struct Automation
{
    QString screenshot;          // --screenshot PATH: once the Finder is idle
    double screenshotDelay = 3;  // seconds after that
    bool quitAfter = false;      // --quit: then quit
    double timeout = 0;          // --timeout S: quit regardless (exit 1 if no screenshot yet)
    QStringList requests;        // --request "OP ARG": then (mount/eject IMAGE as the Disks panel)
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(Settings &settings, Disks &disks, const Automation &automation,
               const QStringList &restartArgs);

    /* Flush the Mac's volumes, then exit the process with CODE (Executor's
       threads can't be stopped: the process goes) */
    void quit(int code = 0);
    /* The images in the Mac at the start */
    void setMounted(const QStringList &images) { mounted_ = images; showDisks(); }
    /* The same, then the application again (settings that need a new session) */
    void restart();

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void changeEvent(QEvent *) override;

private:
    void buildToolBar();
    void buildDisks();
    void buildLog();
    void showDisks();
    void settingChanged(const QStringList &keys);
    void applyLive(const QString &key);
    void fitScreen();
    void sizeToMac();
    void screenshot();
    void poll();
    void mount(const QString &image);
    void eject(const QString &image);
    void openSettings();
    SettingsDialog::Choices systemChoices();

    Settings &settings_;
    Disks &disks_;
    Automation automation_;
    QStringList restartArgs_;
    ScreenWidget *screen_;
    QComboBox *sizeBox_ = nullptr;
    QCheckBox *hidpiBox_ = nullptr;
    QDockWidget *disksDock_ = nullptr, *logDock_ = nullptr;
    QListWidget *diskList_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QLabel *appLabel_ = nullptr, *cpuLabel_ = nullptr;
    QTimer *fitTimer_ = nullptr;
    QString app_;
    bool idleSeen_ = false, shotTaken_ = false, sized_ = false, quitting_ = false;
    QStringList mounted_;        // images in the Mac now
    QStringList pendingMounts_;  // being put in
};
