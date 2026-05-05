/*
 * emulator_subprocess.cpp - Emulator subprocess management for webserver mode
 *
 * Parent execs `mac-phoenix --ipc` as a child subprocess, connects
 * via SHM + Unix socket. Video frames are read directly from IPC SHM
 * by the encoder thread (zero-copy — no relay thread needed).
 */

#include "emulator_subprocess.h"
#include "../ipc/ipc_protocol.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace {
// Locate a repo-relative asset (dev tree first, then /usr/share install).
// Returns "" if the asset isn't present in either location.
std::string resolve_asset(const char* rel) {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) return "";
    exe_path[len] = '\0';
    char* last_slash = strrchr(exe_path, '/');
    if (!last_slash) return "";
    *last_slash = '\0';
    QFileInfo dev{QString::fromLocal8Bit(exe_path) + "/../" + rel};
    if (dev.exists()) return dev.filePath().toStdString();
    QFileInfo installed{QString("/usr/share/mac-phoenix/") + rel};
    if (installed.exists()) return installed.filePath().toStdString();
    return "";
}
}  // namespace

EmulatorSubprocess::EmulatorSubprocess(config::EmulatorConfig* config)
    : config_(config)
{
}

EmulatorSubprocess::~EmulatorSubprocess()
{
    stop();
}

std::vector<std::string> EmulatorSubprocess::build_child_args()
{
    // Get path to our own binary
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) {
        fprintf(stderr, "[EmulatorSubprocess] Failed to read /proc/self/exe\n");
        return {};
    }
    exe_path[len] = '\0';

    std::vector<std::string> args;
    args.push_back(exe_path);
    args.push_back("--config");
    args.push_back("/dev/null");
    args.push_back("--ipc");
    args.push_back("--no-webserver");

    // Pass CPU backend (encodes architecture)
    args.push_back("--backend");
    args.push_back(config_->backend_string());

    if (!config_->rom_path.empty()) {
        args.push_back("--rom");
        args.push_back(config_->rom_path);
    }

    for (const auto& d : config_->disk_paths) {
        args.push_back("--disk");
        args.push_back(d);
    }
    for (const auto& c : config_->cdrom_paths) {
        args.push_back("--cdrom");
        args.push_back(c);
    }
    for (const auto& e : config_->extfs_paths) {
        args.push_back("--extfs");
        args.push_back(e);
    }

    args.push_back("--ram");
    args.push_back(std::to_string(config_->ram_mb));

    // Forward Boot From selection (disk reorder already lives in disk_paths;
    // bootdriver carries the -62 CD override). Only forward if non-default.
    if (config_->bootdriver != 0) {
        args.push_back("--bootdriver");
        args.push_back(std::to_string(config_->bootdriver));
    }

    args.push_back("--screen");
    args.push_back(config_->screen_string());

    if (config_->max_screen_width && config_->max_screen_height) {
        args.push_back("--max-resolution");
        args.push_back(std::to_string(config_->max_screen_width) + "x" +
                       std::to_string(config_->max_screen_height));
    }

    args.push_back("--log-level");
    args.push_back(std::to_string(config_->log_level));

    if (config_->dismiss_shutdown_dialog) {
        args.push_back("--dismiss-shutdown-dialog");
    }

    if (config_->audio_enabled) {
        args.push_back("--audio");
    }

    if (config_->bridge_enabled) {
        args.push_back("--bridge");
    }

    if (config_->browser_enabled) {
        args.push_back("--browser");
        // Auto-mount the MacBrowser floppy so the guest has the .app
        // available without the user editing disk_paths by hand. Skip
        // if it's already in the list (or if we couldn't find the asset).
        std::string dsk = resolve_asset("MacBrowser/MacBrowser.dsk");
        if (dsk.empty()) {
            // Installed layout drops the floppy directly under /usr/share.
            dsk = resolve_asset("MacBrowser.dsk");
        }
        if (!dsk.empty()) {
            bool already = false;
            for (const auto& d : config_->disk_paths) {
                if (d == dsk || (d.size() >= 16 &&
                    d.substr(d.size() - 16) == "/MacBrowser.dsk")) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                args.push_back("--disk");
                args.push_back(dsk);
                fprintf(stderr, "[EmulatorSubprocess] auto-mounted %s\n",
                        dsk.c_str());
            }
        } else {
            fprintf(stderr, "[EmulatorSubprocess] MacBrowser.dsk not found "
                    "— browser pipeline will run but the guest app won't "
                    "be available\n");
        }
    }

    // Network
    if (config_->network != config::NetworkMode::None) {
        args.push_back("--network");
        std::string net_arg = config_->network_string();
        if (!config_->network_if.empty()) {
            net_arg += ":" + config_->network_if;
        }
        args.push_back(net_arg);
    }

    // Serial — child loads from /dev/null so JSON is unavailable; relay
    // these explicitly via CLI. Only emit when set (empty = port disabled,
    // matching today's "omit from JSON" pattern).
    if (!config_->serial_a.empty()) {
        args.push_back("--serial-a");
        args.push_back(config_->serial_a);
    }
    if (!config_->serial_b.empty()) {
        args.push_back("--serial-b");
        args.push_back(config_->serial_b);
    }

    // CPU feature flags
    args.push_back(config_->jit ? "--jit" : "--no-jit");
    args.push_back(config_->jit68k ? "--jit68k" : "--no-jit68k");
    args.push_back(config_->idlewait ? "--idlewait" : "--no-idlewait");

    return args;
}

