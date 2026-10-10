# Executor stragglers — where we are (2026-10-09)

The list agreed after the Finder scripting work. Speech, QuickTime and
networking are punted until later; everything else is in scope.

| # | Item | State |
|---|------|-------|
| 1 | `tell application "Finder"` (aliases, HLE sessions, folder listing) | done — 07dce3db; Finder suite 25/25 on UAE and Executor |
| 2 | Keyboard test, SimpleText | done — 56200715 (test repaired, in ctest), da6ac62b (save: an open file can't be deleted) |
| 3 | Dates in Script Editor ("program error") | done — f6793d47: Script Manager variables, IULDate/TimeString, StringToDate/Time, number formats; matches 7.5.5 |
| 4 | Extension stragglers: Date & Time (menu clock), General Controls (`OSDispatch $5E`, `WriteXPRam`), Find File (`SetGestaltValue`), Apple Guide (`GetFrontProcess`) | **next** |
| 5 | Redraw leftovers behind Script Editor's windows | open |
| 6 | `BeginSystemMode` (only a counter) | open |
| 7 | Color Picker (`RegisterComponentResourceFile`) | open |
| 8 | Startup Items: Finder's real rule (Executor's Process Manager stands in) | open |
| 9 | Desktop DB: icons and rebuild | mostly done — 3fa70c80 (HFS images report a Desktop Manager; icons from bundles); comments (`Desktop DF`) and icons from other volumes' databases open |
| 10 | Apple Event Manager list wire format | open |
| 11 | System heap growth | open |
| 12 | DRVR unit clash | open |
| 13 | ctest port 18108 used twice | done — 56200715 |
| 14 | `command_bridge_executor` flake | open (not seen recently) |
| 15 | Executor's own host files visible in Finder | done — 07dce3db |

Also fixed along the way: Finder type-to-select crash (61ef0717), host
file dates in local time, `ioVLsMod`, folder moves keeping their
contents' paths (07dce3db).

## Parked

### Heap corruption in the Executor guest suite

`guest_suite_executor` (both cores) crashes at the start of its network
section: MacPerl's heap is corrupt by the time `require Socket` runs, and
the CPU ends up executing data (UAE core: guest pc `$03cd74cc`; Musashi:
pc 0). Facts so far:

- It needs the whole suite: disk + shared folder + audio + the AppleScript
  date/number checks + network. Dropping any one of disk, extfs or audio
  avoids it; so does dropping the AppleScript block. Not reproduced with
  smaller scripts.
- It happens without f6793d47's code too (the AppleScript checks then fail
  but still run), so it is older than the date work; heavy AppleScript use
  in MacPerl exposes it. Before the AppleScript checks were added, the
  suite ran to the end (with the punted network failures only).
- `--debug memcheck` (Executor's heap checker; pass it by adding
  `--debug memcheck` to the args in
  `romlib/config/front-ends/phoenix/executor_host.cpp`, and make
  `mm_fatal` in `mman/mmansubr.cpp` report instead of crash) finds an
  earlier problem: at tick 14 of boot, in Finder's application heap (zone
  `0x3df7bc4`), REL block `0x3e08398`'s handle `0x3df7c6c` doesn't point
  back to it. Whether that is the same bug, or Finder doing something the
  checker doesn't expect, isn't known yet.
- Likely class: host code holding a raw pointer into a relocatable block
  across a call that moves memory.

### 68k Toolbox test app (Retro68)

`tests/test_toolbox_suite.sh` builds Executor's test sources into a Mac
app and runs it on UAE (ground truth) or Executor. Infrastructure works;
the app crashes depending on code layout, and on Executor another Retro68
app's segment-loader patches are active when it launches. Details in the
script's header. Not in ctest.

## Environment

Executor on the UAE core needs `vm.mmap_min_addr=0`; it is kept by
`/etc/sysctl.d/99-mac-phoenix.conf` (another project's script runs
`sysctl --system`, which reset it to Ubuntu's 65536).
