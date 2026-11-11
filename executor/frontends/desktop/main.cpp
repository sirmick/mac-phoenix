/*
 * main.cpp - Executor as a desktop application (README.md).
 *
 *   executor-desktop [--data DIR] [--system IMAGE] [--disk IMAGE]... [--ppc]
 *                    [--size WxH] [--scale N] [--ram MB] [--depth BPP]
 *                    [--screenshot PNG [--screenshot-delay S] [--quit]]
 *                    [--timeout S] [--request "OP ARG"]... [APP]
 *
 * --request: once the Finder (the application) is in its event loop, an
 * automation request (host/session.h request(): resize WxH, scale N,
 * windows, ...; mount and eject IMAGE as the Disks panel does them), the
 * reply in the log; then --screenshot.
 *
 * The data folder (default: the OS's application data folder,
 * QStandardPaths::AppDataLocation) holds settings.json, desktop.json,
 * Executor's volumes (volumes/NAME/executor: one a System, its System Folder and
 * preferences: what the Mac saves there stays), the disks' overlays
 * (overlays/), screenshots/ and logs/. It is the store: nothing is
 * anyone else's copy. The command line's choices are for this run only,
 * as mac-phoenix's are: settings.json keeps the dialog's.
 *
 * The session: the settings as the tab's arguments (worker.js) through
 * session::parse_args and prepare (executor/host), then
 * executor_host::run() on a thread of its own (emulator.h).
 */
#include "audio.h"
#include "disks.h"
#include "emulator.h"
#include "log_capture.h"
#include "main_window.h"
#include "settings.h"
#ifdef _WIN32
#include "windows_support.h"
#endif

#include "executor_host.h"
#include "overlay.h"
#include "session.h"

#include <base/common.h>
#include <hfs/hfs.h>
#include <hfs/image_io.h>

#include <QApplication>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#include <cmath>
#include <cstdio>

namespace session = executor_host::session;

namespace {

/* Executor's volume for a System: one for its own and one per System
   image, so each keeps its System Folder and preferences. The folder's
   name is the volume's: "executor", as in the tab. */
QString volume_for(const QString &data, const QString &system)
{
    if(system.isEmpty())
        return data + "/volumes/Executor/executor";
    QFileInfo fi(system);
    QString hash = QCryptographicHash::hash(fi.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    return data + "/volumes/" + fi.completeBaseName() + "-" + hash + "/executor";
}

}

int main(int argc, char **argv)
{
    QApplication::setApplicationName("Executor");
    QApplication::setOrganizationName("MacPhoenix");
    QApplication::setApplicationDisplayName("Executor");
    QApplication app(argc, argv);
    /* A system with no fonts (a server, a bare container): the ones the
       package carries (tools/desktop/appimage.sh: share/executor/fonts
       beside bin/) */
    QDir fonts(QCoreApplication::applicationDirPath() + "/../share/executor/fonts");
    QStringList systemFonts = QFontDatabase::families();
    if(fonts.exists() && (systemFonts.isEmpty() || !systemFonts.contains(QFontInfo(QApplication::font()).family())))
    {
        QString family;
        for(const QString &f : fonts.entryList({ "*.ttf", "*.otf" }, QDir::Files))
        {
            QStringList added = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(fonts.filePath(f)));
            if(family.isEmpty() && !added.isEmpty())
                family = added[0];
        }
        if(!family.isEmpty())
        {
            QApplication::setFont(QFont(family));
            fprintf(stderr, "[desktop] no system fonts (%d families): %s\n", (int)systemFonts.size(),
                    family.toLocal8Bit().constData());
        }
    }

