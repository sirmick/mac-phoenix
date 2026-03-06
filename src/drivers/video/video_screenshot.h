/*
 *  video_screenshot.h - Screenshot video driver
 *
 *  Enable with MACEMU_SCREENSHOTS=1 environment variable.
 */

#ifndef VIDEO_SCREENSHOT_H
#define VIDEO_SCREENSHOT_H

#include <stdbool.h>

bool video_screenshot_init(bool classic);
void video_screenshot_exit(void);
void video_screenshot_refresh(void);

#endif /* VIDEO_SCREENSHOT_H */
