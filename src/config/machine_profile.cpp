#include "machine_profile.h"
#include <cstdio>

static const MachineProfile profile_se = {
    "se",
    0x0276,         // rom_version
    0x400000,       // rom_base_mac (fixed at 4MB for 24-bit)
    0x400000,       // max_ram (4MB limit)
    0,              // cpu_type = 68000
    false,          // fpu
    true,           // twenty_four_bit
    5,              // model_id
    -33,            // disk_refnum
    512,            // screen_width
    342,            // screen_height
    true,           // mono_framebuffer
    64              // screen_row_bytes
};

static const MachineProfile profile_quadra = {
    "quadra",
    0x067c,         // rom_version
    0,              // rom_base_mac (0 = use ram_size)
    0,              // max_ram (0 = no limit)
    4,              // cpu_type = 68040
    true,           // fpu
    false,          // twenty_four_bit
    14,             // model_id
    -63,            // disk_refnum
    640,            // screen_width
    480,            // screen_height
    false,          // mono_framebuffer
    0               // screen_row_bytes
};

static const MachineProfile* g_profile = &profile_quadra;

void set_machine_profile(uint16_t rom_version)
{
    if (rom_version == 0x0276) {
        g_profile = &profile_se;
    } else {
        g_profile = &profile_quadra;
    }
    fprintf(stderr, "[MachineProfile] Selected: %s (ROM version 0x%04x)\n",
            g_profile->name, rom_version);
}

const MachineProfile& machine_profile()
{
    return *g_profile;
}