    /* the command line: ours, and this run's settings */
    QStringList args = app.arguments().mid(1);
    QString data, system;
    bool systemSet = false;
    QStringList disks;
    bool disksSet = false;
    Automation automation;
    QStringList passThrough;  /* for session::parse_args */
    QString size, scale, ram, depth;
    bool ppc = false, crashTest = false, crashTestHeap = false;
    for(int i = 0; i < args.size(); i++)
    {
        const QString &a = args[i];
        auto next = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
        if(a == "--data")
            data = next();
        else if(a == "--system")
            system = next(), systemSet = true;
        else if(a == "--disk" || a == "--cdrom")
            disks << QFileInfo(next()).absoluteFilePath(), disksSet = true;
        else if(a == "--ppc")
            ppc = true;
        else if(a == "--size")
            size = next();
        else if(a == "--scale")
            scale = next();
        else if(a == "--ram")
            ram = next();
        else if(a == "--depth")
            depth = next();
        else if(a == "--screenshot")
            automation.screenshot = next();
        else if(a == "--screenshot-delay")
            automation.screenshotDelay = next().toDouble();
        else if(a == "--quit")
            automation.quitAfter = true;
        else if(a == "--timeout")
            automation.timeout = next().toDouble();
        else if(a == "--request")
            automation.requests << next();
        else if(a == "--console")
        {
#ifdef _WIN32
            windows_support::attach_console();
#endif
        }
        else if(a == "--crash-test")
            crashTest = true;
        else if(a == "--crash-test-heap")
            crashTest = crashTestHeap = true;
        else if(a == "--help" || a == "-h")
        {
            printf("executor-desktop [--data DIR] [--system IMAGE] [--disk IMAGE]... [--ppc] [--size WxH]\n"
                   "                 [--scale N] [--ram MB] [--depth BPP] [--screenshot PNG\n"
                   "                 [--screenshot-delay S] [--quit]] [--timeout S]\n"
                   "                 [--request \"OP ARG\"]... [--console] [APP]\n");
            return 0;
        }
        else if(!a.startsWith('-') && Disks::isImage(a) && QFileInfo(a).isFile())
            disks << QFileInfo(a).absoluteFilePath(), disksSet = true;  /* opened (the desktop entry's %F) */
        else
            passThrough << a;
    }
    if(data.isEmpty())
        data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    data = QFileInfo(data).absoluteFilePath();
    QDir().mkpath(data + "/logs");

    QString logFile = data + "/logs/executor-" + QDate::currentDate().toString("yyyy-MM-dd") + ".log";
    LogCapture::instance().start(logFile);
    fprintf(stderr, "[desktop] Executor %s on %s (%s), started %s\n", EXECUTOR_VERSION,
            QSysInfo::prettyProductName().toLocal8Bit().constData(),
            QSysInfo::currentCpuArchitecture().toLocal8Bit().constData(),
            QDateTime::currentDateTime().toString(Qt::ISODate).toLocal8Bit().constData());
    fprintf(stderr, "[desktop] Executor, data folder %s\n", data.toLocal8Bit().constData());
#ifdef _WIN32
    windows_support::install_crash_handler(data + "/logs", logFile);
    fprintf(stderr, "[desktop] log %s; crash reports go beside it; %s\n",
            QDir::toNativeSeparators(logFile).toLocal8Bit().constData(),
            windows_support::describe().toLocal8Bit().constData());
#endif

    Settings settings(data);
    Disks diskLibrary(settings);
    if(!systemSet)
        system = settings.str("machine.system");
    if(!disksSet)
        disks = settings.list("disks.mounted");
    else
        for(const QString &d : disks)
            diskLibrary.addImage(d);
    if(!system.isEmpty() && !QFileInfo::exists(system))
    {
        fprintf(stderr, "[desktop] the System's image %s isn't there: Executor's own\n", system.toLocal8Bit().constData());
        system.clear();
    }

