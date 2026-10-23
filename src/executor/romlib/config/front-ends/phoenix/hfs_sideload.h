#pragma once
/*
 * hfs_sideload.h - Apple's System Folder from an HFS disk image, for
 * Executor to run on (docs/executor/DIRECTION.md, W3).
 *
 * What tools/macdecode/sideload_system.py does with hfsutils, in C++ so
 * the browser build can do it: a read-only HFS reader (a bare volume, or
 * the first HFS partition of an Apple partition map; not HFS+), and a copy
 * of the System Folder's System, Finder and every file in Preferences,
 * Fonts, Apple Menu Items, Extensions, Control Panels and Startup Items
 * into DATA_DIR/System Folder in the form Executor's local volume reads:
 * the data fork as the file, an AppleDouble "%NAME" with the Finder info
 * and the resource fork, the file's modification date kept (Finder keys
 * its cached segment table on its own date).
 */
#include <string>

namespace executor_host {

// Copies the System Folder of the HFS image into data_dir/System Folder.
// Returns false, with why in *error, if the image has none it can read.
// *has_finder: a Finder was among what it copied.
bool sideload_system(const std::string& image, const std::string& data_dir,
                     bool *has_finder, std::string *error);

}  // namespace executor_host
