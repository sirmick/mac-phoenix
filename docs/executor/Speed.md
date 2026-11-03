# Speed: Executor against the ROM machines

How fast Executor runs Mac programs, 68k and PowerPC, natively and in the
tab, next to the ROM machines (a Quadra 650 on System 7.6.1, a Power Mac
G3 on Mac OS 8.6) on the CPU cores MacPhoenix has. The measure is the
Utilities disk's benchmarks (docs/Provisioning.md, "The utilities disk"),
run by `tools/bench/run.py`.

2026-11-03, `wash/bench` (Executor with the Time Manager fix below), on
this shared x86-64 box. In short, relative to a Quadra 605 (Speedometer's
own scale; a real Quadra 650 is 1.3 on CPU, a Power Mac 8100/80 4.2):

| | 68k code (CPU / Mix) | PowerPC code (CPU / Mix) | QuickDraw (Graphics) |
|---|---|---|---|
| Executor, native | 8.5 / 14.3 (mame-68k) | 56–60 / 171–182 (mame-ppc JIT); 4.6 / 18.5 without | 280–415 |
| Executor, the tab | 1.5 / 2.0 | 3.0 / 9.5 | 55–97 |
| Quadra 650 ROM machine | 30.9 / 42.1 (uae), 8.3 / 12.1 (mame-68k) | — | 25.6 (uae), 8.0 (mame-68k) |
| G3 ROM machine | — | 17.5 / 97.6 (kpx), 16–17.5 / 65–74 (mame-ppc) | — (no 1-bit: skipped) |

- Executor runs 68k code at mame-68k's speed: as the Quadra on
  mame-68k (8.5 against 8.3), a quarter of uae's interpreter. Its
  QuickDraw is the host's C++, 10 to 35 times the ROM machines'.
- PowerPC code with mame-ppc's native back-end is Executor's fastest:
  about 7 times its 68k, twice uae's 68k, and above the G3 ROM machine
  (whose Toolbox is still Apple's, much of it 68k under the nanokernel's
  emulator). Without the native back-end (`ppcinterp`) it drops to 4.6.
- The tab runs at about a fifth of native for 68k code and a twentieth
  for PowerPC code (wasm has no mame-ppc back-end: the UML interpreter);
  QuickDraw in the tab is still 2–4 times the ROM machines'.

## The table

Raw runs: `~/storage/tmp/bench/results.jsonl`; reports and screens of each result: `~/storage/tmp/bench/shots` (`CONFIG-BENCH-RUN-TEST.png`, `...-reportN.txt`).

### Speedometer 4.0

Medians of the runs. Scores: a Quadra 605 = 1 (FPU: a Quadra 650 = 1); Color: the mean of the depths that ran (how many of the five: depths).

| Configuration | runs | CPU | Graphics | Math | Mix | FPU | Color | depths | load | notes |
|---|---|---|---|---|---|---|---|---|---|---|
| quadra-uae | 3 | 30.9 | 25.6 | 530.8 | 42.1 | 28.0 | 30.1 | 5/5/5 | 3.5 |  |
| quadra-uae-jit | 3 | — | — | — | — | — | — | 0/0/0 | 3.0 | the Finder did not come up |
| quadra-mame68k | 3 | 8.3 | 8.0 | 165.1 | 12.1 | 9.9 | 10.0 | 5/5/5 | 2.3 |  |
| g3-kpx | 3 | 17.5 | — | 2267.7 | 97.6 | 33.2 | — | 0/0/0 | 1.3 |  |
| g3-kpx-jit | 3 | 131612.0 | — | 1837105.3 | 122732.3 | 60098.2 | — | 0/0/0 | 9.3 |  |
| g3-mameppc-jit | 3 | 17.5 | — | 491.9 | 74.2 | 27.4 | — | 0/0/0 | 9.2 |  |
| g3-mameppc | 3 | 16.1 | — | 479.7 | 64.8 | 24.3 | — | 0/0/0 | 9.6 |  |
| exec-761-68k | 3 | 8.5 | 281.5 | 200.2 | 14.3 | 8.6 | 377.4 | 4/4/4 | 5.6 | color 2: crashed |
| exec-761-ppc | 3 | 56.1 | 301.3 | 7193.4 | 171.3 | 69.1 | 441.3 | 4/4/4 | 6.9 | color 2: crashed |
| exec-86-jit | 3 | 59.5 | 414.7 | 7148.4 | 181.8 | 65.0 | 516.8 | 4/4/4 | 2.6 | color 2: crashed |
| exec-86 | 3 | 4.6 | 299.3 | 333.2 | 18.5 | 5.6 | 395.0 | 4/4/4 | 8.1 | color 2: crashed; color 3: crashed |
| tab-761-68k | 3 | 1.5 | 54.9 | 22.9 | 2.0 | 1.5 | 75.5 | 5/5/5 | 3.2 |  |
| tab-761-ppc | 3 | 3.1 | 64.8 | 159.5 | 9.5 | 3.0 | 93.3 | 5/5/5 | 3.5 |  |
| tab-86 | 3 | 3.0 | 86.5 | 169.0 | 8.7 | 3.2 | 123.4 | 5/5/5 | 3.8 |  |

