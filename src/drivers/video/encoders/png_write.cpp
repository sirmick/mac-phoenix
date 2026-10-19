#include "png_write.h"

#include <cstring>
#include <zlib.h>

namespace {

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(v >> 24);
    out.push_back(v >> 16);
    out.push_back(v >> 8);
    out.push_back(v);
}

// Length, type and data are already in out from `start`; append the CRC.
void end_chunk(std::vector<uint8_t>& out, size_t start) {
    uint32_t len = static_cast<uint32_t>(out.size() - start - 8);
    for (int i = 0; i < 4; i++) out[start + i] = len >> (24 - 8 * i);
    uLong crc = crc32(0, out.data() + start + 4, static_cast<uInt>(out.size() - start - 4));
    put32(out, static_cast<uint32_t>(crc));
}

size_t begin_chunk(std::vector<uint8_t>& out, const char type[4]) {
    size_t start = out.size();
    put32(out, 0);  // length, filled in by end_chunk
    out.insert(out.end(), type, type + 4);
    return start;
}

}  // namespace

bool png_encode_rgb(const uint8_t* rgb, int width, int height, std::vector<uint8_t>& out) {
    if (width <= 0 || height <= 0) return false;
    const size_t row = static_cast<size_t>(width) * 3;

    out.clear();
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    out.insert(out.end(), sig, sig + 8);

    size_t c = begin_chunk(out, "IHDR");
    put32(out, width);
    put32(out, height);
    out.push_back(8);  // bit depth
    out.push_back(2);  // colour type: RGB
    out.push_back(0);  // compression
    out.push_back(0);  // filter method
    out.push_back(0);  // no interlace
    end_chunk(out, c);

    // Deflate the rows straight into the IDAT chunk, each behind filter
    // byte 0 (none: the patterns compress better unfiltered).
    z_stream zs{};
    if (deflateInit(&zs, 1) != Z_OK) return false;
    c = begin_chunk(out, "IDAT");
    size_t data_start = out.size();
    out.resize(data_start + deflateBound(&zs, (row + 1) * height));
    zs.next_out = out.data() + data_start;
    zs.avail_out = static_cast<uInt>(out.size() - data_start);

    static const uint8_t filter_none = 0;
    for (int y = 0; y < height; y++) {
        zs.next_in = const_cast<Bytef*>(&filter_none);
        zs.avail_in = 1;
        deflate(&zs, Z_NO_FLUSH);
        zs.next_in = const_cast<Bytef*>(rgb + y * row);
        zs.avail_in = static_cast<uInt>(row);
        deflate(&zs, Z_NO_FLUSH);
    }
    int rc = deflate(&zs, Z_FINISH);
    out.resize(data_start + zs.total_out);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) return false;
    end_chunk(out, c);

    end_chunk(out, begin_chunk(out, "IEND"));
    return true;
}
