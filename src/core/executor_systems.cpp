#include "executor_systems.h"
#include "emulator_config.h"

#include <QProcess>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace executor_systems {

std::string root(const std::string& storage_dir)
{
    return storage_dir + "/executor-systems";
}

bool valid_name(const std::string& name)
{
    return !name.empty() && name.size() <= 64 && name[0] != '.'
        && name.find('/') == std::string::npos && name.find("..") == std::string::npos;
}

std::vector<System> list(const std::string& storage_dir)
{
    std::vector<System> out;
    std::error_code ec;
    const std::string suffix = ".clean";
    for (const auto& e : fs::directory_iterator(root(storage_dir), ec)) {
        std::string dir = e.path().filename().string();
        if (!e.is_directory(ec) || dir.size() <= suffix.size()
            || dir.compare(dir.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        fs::path clean = e.path();
        if (!fs::is_directory(clean / "System Folder", ec))
            continue;
        System s;
        s.name = dir.substr(0, dir.size() - suffix.size());
        s.has_finder = fs::exists(clean / "System Folder" / "Finder", ec);
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const System& a, const System& b) { return a.name < b.name; });
    return out;
}

// A recursive copy that keeps modification times (the files' Mac dates).
static bool copy_tree(const fs::path& from, const fs::path& to, std::error_code& ec)
{
    fs::create_directory(to, ec);
    if (ec)
        return false;
    for (const auto& e : fs::directory_iterator(from, ec)) {
        fs::path dst = to / e.path().filename();
        if (e.is_symlink(ec))
            fs::copy_symlink(e.path(), dst, ec);
        else if (e.is_directory(ec))
            copy_tree(e.path(), dst, ec);
        else {
            fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                fs::last_write_time(dst, fs::last_write_time(e.path(), ec), ec);
        }
        if (ec)
            return false;
    }
    fs::last_write_time(to, fs::last_write_time(from, ec), ec);
    return !ec;
}

// <name>.clean -> <name> (from scratch when fresh, or when it is missing).
static bool prepare(const fs::path& work, bool fresh, std::string& err)
{
    std::error_code ec;
    fs::path clean = work.string() + ".clean";
    if (!fs::is_directory(clean, ec)) {
        err = clean.string() + " missing";
        return false;
    }
    if (!fresh && fs::is_directory(work, ec))
        return true;
    fs::remove_all(work, ec);
    if (!copy_tree(clean, work, ec)) {
        err = "copying " + clean.string() + ": " + ec.message();
        return false;
    }
    return true;
}

std::string system_image(const config::EmulatorConfig& config)
{
    if (config.executor_system.empty() || !valid_name(config.executor_system))
        return "";
    std::error_code ec;
    fs::path img = fs::path(config.storage_dir) / "images" / config.executor_system;
    return fs::is_regular_file(img, ec) ? img.string() : "";
}

void resolve(const config::EmulatorConfig& config, std::string& data_dir, std::string& app)
{
    data_dir = config.executor_data_dir;
    app = config.executor_app;

    if (data_dir.empty() && !config.executor_system.empty()) {
        // An image picked as the System: extract its System Folder once.
        std::string image = system_image(config);
        std::error_code ec;
        if (!image.empty()
            && !fs::is_directory(fs::path(root(config.storage_dir)) / (config.executor_system + ".clean"), ec)) {
            std::string err;
            fprintf(stderr, "[Executor] extracting the System Folder from %s\n", image.c_str());
            if (!create_from_image(config.storage_dir, config.executor_system, image, err))
                fprintf(stderr, "[Executor] %s\n", err.c_str());
        }
        if (!valid_name(config.executor_system)) {
            fprintf(stderr, "[Executor] bad system name '%s'\n", config.executor_system.c_str());
        } else {
            fs::path work = fs::path(root(config.storage_dir)) / config.executor_system;
            std::string err;
            if (prepare(work, config.executor_fresh, err))
                data_dir = work.string();
            else
                fprintf(stderr, "[Executor] system '%s': %s\n", config.executor_system.c_str(), err.c_str());
        }
    }

    if (!app.empty() || data_dir.empty())
        return;
    const std::string& start = config.executor_start;
    std::error_code ec;
    if (start.empty() || start == "finder") {
        fs::path finder = fs::path(data_dir) / "System Folder" / "Finder";
        if (fs::exists(finder, ec))
            app = finder.string();
    } else if (start != "browser") {
        fs::path p = start[0] == '/' ? fs::path(start) : fs::path(data_dir) / start;
        if (fs::exists(p, ec))
            app = p.string();
        else
            fprintf(stderr, "[Executor] start application '%s' not found\n", p.c_str());
    }
}

bool create_from_image(const std::string& storage_dir, const std::string& name,
                       const std::string& image_path, std::string& err, bool refresh)
{
    if (!valid_name(name)) {
        err = "invalid name";
        return false;
    }
    std::error_code ec;
    if (!fs::exists(image_path, ec)) {
        err = image_path + " not found";
        return false;
    }
    fs::path clean = fs::path(root(storage_dir)) / (name + ".clean");
    if (refresh)
        fs::remove_all(clean, ec);
    if (fs::exists(clean, ec)) {
        err = name + " already exists";
        return false;
    }

    // The tool sits next to the build directory: <repo>/build/mac-phoenix.
    char exe[4096];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len <= 0) {
        err = "cannot find the executable";
        return false;
    }
    exe[len] = 0;
    fs::path tool = fs::path(exe).parent_path().parent_path() / "tools/macdecode/sideload_system.py";
    if (!fs::exists(tool, ec)) {
        err = tool.string() + " not found";
        return false;
    }

    fs::create_directories(clean, ec);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("python3", QStringList{QString::fromStdString(tool.string()),
                                      QString::fromStdString(image_path),
                                      QString::fromStdString(clean.string())});
    if (!proc.waitForStarted(5000) || !proc.waitForFinished(300000)
        || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        err = "sideload_system.py failed: " + proc.readAll().right(400).toStdString();
        fs::remove_all(clean, ec);
        return false;
    }
    return true;
}

}  // namespace executor_systems
