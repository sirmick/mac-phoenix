# Config System Unification Plan

## Problem

Two config systems read/write `~/.config/mac-phoenix/config.json`:

| System | Files | JSON Format | Used By |
|--------|-------|-------------|---------|
| **Legacy Prefs** | `json_config.cpp`, `prefs.cpp`, `prefs_items.cpp` | Nested: `storage.disks`, `boot.bootdriver`, string CPU `"68030"` | Emulator core (PrefsFindXXX) |
| **EmulatorConfig** | `emulator_config.cpp/.h` | Flat: `disks`, `bootdriver`, int `cpu_type: 4` | Web API, web UI |

They overwrite each other, silently dropping fields the other doesn't know about.

## Strategy

Hard cut. Delete the entire legacy Prefs system. Replace all ~45 `PrefsFindXXX()` calls in 12 consumer files with direct `EmulatorConfig` field access via a global pointer.

No bridge. No sync. No migration.

## Implementation

### Step 1: Make EmulatorConfig a singleton

```cpp
// src/config/emulator_config.h
class EmulatorConfig {
public:
    static EmulatorConfig& instance();

    // No copy/move
    EmulatorConfig(const EmulatorConfig&) = delete;
    EmulatorConfig& operator=(const EmulatorConfig&) = delete;

    // Existing API
    nlohmann::json to_json() const;
    void merge_json(const nlohmann::json& j);
    bool save() const;

    // Fields...
private:
    EmulatorConfig() = default;
};
```

Initialized in `main.cpp` via `EmulatorConfig::instance().merge_json(...)`. All core code reads `EmulatorConfig::instance().field` instead of `PrefsFindXXX("field")`. Testable by calling `merge_json()` with test values before running code under test.

### Step 2: Add missing fields to EmulatorConfig

Fields currently only in legacy Prefs that active code reads:

```
bootdrive (int, 0)           — emulator_init.cpp, cpu_context.cpp
zappram (bool, true)         — emulator_init.cpp
nocdrom (bool, false)        — rom_patches.cpp, platform_unix.cpp
frameskip (int, 6)           — (if used)
yearofs, dayofs (int, 0)     — macos_util.cpp
udptunnel (bool, false)      — ether.cpp
udpport (int, 6066)          — ether.cpp
extfs (string, "")           — extfs.cpp
floppy_paths (vector<string>)— sony.cpp
jit* fields (7 total)        — compemu_support.cpp
```

Add to EmulatorConfig struct, add to `to_json()`/`merge_json()`.

### Step 3: Replace PrefsFindXXX calls in each consumer

| File | Calls | Replacement |
|------|-------|-------------|
| `emulator_init.cpp` (7) | `PrefsFindInt32("cpu")` etc. | `EmulatorConfig::instance().m68k.cpu_type` |
| `cpu_context.cpp` (10) | `PrefsReplace/Add` block | Delete (config already loaded) |
| `main.cpp` (3) | `PrefsInit` + ad-hoc syncs | Delete |
| `adb.cpp` (1) | `PrefsFindInt32("keyboardtype")` | `EmulatorConfig::instance().m68k.keyboardtype` |
| `disk.cpp` (2) | `PrefsFindString("disk", i)` | `EmulatorConfig::instance().disk_paths[i]` |
| `cdrom.cpp` (1) | `PrefsFindString("cdrom", i)` | `EmulatorConfig::instance().cdrom_paths[i]` |
| `sony.cpp` (2) | `PrefsFindString("floppy", i)` | `EmulatorConfig::instance().floppy_paths[i]` |
| `rom_patches.cpp` (2) | `PrefsFindBool("nocdrom")`, `PrefsFindInt32("modelid")` | `EmulatorConfig::instance().nocdrom`, `EmulatorConfig::instance().m68k.modelid` |
| `rsrc_patches.cpp` (1) | `PrefsFindBool("idlewait")` | `EmulatorConfig::instance().m68k.idlewait` |
| `platform_unix.cpp` (28) | `PrefsFindBool("nocdrom")`, cdrom strings, `SysAddCDROMPrefs` | `EmulatorConfig::instance().nocdrom`, `EmulatorConfig::instance().cdrom_paths` |
| `ether.cpp` (2) | `PrefsFindBool("udptunnel")`, `PrefsFindInt32("udpport")` | `EmulatorConfig::instance().udptunnel`, `EmulatorConfig::instance().udpport` |
| `macos_util.cpp` (4) | yearofs, dayofs | `EmulatorConfig::instance().yearofs`, `EmulatorConfig::instance().dayofs` |
| `extfs.cpp` (1) | `PrefsFindString("extfs")` | `EmulatorConfig::instance().extfs` |
| `compemu_support.cpp` (8) | 7 JIT prefs | `EmulatorConfig::instance().m68k.jit*` |