bool EmulatorSubprocess::start()
{
    if (child_process_ && child_process_->state() != QProcess::NotRunning) {
        fprintf(stderr, "[EmulatorSubprocess] Already running (pid %d)\n", child_pid_);
        return false;
    }

    // Clear stale SHM/eventfd from any previous session so the encoder
    // thread sees nullptr and resets its reconnect state (ipc_was_connected,
    // ipc_last_frame_count) before we publish the new child's SHM.
    clear_ipc_shm();

    auto args = build_child_args();
    if (args.empty()) {
        return false;
    }

    fprintf(stderr, "[EmulatorSubprocess] Launching:");
    for (const auto& a : args) fprintf(stderr, " %s", a.c_str());
    fprintf(stderr, "\n");

    // Build QProcess. ForwardedChannels makes the child's stdout/stderr
    // appear in the parent's terminal — same behavior as the original
    // fork+execv which inherited file descriptors.
    child_process_ = std::make_unique<QProcess>();
    child_process_->setProcessChannelMode(QProcess::ForwardedChannels);

    const QString program = QString::fromStdString(args.front());
    QStringList qargs;
    for (size_t i = 1; i < args.size(); i++) {
        qargs << QString::fromStdString(args[i]);
    }

    child_process_->start(program, qargs);
    if (!child_process_->waitForStarted(5000)) {
        fprintf(stderr, "[EmulatorSubprocess] QProcess failed to start: %s\n",
                child_process_->errorString().toUtf8().constData());
        child_process_.reset();
        return false;
    }

    child_pid_ = static_cast<pid_t>(child_process_->processId());
    fprintf(stderr, "[EmulatorSubprocess] Child started (pid %d)\n", child_pid_);

    // Poll for SHM to appear (child creates it during init).
    bool connected = false;
    for (int attempt = 0; attempt < 400; attempt++) {  // 10 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(25));

        if (child_process_->state() == QProcess::NotRunning) {
            fprintf(stderr, "[EmulatorSubprocess] Child exited before connection (code %d)\n",
                    child_process_->exitCode());
            child_process_.reset();
            child_pid_ = -1;
            return false;
        }

        if (ipc_client_.connect(child_pid_)) {
            connected = true;
            break;
        }
    }

    if (!connected) {
        fprintf(stderr, "[EmulatorSubprocess] Failed to connect to child after 10s\n");
        child_process_->kill();
        child_process_->waitForFinished(2000);
        child_process_.reset();
        child_pid_ = -1;
        return false;
    }

    fprintf(stderr, "[EmulatorSubprocess] Connected to child IPC\n");

    // No monitor thread: QProcess isn't thread-safe, and the IPC heartbeat
    // watchdog (command_bridge.cpp) plus the lazy reap_if_dead() in
    // is_running()/stop() catch async death within one heartbeat tick. The
    // encoder also has a fallback path for stale ipc_shm pointers.

    // Publish IPC SHM to encoder thread (zero-copy video)
    publish_ipc_shm();

    return true;
}

