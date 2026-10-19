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

// rom_checksum (the ROM's first long) names the exact ROM; machines that
// share a ROM version but not a layout (IIci vs Quadra) are told apart by it.
void set_machine_profile(uint16_t rom_version, uint32_t rom_checksum);
const MachineProfile& machine_profile();

#endif
