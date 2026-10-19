/*
 * png_write - RGB24 to PNG with zlib.
 *
 * Mac screens are mostly 1-bit dither patterns and flat UI; deflate's
 * long-distance matches take a 1024x768 desktop to ~20 KB at level 1 in a
 * few ms, where fpng (no long matches) made 2.4 MB of it.
 */

#ifndef PNG_WRITE_H
#define PNG_WRITE_H

#include <cstdint>
#include <vector>

// rgb: width*height*3 bytes, rows packed. Replaces out's contents.
bool png_encode_rgb(const uint8_t* rgb, int width, int height, std::vector<uint8_t>& out);

#endif // PNG_WRITE_H
