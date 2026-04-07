#ifndef MACHINE_PROFILE_H
#define MACHINE_PROFILE_H
#include <cstdint>

struct MachineProfile {
    const char* name;
    uint16_t rom_version;
    uint32_t rom_base_mac;
    uint32_t max_ram;
    int cpu_type;       // 0=68000, 2=68020, 3=68030, 4=68040
    bool fpu;
    bool twenty_four_bit;
    uint8_t model_id;
    int disk_refnum;
    int screen_width;
    int screen_height;
    bool mono_framebuffer;
    int screen_row_bytes;
};

void set_machine_profile(uint16_t rom_version);
const MachineProfile& machine_profile();

#endif
