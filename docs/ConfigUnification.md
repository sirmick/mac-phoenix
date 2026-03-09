# Config System Unification — Complete

**Status**: Done (March 2026)

## What Changed

The dual config system (legacy `PrefsFindXXX` linked-list prefs + new `EmulatorConfig`) was unified into a single `EmulatorConfig` singleton.

### Deleted

- `src/core/prefs.cpp` (514 lines) — BasiliskII linked-list prefs
- `src/core/prefs_items.cpp` (139 lines) — default prefs values
- `src/common/include/prefs.h` (90 lines) — prefs API declarations
- `src/core/json_config.cpp` (571 lines) — intermediate JSON-to-prefs bridge
- `src/core/json_config.h` (63 lines)

### Result

All 30+ source files now use `config::EmulatorConfig::instance()` directly. One config format, one codepath, no sync issues.

### Config Separation (March 2026)

CLI args and file-based config are kept separate. `EmulatorConfig` tracks a `file_config_` JSON object representing what's on disk. CLI args override at runtime but are never persisted. The web UI uses `merge_ui_json()` which updates both the runtime config and `file_config_`, so only UI-modified values are saved. Environment variables are no longer supported — use CLI flags instead.

See [JsonConfig.md](JsonConfig.md) for the current config schema and usage.