### MacBench 5.0

Medians. Scores: a Power Macintosh G3/300 = 1000.

| Configuration | runs | Processor | Floating Point | Processor s | Floating Point s | notes |
|---|---|---|---|---|---|---|
| g3-kpx | 3 | 996 | 1093 | 25.8 | 44.9 |  |
| g3-kpx-jit | 3 | 16224 | — | 105.7 | — | fp: no score |
| g3-mameppc-jit | 1 | — | — | — | — | no main window (stays on its splash) |
| g3-mameppc | 1 | — | — | — | — | no main window (stays on its splash) |
| exec-761-ppc | 1 | — | — | — | — | no main window (stays on its splash) |
| exec-86-jit | 1 | — | — | — | — | no main window (stays on its splash) |
| exec-86 | 1 | — | — | — | — | no main window (stays on its splash) |

## Method

`tools/bench/run.py` (`--only`, `--runs`, `--bench`; `--table DIR` makes the
table again from a run's `results.jsonl`):

- **Configurations.** The ROM machines: the Quadra 650 ROM on System
  7.6.1 (`7.6.1.img`) on uae's interpreter, uae with `--jit` and
  `mame-68k`; the G3 ROM on Mac OS 8.6 (`8.6.img`) on Kheperix
  (`--core kpx`, with and without `--jit`) and `mame-ppc` (`--jit`, its
  native back-end, and `--no-jit`, its UML interpreter). Executor
  natively on Apple's 7.6.1 (68k code on `mame-68k`; `--executor-ppc`:
  the PowerPC side of fat applications on `mame-ppc`) and on 8.6
  (PowerPC; `mame-ppc` with its native back-end, and without it:
  `--executor-debug ppcinterp`). Executor in the tab: the wasm build
  (`build-wasm`, Release) in headless Chromium on `tools/standalone`,
  booting the site's 7.6.1 and 8.6 disks (`boot-7.6.1.img`,
  `boot-8.6.img`: `tools/web/make_boot_disk.py`, the System Folder cut to
  what Executor runs), `?ppc=1` for PowerPC (`tools/bench/tab.mjs`).
- **Each session**: `--config /dev/null`, a scratch `--storage-dir` with
  fresh copies of the System's image and `utilities.img` (written:
  Speedometer saves its reports there; Executor `--executor-writable-images`),
  640x480, `--no-idlewait`, `--network none`, `--depth 8` where the
  machine takes one (the 68k ROM machine, Executor; the tab `?depth=8`).
  The G3 has no `--depth`: it starts as 8.6 left it.
- **Speedometer 4.0** (fat: 68k and PowerPC code): launched by the bridge,
  its splash clicked, Not Yet at its registration screen, then Tests >
  Performance Rating (CPU, Graphics, Disk on the Utilities disk, Math),
  Benchmark Mix, FPU Benchmarks, each with every test ticked, then Color
  QuickDraw a depth at a time (Monochrome, 2, 4, 8, 16 bits); after each,
  File > Save Text Report into its folder. The scores are read from those
  reports, out of the disk image after the session (hfsutils; the tab:
  the image as changed, downloaded from the page). A session that crashes
  in a colour depth goes on with the next depth in a new session.
- **MacBench 5.0** (PowerPC only): its licence scrolled to the end and
  agreed to, registered, then Tests > Processor and Tests > Floating
  Point; the scores read off the screen (`/api/text`, the ROM machines).
- **Runs**: 3 of each configuration, one after another (never two
  sessions at once), the median of each score. The box is shared: the
  load average at the start of each run is in the table.

Scores are Speedometer's own: relative to a **Quadra 605 = 1** (the FPU
tests: a **Quadra 650 = 1**). Speedometer's machine records give real
machines on the same scale (Analysis > Systems Comparison;
`~/storage/tmp/bench/shots/reference-*.png`):

| Real machine | PR | CPU | Graphics | Math | Mix | FPU | Color |
|---|---|---|---|---|---|---|---|
| Quadra 650 | 1.56 | 1.32 | 1.31 | 19.16 | 1.89 | 1.00 | 1.08 |
| Power Mac 8100/80AV | 2.84 | 4.19 | 2.08 | 143.54 | 7.38 | 5.94 | 1.73 |

MacBench's scores are relative to a **Power Macintosh G3/300 = 1000**
(its own Results folder).

## Caveats

- **The scores are guest-timed.** Speedometer times its tests with the
  Time Manager (`PrimeTime` with -1,800,000,000 µs, the test, `RmvTime`;
  the elapsed time from `tmCount`) and runs each test for about the same
  wall time everywhere (PR ~11 s, Mix ~16 s, FPU ~4 s), counting
  iterations. A machine whose clock isn't host time scores what its
  clock says: mame-ppc's G3 counts its timebase in its own cycles, so the
  G3 on mame-ppc scores alike with and without its JIT (17.5 against
  16.1), whatever the host does. Executor and uae keep host time.
