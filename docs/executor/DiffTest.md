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
  - a key with `~` in it is printed for information and not compared
    (the machine's RAM, free memory);
  - `SECTION("name")`: sections, so a crashed run can resume at the next
    one (`Host:<probe>.start`);
  - `probe_pixels(key, gworld)`: a CRC of an offscreen drawing and, when
    `Host:<probe>.dump` exists, the pixels as a PPM (`<probe>-<crc of the
    key>.ppm`, the key in a comment line: HFS names end at 31 characters;
    the runner renames them `<probe>-<section>.<key>.ppm`);
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
| `appr` | AppearanceLib: Gestalt, clients, brushes and text colours, fonts, metrics, styles, window/button/track/tab regions, the theme collection, the private theme and desktop calls of the 8.6 Appearance control panel, every drawing primitive offscreen at 32 and 8 bits; window frames of the classic and Appearance procIDs on the screen, active and inactive (`frames`: 8.6's `DrawThemeWindowFrame` draws nothing into a GWorld); QuickDraw's arithmetic transfer modes (offscreen, basic and colour ports on the screen, CopyBits from `screenBits`) and a control panel's title drawing replayed |
| `ctl` | Control Manager 8: every standard kind (values, features, best rectangle, data sizes by tag), the classic definitions' best rectangles by title and font, embedding, disposal with subcontrols, keyboard focus, 32-bit values, control data with short buffers; Dialog Manager 8.0: items as controls, AppendDITL, ShortenDITL with embedded items |
| `menu` | Menu Manager with 8.x's calls: metacharacters, insertion order, marks, styles, command keys and glyphs, item icons by number and handle, CalcMenuSize, hierarchical items, MenusLib's 8.5 and private calls, the menu bar clock |
| `sys` | Gestalt (~120 selectors), FindFolder for each folder type and the folder's name, value-holding low-memory globals, the process's own information, Script Manager variables (system and Roman) |
| `wind` | Window Manager 2.0: CreateNewWindow for each class and attribute set (class, attributes, features, structure widths, bounds and regions by region code), the classic definitions through the same calls, SetWindowBounds, MoveWindowStructure, RepositionWindow, standard and ideal states, collapsing, properties, the modified state, content colour, floating windows, proxies |
| `text` | Font Manager and QuickDraw text: font numbers and names, RealFont, FMSwapFont's output, GetFontInfo, FontMetrics, every character's width, StringWidth, MeasureText, TruncString, for 10 fonts x 6 sizes x 9 styles |

What they found on their first runs (fixed): GetThemeSoundMask wrote a long
too many; GetItemStyle wrote a word into a byte; InsertMenuItem with
several items didn't reverse them (IMIV-55, as 8.6 does); a window's root
control didn't count as an embedder (CountSubControls, AutoEmbedControl
failed on it); classic controls answered -30581 where 8.6 answers -30580,
and control data calls wrote their size output on errors; GetBestControlRect
failed for definitions that can't compute one (8.6: noErr, the control's
rectangle); the Code Fragment Manager's calls weren't exported as
CodeFragmentMgr; SetMenuItemKeyGlyph took any item; IsMenuItemEnabled,
SetSubmenuParentChoosable, Get/SetMenuItemModifiers, GetMenuItemKeyGlyph,
InvalidateAllMenuSizes, InvalidateAllCachedMenuPixels,
RecalculateAllWindowTitleWidths were missing. What's open is in each
probe's `expected/` file.

`text` (with every character's width since 2026-10-30) found the Font
Manager answering 7.5's way or wrongly: no TrueType (8.6's system font,
Charcoal, is an outline only), the System file's FONDs hiding the Fonts
folder's, style extras from a fixed table instead of the FOND's, styled
metrics in FMSwapFont's output, the missing glyph measured by its image;
it matches 8.6 on every line now (MacOS8-9.md, "Fonts").

`sys` found Gestalt answering 7.5's values on 8.6 and FindFolder not
knowing 8.x's folders; Executor answers 8.6's now where it has what the
selector claims (`'menu'`, `'mixd'`, `'rsrc'`, `'nmgr'`, `'scri'`,
`'evnt'`, `'vers'`, `'tmgr'`, part of `'fs  '`: gestalt.cpp's
`since_table` and `gestalt_set_system_version`) and makes Application
Support, Contextual Menu Items and Cleanup At Startup at an 8.x startup.
`ctl` (best rectangles by title and font since 2026-10-30) found the
classic push button, check box and radio button not answering the 8.0
messages; they answer 8.6's features (0x2240, an unnamed bit 13 with
them), data and best rectangles now.

Open, from `sys`: selectors whose 8.6 answer claims what Executor lacks
(`'wind'`: Window Manager 2.0; `'qd  '` 2.5; `'fold'`'s folder
descriptors; `'ppc '`; `'dict'`; QuickTime, AppleScript), or that describe
the machine. From `ctl`: the popup menu's 8.0 messages, and 8.0 dialogs'
items as controls.

`wind` (2026-10-31) found most of Window Manager 2.0 missing:
CreateNewWindow among them, which answered 0 and left the caller holding
garbage for a window. Executor has 70 of WindowsLib's 74 calls now (not
'wind' resources, collections, content paint procs), with 8.6's answers:
CreateNewWindow's classes and the attributes each takes, kDocumentWindowClass
for every window, the title bar, title and drag regions only, properties
that remove without error, Window Manager 2.0's floating windows passed
over by FrontWindow. What stays different is the frame (platinum against
System 7's) and the screen size.

Planned: the Code Fragment Manager, Apple event coercions,
PrivateInterfaceLib.

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
