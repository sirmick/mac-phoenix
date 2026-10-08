/*
 * Executor systems: the System Folders the Executor backend can run on.
 *
 * The user picks a disk image from <storage>/images as the System; its
 * System Folder is extracted on first use into a cache named after the
 * image (below) and the image itself is mounted read-only beside it, so
 * the applications on it are there too.  A name that isn't an image picks
 * a cache directly (one prepared by hand).
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
    std::string name;          // the image's file name, for an extracted one
    bool has_finder = false;   // <name>.clean/System Folder/Finder exists
};

std::string root(const std::string& storage_dir);
std::vector<System> list(const std::string& storage_dir);

// The data folder and start application for a run, from the config:
// --executor-data / --executor-app win; otherwise executor_system (copying
// <name>.clean to <name> as needed) and executor_start ("finder", "browser" or a
// path inside the System's folder). Empty strings mean Executor's defaults.
void resolve(const config::EmulatorConfig& config, std::string& data_dir, std::string& app);

// The image to mount beside the System (empty if the System is not one).
std::string system_image(const config::EmulatorConfig& config);

// Build <name>.clean from a disk image with tools/macdecode/sideload_system.py
// (replacing an existing one when refresh is set).
bool create_from_image(const std::string& storage_dir, const std::string& name,
                       const std::string& image_path, std::string& err,
                       bool refresh = false);

bool valid_name(const std::string& name);

}  // namespace executor_systems