- **Executor before d4f09192 timed nonsense**: its `RmvTime` never left
  the time remaining in `tmCount` (Inside Macintosh: Processes 3-21),
  and `Microseconds` stepped in milliseconds; a 2 s colour test read as
  29 s. Fixed on this branch (gtest `TimeMgr.*`).
- **The G3 on Kheperix with `--jit`** gives no usable numbers: its clock
  doesn't advance within a test (Dhrystones = 2,147,483.647, Speedometer's
  ceiling); without the JIT the G3 on Kheperix is steady (17.512 three
  times).
- **The G3 has no 1-bit mode**: Speedometer skips its Graphics test and
  the colour depths ("This machine does not support monochrome graphics"),
  so those columns are empty for the G3 rows.
- **Performance Rating** is left out: the report garbles its line
  (`Performance Rating:  KWhetsto15.359`) and the PowerPC rows' values
  don't follow their parts.
- **Executor crashes in the 4-bit colour test** after the 2-bit one,
  natively (7.6.1 and 8.6, 68k and PowerPC; not in the tab): a nil
  region handle in `InsetRgn`, then `XorRgn` from `FrameOval` (gdb:
  `~/storage/tmp/bench/crash-xorrgn-gdb.txt`), most likely the
  application heap exhausted after the depth switches. Not fixed here; the
  colour column is the mean of the other four depths for those rows.
- **uae with `--jit`** doesn't reach the Finder in these sessions (the
  known uae JIT problem); no row.
- **MacBench 5.0** gets past its splash only on the G3 on Kheperix
  (Processor 996, Floating Point 1093 against a G3/300's 1000; with
  `--jit` Processor reads 16224 and Floating Point never finishes: the
  same clock as Speedometer's). On mame-ppc (the G3 ROM machine and
  Executor) it spins after the splash without calling a trap, on the ROM
  machine too, so not Executor's; one attempt each. Not in the tab (no
  `/api/text` there to read its scores).
- **Variance**: the box is shared (load average 1 to 16 during the runs,
  in the table); single runs differ by up to 2x under load (Executor
  7.6.1 PowerPC: CPU 33 at load 9.5, 56–60 at load 3–7; Graphics 98 to
  309). The medians of 3 are steady to about 10%, but a quiet box and
  more runs would do better.
- **Different Systems**: the tab boots the site's cut-down boot disks,
  native Executor and the ROM machines Apple's full 7.6.1 and 8.6 images;
  the tab gives Apple's System 128 MB, native runs 64 MB.

## Our own benchmark (proposal)

Speedometer and MacBench measure what their authors chose, through
dialogs we have to click, with clocks we don't control; they crash or
hang on some configurations and swing with load. A benchmark of our own,
a plain C program like the guest test apps (`tests/guest/*`, built fat
68k + PowerPC by `tools/mpw/mpw` or Retro68), would run unattended on
every configuration and report numbers we can track release to release.

**Tests** (each a fixed amount of work, timed with `Microseconds` and
also with the host's clock through the bridge, so a guest clock that
isn't host time shows; each repeated N times, min/median reported):

- integer: a Dhrystone-like loop, a sieve, a sort, CRC32 over 1 MB;
- floating point: a Whetstone-like loop, an FFT, a matrix multiply
  (`double`; 68k: 68881 code and SANE separately);
- memory: `BlockMove` of 1 KB / 64 KB / 1 MB, `memcpy`, a strided walk;
- QuickDraw: `CopyBits` 640x480 screen-to-offscreen and back at 1, 8 and
  32 bits, `PaintRect`/`FrameOval`, `DrawString` 1,000 times (bitmap and
  TrueType), region ops (`UnionRgn`, `InsetRgn` on a complex region);
- Toolbox trap overhead: 100,000 calls each of a cheap trap
  (`TickCount`, `GetPort`/`SetPort`, `Gestalt`), from 68k and from
  PowerPC (the Mixed Mode switch on a Power Mac);
- File Manager: create, write and read back 4 MB in 4 KB and 64 KB
  blocks, open/close 1,000 times, on the boot volume and a disk image;
- Memory Manager: `NewHandle`/`DisposeHandle` 10,000 times, `HLock` /
  `MoveHHi`, a compacting heap;
- timing sanity: `Microseconds` against `TickCount` over 2 s (the
  clock's resolution and drift).

**Reporting**: one line per test (`name iterations µs` and the host's
elapsed time) to a results file on the Host volume, as MacPhoenixSuite
does; the app quits when done. `tools/bench/run.py` launches it by the
bridge and reads the file, so no dialogs, no screenshots to match; the
tab reads it from the page's volume (`read`). Same build on every row.

**Effort**: the program and its tests 1–2 days (most of the code is
standard loops; the QuickDraw and File Manager tests need a window and a
scratch file); fat build and the run.py side (launch, wait for the
file, table) half a day; a ctest cell that runs it on Executor 7.6.1
68k and PPC and checks it completes, half a day. About 3 days, plus the
first full table.