    /* the tab's arguments (worker.js), from the settings and the command line */
    std::vector<std::string> sargs = { "executor-desktop" };
    auto add = [&](const QString &a) { sargs.push_back(a.toStdString()); };
    add("--size");
    add(size.isEmpty() ? settings.str("machine.screen") : size);
    int sc = scale.isEmpty() ? settings.integer("display.scale") : (int)std::lround(scale.toDouble());
    if(sc == 0)
        sc = std::max(1, (int)std::ceil(app.devicePixelRatio() - 0.05)); /* 1.45: 2 */
    if(sc > 1)
        add("--scale"), add(QString::number(sc));
    /* --scale on the command line holds: Auto sharpness doesn't follow
       the window's display scale then (MainWindow::poll) */
    app.setProperty("executor.scale_fixed", !scale.isEmpty());
    QStringList mountedNow;
    for(const QString &d : disks)
    {
        if(!QFileInfo::exists(d))
            continue;
        add(Disks::isCD(d) ? "--cdrom" : "--disk");
        add(d);
        if(diskLibrary.additionsFor(d, system))
            add("--additions"), add(d);
        mountedNow << d;
    }
    if(!system.isEmpty())
        add("--system"), add(system);
    if(ppc || settings.boolean("machine.ppc"))
        add("--ppc");
    QString r = ram.isEmpty() ? QString::number(settings.integer("machine.ram_mb")) : ram;
    if(r.toInt() > 0)
        add("--ram"), add(r);
    QString dp = depth.isEmpty() ? QString::number(settings.integer("machine.depth")) : depth;
    if(dp.toInt() > 0)
        add("--depth"), add(dp);
    QString logtraps = settings.str("advanced.logtraps");
    if(!logtraps.isEmpty())
    {
        add("--logtraps");
        if(logtraps != "1")
            add("--logtraps-filter"), add(logtraps);
        add("--logtraps-nesting"), add(QString::number(settings.integer("advanced.logtraps_nesting")));
    }
    if(!settings.str("advanced.debug").isEmpty())
        add("--debug"), add(settings.str("advanced.debug"));
    for(const QString &spec : settings.str("advanced.ppc_break").split(';', Qt::SkipEmptyParts))
        add("--ppc-break"), add(spec);
    for(const QString &a : passThrough)
        add(a);
    std::vector<char *> sargv;
    for(auto &s : sargs)
        sargv.push_back(s.data());
    session::Options o = session::parse_args((int)sargv.size(), sargv.data());
    executor_host::Config &c = o.config;

    /* Disk images are writable, their changes in overlays (the opener:
       hfs/image_io.h, host/overlay.h), the images never written; the
       System's image mounted too, locked, unless it's in the Mac as a
       disk of its own; CDs locked, no overlay. */
    QDir().mkpath(data + "/overlays");
    Executor::image_io::set_opener([data](const std::string &path, bool writable)
                                       -> std::unique_ptr<Executor::image_io::File> {
        QString p = QString::fromStdString(path);
        QString overlay = Disks::overlayPath(data, p);
        if(overlay.isEmpty() || !QFileInfo(p).isFile())
            return nullptr;
        std::string err;
        auto o = executor_host::Overlay::open(path, overlay.toStdString(), writable, &err);
        if(!o)
            fprintf(stderr, "[desktop] %s: %s\n", path.c_str(), err.c_str());
        else
            fprintf(stderr, "[desktop] %s%s: changes in %s (version %u, %zu blocks)\n", path.c_str(),
                    writable ? "" : " (locked)", overlay.toLocal8Bit().constData(), o->version(), o->blocks());
        return o;
    });
    c.writable_images = true;
    Executor::ROMlib_ejectable_images = true;
    if(!system.isEmpty() && !mountedNow.contains(system))
        c.locked_disks.push_back(system.toStdString());
    c.unimplemented_log = (data + "/unimplemented.log").toStdString();
    c.open_transport = true;
    if(settings.boolean("sound.enabled") && audio::open())
        c.on_audio = audio::push;
    else
        c.on_audio = [](const uint8_t *, uint32_t) { return true; };  /* the Mac has its sound hardware */

    session::Setup setup;
    setup.data_dir = volume_for(data, system).toStdString();
    setup.images = "";  /* the images are where the user keeps them: every drive's watched */
    setup.log_prefix = "[desktop]";
    setup.ram_mb = 32;
    setup.apple_ram_mb = 128;
    QDir().mkpath(QString::fromStdString(setup.data_dir));

    /* the mounted list as it is now (the command line's, for this run) */
    if(!disksSet)
        settings.set("disks.mounted", QJsonArray::fromStringList(mountedNow));
    MainWindow window(settings, diskLibrary, automation, args);
    window.setMounted(mountedNow);
    window.show();
    fprintf(stderr, "[desktop] Executor %dx%d, System %s, data %s\n", c.width, c.height,
            system.isEmpty() ? "Executor's own" : system.toLocal8Bit().constData(), setup.data_dir.c_str());
    Emulator::instance().start(o, setup);
#ifdef _WIN32
    if(crashTest)
        QTimer::singleShot(2000, [crashTestHeap] { windows_support::crash_now(crashTestHeap); });
#else
    (void)crashTest, (void)crashTestHeap;
#endif
    int rc = app.exec();
    window.quit(rc);
    return rc;
}
