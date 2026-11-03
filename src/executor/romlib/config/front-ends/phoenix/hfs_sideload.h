#pragma once
/*
 * hfs_sideload.h - Apple's System Folder from an HFS or HFS+ disk image,
 * for Executor to run on (docs/executor/DIRECTION.md, W3).
 *
 * What tools/macdecode/sideload_system.py does with hfsutils, in C++ so
 * the browser build can do it: a read-only HFS reader (a bare volume, or
 * the first HFS partition of an Apple partition map; HFS+ volumes, bare,
 * wrapped or partitioned, through hfs/plus: docs/executor/HFSPlus.md), and a copy
 * of the System Folder's System, Finder and every file in Preferences,
 * Fonts, Apple Menu Items, Extensions, Control Panels and Startup Items
 * into DATA_DIR/System Folder in the form Executor's local volume reads:
 * the data fork as the file, an AppleDouble "%NAME" with the Finder info
 * and the resource fork, the file's modification date kept (Finder keys
 * its cached segment table on its own date).
 */
#include <string>
#include <vector>

namespace executor_host {

// Copies the System Folder of the HFS image into data_dir/System Folder.
// Returns false, with why in *error, if the image has none it can read.
// *has_finder: a Finder was among what it copied.
bool sideload_system(const std::string& image, const std::string& data_dir,
                     bool *has_finder, std::string *error);

// What system_additions() did: files copied, files left as they were (the
// System's newer or the same), and what it copied that is in use only after
// a restart ("Fonts", "Extensions:NAME": code that runs at startup).
struct Additions
{
    int copied = 0, kept = 0;
    std::vector<std::string> restart;  // MacRoman
};

// Copies the image's "System Folder Additions" (what its applications'
// installers put in the System Folder: tools/web/make_disk.py
// --system-from) into data_dir/System Folder: each file it hasn't, or has
// older than the image's; the folders' names matched as HFS does, without
// case. False, with why in *error, if the image can't be read; true with
// nothing done if it has none.
bool system_additions(const std::string& image, const std::string& data_dir,
                      Additions *done, std::string *error);

}  // namespace executor_host