### Step 4: Delete legacy Prefs system

Delete these files entirely:
- `src/core/json_config.cpp`
- `src/core/json_config.h`
- `src/core/prefs.cpp`
- `src/core/prefs_items.cpp`
- `src/common/include/prefs.h`

Remove from `src/core/meson.build`.

Remove `#include "prefs.h"` from all files touched in Step 3.

### Step 5: Clean up cpu_context.cpp

The entire block that syncs EmulatorConfig → Prefs (lines 270-291) becomes dead code. Delete it. `init_m68k()` just uses `EmulatorConfig::instance().` directly.

## Testing

### New: Config round-trip test (`tests/test_config.sh`)

```bash
# Write known config, start emulator, verify /api/config returns matching values
TMPCONFIG=$(mktemp)
cat > "$TMPCONFIG" << 'EOF'
{"ram_mb":64,"bootdriver":-62,"m68k":{"cpu_type":3}}
EOF
timeout 5 ./build/mac-phoenix --config "$TMPCONFIG" --no-webserver --timeout 2
# Verify exit 0
```

### New: Config save/reload test (`tests/test_config_save.sh`)

```bash
# Start emulator, POST new config, kill, restart, verify persisted
curl -X POST localhost:8126/api/config -d '{"ram_mb":128}'
# Kill and restart
curl localhost:8126/api/config | jq -e '.ram_mb == 128'
```

### New: Settings dialog E2E (`tests/e2e/settings.spec.ts`)

1. Open settings, verify controls disabled during load
2. Verify controls enable after load
3. Verify saved values reflected (ROM, disks, CDROMs, boot priority)
4. Change setting, save, reopen, verify persisted

### Existing tests (regression gates)

```bash
meson test -C build api_endpoints boot_uae mouse_position
```

## File Changes

| File | Action |
|------|--------|
| `src/config/emulator_config.h` | Make singleton, add missing fields |
| `src/config/emulator_config.cpp` | Extend serialization for new fields |
| `src/main.cpp` | Init singleton, remove `PrefsInit` |
| `src/core/cpu_context.cpp` | Remove prefs sync block, use `EmulatorConfig::instance().` |
| `src/core/emulator_init.cpp` | Replace PrefsFindXXX with `EmulatorConfig::instance().` |
| `src/core/adb.cpp` | Replace PrefsFindXXX |
| `src/core/disk.cpp` | Replace PrefsFindXXX |
| `src/core/cdrom.cpp` | Replace PrefsFindXXX |
| `src/core/sony.cpp` | Replace PrefsFindXXX |
| `src/core/rom_patches.cpp` | Replace PrefsFindXXX |
| `src/core/rsrc_patches.cpp` | Replace PrefsFindXXX |
| `src/core/macos_util.cpp` | Replace PrefsFindXXX |
| `src/core/extfs.cpp` | Replace PrefsFindXXX |
| `src/core/ether.cpp` | Replace PrefsFindXXX |
| `src/drivers/platform/platform_unix.cpp` | Replace PrefsFindXXX |
| `src/cpu/uae_cpu/compiler/compemu_support.cpp` | Replace PrefsFindXXX |
| `src/core/json_config.cpp` | **DELETE** |
| `src/core/json_config.h` | **DELETE** |
| `src/core/prefs.cpp` | **DELETE** |
| `src/core/prefs_items.cpp` | **DELETE** |
| `src/common/include/prefs.h` | **DELETE** |
| `src/core/meson.build` | Remove deleted files |
| `tests/test_config.sh` | **NEW** |
| `tests/test_config_save.sh` | **NEW** |
| `tests/e2e/settings.spec.ts` | **NEW** |
