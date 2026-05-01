/* video_modes.h — single source of truth for the resolutions the emulator
 * advertises to the guest Mac OS.
 *
 * Three init paths consume this table:
 *   src/main.cpp                       (m68k IPC subprocess mode)
 *   src/cpu/kpx/video_ppc.cpp          (PPC, all modes)
 *   src/drivers/video/video_webrtc.cpp (m68k non-IPC, legacy WebRTC mode)
 *
 * Each backend filters by its own flag bit. Mono machines (Mac SE) bypass
 * the table entirely and publish a single 512x342 1-bit mode from the
 * machine profile — there's no point pretending the SE could do anything
 * else.
 *
 * The `apple_id` field is the slot-ROM display ID that gets written into
 * sResource entries and read back by Monitors cdev. Values 0x80..0x8A are
 * SheepShaver's vintage range (kept verbatim for back-compat); 0x8B..0x95
 * are mac-phoenix extensions for modes Apple never shipped IDs for.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace mp::video {

struct ModeDef {
    std::uint16_t w;
    std::uint16_t h;
    std::uint16_t apple_id;     // viAppleID — see APPLE_* enum in compat/video.h
    std::uint16_t refresh_hz;   // for csRefreshRate
    std::uint8_t  flags;
};

inline constexpr std::uint8_t kFlagM68k = 1u << 0;  // m68k backends advertise
inline constexpr std::uint8_t kFlagPpc  = 1u << 1;  // PPC backend advertises
inline constexpr std::uint8_t kFlagH264 = 1u << 2;  // safe to encode with OpenH264

inline constexpr ModeDef kModes[] = {
    /* w     h    apple_id  refresh  flags */
    /* Classic 4:3 / 5:4 */
    {  640,  480, 0x82,        60,   kFlagM68k | kFlagPpc | kFlagH264 },  // VGA
    {  800,  600, 0x84,        60,   kFlagM68k | kFlagPpc | kFlagH264 },  // SVGA
    {  832,  624, 0x8B,        75,   kFlagM68k | kFlagPpc | kFlagH264 },  // Mac 16" RGB
    { 1024,  768, 0x85,        75,   kFlagM68k | kFlagPpc | kFlagH264 },  // XGA
    { 1152,  870, 0x8C,        75,   kFlagM68k | kFlagPpc | kFlagH264 },  // Mac 21" two-page
    { 1280, 1024, 0x88,        75,   kFlagM68k | kFlagPpc | kFlagH264 },  // SXGA
    { 1440,  900, 0x8D,        60,   kFlagM68k | kFlagPpc | kFlagH264 },  // 16:10
    /* Modern 16:9 / 16:10 desktop + entry-level laptop */
    { 1600,  900, 0x96,        60,   kFlagM68k | kFlagPpc | kFlagH264 },  // HD+ laptop
    { 1600, 1200, 0x89,        75,   kFlagM68k | kFlagPpc | kFlagH264 },  // UXGA
    { 1920, 1080, 0x8E,        60,   kFlagM68k | kFlagPpc | kFlagH264 },  // FHD; m68k cap
    /* PPC-only beyond here (m68k 8 MB framebuffer would overflow) */
    { 1920, 1200, 0x8F,        60,               kFlagPpc | kFlagH264 },  // WUXGA
    /* Common laptop natives — Apple Retina + Surface */
    { 2256, 1504, 0x97,        60,               kFlagPpc },              // Surface Laptop
    { 2560, 1080, 0x90,        60,               kFlagPpc | kFlagH264 },  // UWHD 21:9; h264 reliable ceiling
    /* h264 unreliable beyond 2560x1080 keyframes; client falls to vp9 */
    { 2560, 1440, 0x91,        60,               kFlagPpc },              // QHD
    { 2560, 1600, 0x98,        60,               kFlagPpc },              // MBP 13" / many laptops
    { 2880, 1800, 0x99,        60,               kFlagPpc },              // MBP 15" Intel
    { 3024, 1964, 0x9A,        60,               kFlagPpc },              // MBP 14" M-series (notch)
    { 3440, 1440, 0x92,        60,               kFlagPpc },              // UWQHD 21:9
    { 3456, 2234, 0x9B,        60,               kFlagPpc },              // MBP 16" M-series (notch)
    { 3840, 1080, 0x93,        60,               kFlagPpc },              // dual-4K side-by-side fill 32:9
    { 3840, 1600, 0x94,        60,               kFlagPpc },              // UWQHD+ 21:9
    { 3840, 2160, 0x95,        60,               kFlagPpc },              // 4K UHD
};

inline constexpr std::size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// Compile-time max dimensions per backend. Used to size the host-side
// frame ring buffer (VideoOutput) — must equal the largest mode the
// backend will ever switch to, otherwise out-of-bounds reads when the
// guest hops to a bigger mode mid-session.
template <std::uint8_t flag>
constexpr std::uint16_t _max_dim_for(bool height) {
    std::uint16_t m = 0;
    for (std::size_t i = 0; i < kModeCount; ++i) {
        if (!(kModes[i].flags & flag)) continue;
        std::uint16_t v = height ? kModes[i].h : kModes[i].w;
        if (v > m) m = v;
    }
    return m;
}

inline constexpr std::uint16_t kMaxWidth_M68k  = _max_dim_for<kFlagM68k>(false);
inline constexpr std::uint16_t kMaxHeight_M68k = _max_dim_for<kFlagM68k>(true);
inline constexpr std::uint16_t kMaxWidth_Ppc   = _max_dim_for<kFlagPpc>(false);
inline constexpr std::uint16_t kMaxHeight_Ppc  = _max_dim_for<kFlagPpc>(true);

inline constexpr const ModeDef* find_by_apple_id(std::uint16_t apple_id) {
    for (std::size_t i = 0; i < kModeCount; ++i) {
        if (kModes[i].apple_id == apple_id) return &kModes[i];
    }
    return nullptr;
}

inline constexpr const ModeDef* find_by_dims(std::uint16_t w, std::uint16_t h) {
    for (std::size_t i = 0; i < kModeCount; ++i) {
        if (kModes[i].w == w && kModes[i].h == h) return &kModes[i];
    }
    return nullptr;
}

}  // namespace mp::video
