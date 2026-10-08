/*
 * Executor systems: the System Folders the Executor backend can run on.
 *
 * Each is two folders in <storage>/executor-systems/:
 *   <name>.clean/  the System Folder as prepared (sideload_system.py from a
 *                  disk image, or copied in by hand); never written by a run
 *   <name>/        what Executor runs on (the volume is named after it);
 *                  refreshed from <name>.clean before a run when "start
 *                  fresh" is on (a crashed run can leave half-written
 *                  preferences behind), kept otherwise.  File times are
 *                  kept: they are the files' Mac dates, and Finder checks
 *                  its own against the segment cache in its preferences.
 *
 * Without a system, Executor uses its own data folder (<storage>/executor)
 * and starts in its Browser.
 */
#pragma once

#include <string>
#include <vector>

namespace config { struct EmulatorConfig; }

namespace executor_systems {

struct System {
    std::string name;
    bool has_finder = false;   // <name>.clean/System Folder/Finder exists
};

std::string root(const std::string& storage_dir);
std::vector<System> list(const std::string& storage_dir);

// The data folder and start application for a run, from the config:
// --executor-data / --executor-app win; otherwise executor_system (copying
// <name>.clean to <name> as needed) and executor_start ("finder", "browser" or a
// path inside the System's folder). Empty strings mean Executor's defaults.
void resolve(const config::EmulatorConfig& config, std::string& data_dir, std::string& app);

// Build <name>.clean from a disk image with tools/macdecode/sideload_system.py.
bool create_from_image(const std::string& storage_dir, const std::string& name,
                       const std::string& image_path, std::string& err);

bool valid_name(const std::string& name);

}  // namespace executor_systems
