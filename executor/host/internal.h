#pragma once
/* host/'s files among themselves (session.h is the interface). */
#include "session.h"

#include <base/common.h>

#include <string>

namespace executor_host::session {

const Setup &setup();
const Hooks &hooks();

/* images.cpp: disk images in and out while Executor runs */
Executor::OSErr mount_image(const std::string &path);
Executor::OSErr eject_image(const std::string &path);
void watch_images();

/* session.cpp: the image's System Folder additions into Executor's System
   (LIVE: put in while Executor runs) */
void add_to_system(const std::string &path, bool live);

/* requests.cpp: the strings drawn lately, for "drawn" */
void text_drawn(const char *text, int len);

std::string pstring(const unsigned char *p);

}  // namespace executor_host::session
