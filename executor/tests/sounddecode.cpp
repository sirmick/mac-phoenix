/* MacPhoenix: the Movie Toolbox's sound formats to the mixer's 8-bit mono
   (romlib/sound/sounddecode.cpp; docs/executor/QuickTime.md, section 12).
   Values worked out by hand from the formats' descriptions: IMA ADPCM's
   step and index tables, ITU-T G.711, QuickTime's sample descriptions.
   QTTest's tones (tests/guest/qt, ctest qt_executor_761) check the sound
   played. */
#include "gtest/gtest.h"

#include <base/common.h>
#include <sound/sounddecode.h>

#include <vector>

using namespace Executor;

namespace
{
const uint32_t kRaw = "raw "_4, kTwos = "twos"_4, kSowt = "sowt"_4, kUlaw = "ulaw"_4,
               kAlaw = "alaw"_4, kIma4 = "ima4"_4, kMac3 = "MAC3"_4;

std::vector<uint8_t> decode(uint32_t format, int channels, int bits, std::vector<uint8_t> in,
                            uint32_t skip, uint32_t frames, bool *ok = nullptr)
{
    std::vector<uint8_t> out(frames, 0x55);
    bool r = ROMlib_decode_sound(format, channels, bits, in.data(), in.size(), skip, frames, out.data());
    if(ok)
        *ok = r;
    return out;
}
}

TEST(SoundDecode, Packets)
{
    EXPECT_EQ(64u, ROMlib_sound_frames_per_packet(kIma4));
    EXPECT_EQ(68u, ROMlib_sound_bytes_per_packet(kIma4, 2, 16));
    EXPECT_EQ(6u, ROMlib_sound_frames_per_packet(kMac3));
    EXPECT_EQ(1u, ROMlib_sound_frames_per_packet(kTwos));
    EXPECT_EQ(4u, ROMlib_sound_bytes_per_packet(kTwos, 2, 16));
    EXPECT_EQ(1u, ROMlib_sound_bytes_per_packet(kUlaw, 1, 16));
}

TEST(SoundDecode, Uncompressed)
{
    EXPECT_EQ((std::vector<uint8_t>{ 0x80, 0xFF, 0x00 }), decode(kRaw, 1, 8, { 0x80, 0xFF, 0x00 }, 0, 3));
    EXPECT_EQ((std::vector<uint8_t>{ 0x80, 0xFF, 0x00 }), decode(kTwos, 1, 8, { 0x00, 0x7F, 0x80 }, 0, 3));
    // 16 bits: the high byte, rounded; big-endian and little-endian
    EXPECT_EQ((std::vector<uint8_t>{ 192, 64 }), decode(kTwos, 1, 16, { 0x40, 0x00, 0xC0, 0x00 }, 0, 2));
    EXPECT_EQ((std::vector<uint8_t>{ 192 }), decode(kSowt, 1, 16, { 0x00, 0x40 }, 0, 1));
    // stereo: the two channels' mean
    EXPECT_EQ((std::vector<uint8_t>{ 160 }), decode(kTwos, 2, 16, { 0x40, 0x00, 0x00, 0x00 }, 0, 1));
    // a frame in
    EXPECT_EQ((std::vector<uint8_t>{ 0x00 }), decode(kRaw, 1, 8, { 0x80, 0x00 }, 1, 1));
}

TEST(SoundDecode, G711)
{
    // mu-law 0xFF is 0, 0x00 -32124; A-law 0xD5 is +8
    EXPECT_EQ((std::vector<uint8_t>{ 128, 3 }), decode(kUlaw, 1, 16, { 0xFF, 0x00 }, 0, 2));
    EXPECT_EQ((std::vector<uint8_t>{ 128 }), decode(kAlaw, 1, 16, { 0xD5 }, 0, 1));
}

TEST(SoundDecode, Ima4)
{
    // predictor $4000, index 88 (step 32767); code 7 adds 61436 (clipped
    // to 32767, index 88 still), code 8 subtracts 4095 (32767 >> 3)
    std::vector<uint8_t> packet(34, 0);
    packet[0] = 0x40;
    packet[1] = 88;
    packet[2] = 0x87;   // low nibble first
    std::vector<uint8_t> out = decode(kIma4, 1, 16, packet, 0, 2);
    EXPECT_EQ((std::vector<uint8_t>{ 255, 240 }), out);
    EXPECT_EQ((std::vector<uint8_t>{ 240 }), decode(kIma4, 1, 16, packet, 1, 1));
    // too few bytes: silence, false
    bool ok = true;
    EXPECT_EQ((std::vector<uint8_t>{ 128, 128 }), decode(kIma4, 1, 16, { 0x40, 88 }, 0, 2, &ok));
    EXPECT_FALSE(ok);
}

TEST(SoundDecode, MaceNotDecoded)
{
    bool ok = true;
    EXPECT_EQ((std::vector<uint8_t>{ 128, 128 }), decode(kMac3, 1, 8, { 1, 2, 3, 4 }, 0, 2, &ok));
    EXPECT_FALSE(ok);
    EXPECT_FALSE(ROMlib_sound_format_known(kMac3, 8));
}
