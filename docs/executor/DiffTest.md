# Differential tests: Apple's libraries against Executor's

Executor reimplements Mac OS 8/9's Toolbox libraries (AppearanceLib,
WindowsLib, ControlsLib, MenusLib, MacDialogsLib, ...). Where it answers
differently from Apple's code, programs break, often far from the
cause (a call Executor lacks answers 0, the caller takes stale memory for
its output). A differential probe asks both the same questions and
compares the answers line by line.

## Pieces

- **Probes**: `tests/guest/difftest/<probe>.c`, PowerPC applications built
  with Retro68 against the Multiversal headers
  (`tests/guest/difftest/build-retro68.sh [probe ...]` writes
  `<probe>PPC.bin`, committed). They include `probe.h`:
  - `out(key, fmt, ...)`, `out_hex`, `out_str`, `out_rect`, `out_rgn`,
    `out_handle`: one `section.key: value` line each, flushed at once;
    contents (bytes, sizes, CRCs), never addresses or times;
  - fill outputs with `SENTINEL` (0xA5) before each call: an output a call
    leaves alone prints as `a5a5...`;
  - `SECTION("name")`: sections, so a crashed run can resume at the next
    one (`Host:<probe>.start`);
  - `probe_pixels(key, gworld)`: a CRC of an offscreen drawing and, when
    `Host:<probe>.dump` exists, the pixels as a PPM;
  - `probe_bind(lib, name)`: a routine looked up with `FindSymbol` (private
    calls, calls that may be missing): `bind.<name>: found/missing`, and
    the probe carries on without it.
- **Runner**: `tools/difftest/run.py <probe> [--target T] [--dump]
  [--refresh | --no-refresh] [--accept]`. Targets: `g3-8.6`, `g3-9.0.4`
  (Apple's code: the G3 ROM on mame-ppc) and `executor-8.6`, which is
  compared with `g3-8.6`. Each run goes through `tests/test_guest_suite.sh`
  (`--keep-dir` keeps what the probe wrote on Host:, `--host-file` puts
  the control files there). A run that crashed is resumed after the
  crashed section, as often as it takes.
- **References**: `tests/guest/difftest/ref/<probe>-<target>.txt`,
  committed, made from the G3 runs. Their first line is the probe binary's
  SHA-256: a rebuilt probe makes them stale, and the runner makes them
  again (`--no-refresh` compares with them as they are, for machines
  without the ROM).
- **Expected differences**: `tests/guest/difftest/expected/<probe>-<target>.txt`,
  `key-glob<TAB>reason` lines. The report (`<storage>/difftest/<probe>/report-<target>.md`)
  lists the unexpected differences (the exit status), lines missing from the
  run, extra lines, and expected entries that now match (remove them). With
  `--dump`, a side-by-side PNG of each differing drawing.
  `--accept` appends the current differences with reason "TODO".

## Probes

| Probe | Covers |
|---|---|
| `appr` | AppearanceLib: Gestalt, clients, brushes and text colours, fonts, metrics, styles, window/button/track/tab regions, the theme collection, the private theme and desktop calls of the 8.6 Appearance control panel, every drawing primitive offscreen at 32 and 8 bits |

Planned (docs/executor/MacOS8-9.md): Control Manager 8, Dialog Manager 8,
MenusLib 8.5, Window Manager 2.0, Font Manager and QuickDraw text, Gestalt/
Folder Manager/low memory/Process Manager, the Code Fragment Manager,
Apple event coercions, PrivateInterfaceLib.

## Related tools

- `tools/macdecode/libraries.py --census --rom ROM --system FOLDER --logs DIR`:
  each symbol Apple's libraries export that Executor's lack, ranked by the
  calls sessions made (grid logs, `<storage>/executor/unimplemented.log`)
  and by how many programs import it; weak imports counted (Executor logs
  `CFM: weak import X unresolved (nil)`: a program that calls one without
  checking jumps to 0).
- `tools/executor/grid_compare.py OLD NEW`: two grid runs item by item
  (result, front process, unimplemented calls, changed screenshots with
  side-by-side PNGs).

## Writing a probe

1. `tests/guest/difftest/<name>.c`: `probe_open("<name>")`, sections, `out`
   lines, `probe_close()`. Keep sections independent (a resumed run starts
   a new process: what an earlier section set up is gone).
2. Add it to `tests/guest/difftest/CMakeLists.txt`; `build-retro68.sh <name>`.
3. `tools/difftest/run.py <name>` (makes the G3 reference), read the report,
   fix Executor or record the difference in `expected/`.
4. A ctest cell (`difftest_<name>`, `--no-refresh`).