bool EmulatorSubprocess::stop()
{
    if (!child_process_ || child_process_->state() == QProcess::NotRunning) {
        child_process_.reset();
        child_pid_ = -1;
        return true;
    }

    fprintf(stderr, "[EmulatorSubprocess] Stopping child (pid %d)\n", child_pid_);

    // Clear the atomic first so readers that see it go straight to the
    // nullptr fallback path without racing for the lock. Existing readers
    // holding the shared lock will finish their in-flight dereference;
    // disconnect() below takes the exclusive lock, waits for them to
    // drain, then munmaps. After this, no thread can observe an unmapped
    // page.
    clear_ipc_shm();
    ipc_client_.disconnect();

    // QProcess::terminate() sends SIGTERM on Unix, posts WM_CLOSE on Windows.
    child_process_->terminate();
    if (child_process_->waitForFinished(1000)) {
        child_process_.reset();
        child_pid_ = -1;
        fprintf(stderr, "[EmulatorSubprocess] Child stopped gracefully\n");
        return true;
    }

    // Force kill — QProcess::kill() sends SIGKILL on Unix, TerminateProcess
    // on Windows.
    child_process_->kill();
    child_process_->waitForFinished(2000);
    child_process_.reset();
    child_pid_ = -1;

    fprintf(stderr, "[EmulatorSubprocess] Child killed\n");
    return true;
}

bool EmulatorSubprocess::reset()
{
    stop();
    return start();
}

void EmulatorSubprocess::reap_if_dead()
{
    if (child_process_ && child_process_->state() == QProcess::NotRunning &&
        child_pid_ > 0) {
        const QProcess::ExitStatus es = child_process_->exitStatus();
        const int code = child_process_->exitCode();
        if (es == QProcess::CrashExit) {
            fprintf(stderr, "[EmulatorSubprocess] Child crashed (signal/code %d)\n", code);
        } else if (code != 0) {
            fprintf(stderr, "[EmulatorSubprocess] Child exited with code %d\n", code);
        }
        clear_ipc_shm();
        child_pid_ = -1;
    }
}

bool EmulatorSubprocess::is_running() const
{
    // Lazy death check — see header for why we don't have a monitor thread.
    const_cast<EmulatorSubprocess*>(this)->reap_if_dead();
    if (child_pid_ <= 0) return false;
    if (!ipc_client_.is_connected()) return false;
    const IPCBuffer* buf = ipc_client_.shm();
    return buf && buf->state == IPC_STATE_RUNNING;
}

void EmulatorSubprocess::set_ipc_shm_atoms(std::atomic<IPCBuffer*>* shm, std::atomic<int>* eventfd)
{
    ipc_shm_atom_ = shm;
    ipc_eventfd_atom_ = eventfd;
}

void EmulatorSubprocess::publish_ipc_shm()
{
    if (ipc_shm_atom_) {
        ipc_shm_atom_->store(ipc_client_.shm(), std::memory_order_release);
    }
    if (ipc_eventfd_atom_) {
        // Phase 3b: the field name is preserved for binary compat with
        // existing call sites (g_ipc_eventfd) but now carries the
        // notify-socket fd from QLocalSocket::socketDescriptor().
        ipc_eventfd_atom_->store(ipc_client_.frame_notify_fd(),
                                 std::memory_order_release);
    }
    fprintf(stderr, "[EmulatorSubprocess] Published IPC SHM to encoder (shm=%p, notify_fd=%d)\n",
            (void*)ipc_client_.shm(), ipc_client_.frame_notify_fd());
}

void EmulatorSubprocess::clear_ipc_shm()
{
    if (ipc_shm_atom_) {
        ipc_shm_atom_->store(nullptr, std::memory_order_release);
    }
    if (ipc_eventfd_atom_) {
        ipc_eventfd_atom_->store(-1, std::memory_order_release);
    }
}
