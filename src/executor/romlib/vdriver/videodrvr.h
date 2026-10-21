#pragma once

#include <vdriver/vdriver.h>

namespace Executor
{
/* The screen's video driver, ".Display" (videodrvr.cpp): installs it once
   and answers its refnum, for the main GDevice's gdRefNum. */
INTEGER ROMlib_install_video_driver();

/* The screen's colours, from the Color Manager: into the driver's table
   (the hardware's) and to the screen. */
void ROMlib_video_set_colors(int n, const vdriver_color_t *colors);
}
