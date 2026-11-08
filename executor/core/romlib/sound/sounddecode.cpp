/* MacPhoenix: see sounddecode.h. */
#include <base/common.h>
#include <sound/sounddecode.h>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace Executor;

namespace
{
constexpr uint32_t fourcc(const char (&s)[5])
{
    return (uint32_t)(uint8_t)s[0] << 24 | (uint32_t)(uint8_t)s[1] << 16
        | (uint32_t)(uint8_t)s[2] << 8 | (uint8_t)s[3];
}

const uint32_t kRaw = fourcc("raw "), kTwos = fourcc("twos"), kSowt = fourcc("sowt"),
               kNone = fourcc("NONE"), kUlaw = fourcc("ulaw"), kAlaw = fourcc("alaw"),
               kIma4 = fourcc("ima4"), kMac3 = fourcc("MAC3"), kMac6 = fourcc("MAC6");

/* IMA ADPCM's step sizes and index changes (the IMA's Recommended
   Practices, 1992) */
const int16_t ima_steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
const int8_t ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/* one ima4 packet (34 bytes) to 64 samples */
void ima4_packet(const uint8_t *p, int16_t *out)
{
    uint16_t header = (uint16_t)(p[0] << 8 | p[1]);
    int predictor = (int16_t)(header & 0xFF80);
    int index = std::min(header & 0x7F, 88);
    for(int i = 0; i < 64; i++)
    {
        int code = (p[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
        int step = ima_steps[index];
        int diff = step >> 3;
        if(code & 1)
            diff += step >> 2;
        if(code & 2)
            diff += step >> 1;
        if(code & 4)
            diff += step;
        predictor += (code & 8) ? -diff : diff;
        predictor = std::max(-32768, std::min(32767, predictor));
        index = std::max(0, std::min(88, index + ima_index[code & 7]));
        out[i] = (int16_t)predictor;
    }
}

/* ITU-T G.711 */
int16_t ulaw(uint8_t u)
{
    u = ~u;
    int t = (((u & 0x0F) << 3) + 0x84) << ((u & 0x70) >> 4);
    return (int16_t)((u & 0x80) ? 0x84 - t : t - 0x84);
}

int16_t alaw(uint8_t a)
{
    a ^= 0x55;
    int seg = (a & 0x70) >> 4;
    int t = (a & 0x0F) << 4;
    t = seg == 0 ? t + 8 : (t + 0x108) << (seg - 1);
    return (int16_t)((a & 0x80) ? t : -t);
}

uint8_t to8(int v)
{
    return (uint8_t)(std::max(-128, std::min(127, (v + 0x80) >> 8)) + 128);
}

/* one sample of an uncompressed format, as 16 bits */
int16_t pcm(uint32_t format, int bits, const uint8_t *p)
{
    if(format == kUlaw)                                  /* bits: what it decodes to */
        return ulaw(p[0]);
    if(format == kAlaw)
        return alaw(p[0]);
    if(bits <= 8)
    {
        if(format == kTwos)
            return (int16_t)((int8_t)p[0] * 256);
        return (int16_t)((p[0] - 128) * 256);           /* raw, NONE */
    }
    uint16_t v = format == kSowt ? (uint16_t)(p[1] << 8 | p[0]) : (uint16_t)(p[0] << 8 | p[1]);
    if(format == kRaw)
        v ^= 0x8000;
    return (int16_t)v;
}
}

uint32_t Executor::ROMlib_sound_frames_per_packet(uint32_t format)
{
    if(format == kIma4)
        return 64;
    if(format == kMac3 || format == kMac6)
        return 6;
    return 1;
}

uint32_t Executor::ROMlib_sound_bytes_per_packet(uint32_t format, int channels, int bits)
{
    channels = std::max(channels, 1);
    if(format == kIma4)
        return 34 * channels;
    if(format == kMac3)
        return 2 * channels;
    if(format == kMac6)
        return channels;
    if(format == kUlaw || format == kAlaw)
        return channels;
    return channels * std::max(bits / 8, 1);
}

bool Executor::ROMlib_sound_format_known(uint32_t format, int bits)
{
    if(format == kIma4 || format == kUlaw || format == kAlaw)
        return true;
    if(format == kRaw || format == kTwos || format == kSowt || format == kNone)
        return bits == 8 || bits == 16;
    return false;
}

bool Executor::ROMlib_decode_sound(uint32_t format, int channels, int bits,
                                   const uint8_t *in, size_t in_bytes,
                                   uint32_t skip, uint32_t frames, uint8_t *out)
{
    memset(out, 0x80, frames);
    channels = std::max(channels, 1);
    if(!ROMlib_sound_format_known(format, bits))
        return false;
    uint32_t fpp = ROMlib_sound_frames_per_packet(format);
    uint32_t bpp = ROMlib_sound_bytes_per_packet(format, channels, bits);
    uint32_t packets = (skip + frames + fpp - 1) / fpp;
    if(in_bytes < (size_t)packets * bpp)
        return false;
    if(format == kIma4)
    {
        std::vector<int16_t> pcm16(64 * channels);
        for(uint32_t k = 0; k < packets; k++)
        {
            for(int c = 0; c < channels; c++)
                ima4_packet(in + (size_t)k * bpp + c * 34, &pcm16[c * 64]);
            for(uint32_t i = 0; i < 64; i++)
            {
                int64_t f = (int64_t)k * 64 + i - skip;
                if(f < 0 || f >= frames)
                    continue;
                int sum = 0;
                for(int c = 0; c < channels; c++)
                    sum += pcm16[c * 64 + i];
                out[f] = to8(sum / channels);
            }
        }
        return true;
    }
    int bytes = (format == kUlaw || format == kAlaw) ? 1 : bits / 8;
    for(uint32_t f = 0; f < frames; f++)
    {
        const uint8_t *p = in + (size_t)(skip + f) * bpp;
        int sum = 0;
        for(int c = 0; c < channels; c++)
            sum += pcm(format, bits, p + c * bytes);
        out[f] = to8(sum / channels);
    }
    return true;
}
