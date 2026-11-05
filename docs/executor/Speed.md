# Speed: Executor against the ROM machines

How fast Executor runs Mac programs, 68k and PowerPC, natively and in the
tab, next to the ROM machines (a Quadra 650 on System 7.6.1, a Power Mac
G3 on Mac OS 8.6) on the CPU cores MacPhoenix has. The measure is the
Utilities disk's benchmarks (docs/Provisioning.md, "The utilities disk"),
run by `tools/bench/run.py`, and our own benchmark, MacPhoenixBench
(below, "MacPhoenixBench: our own"), timed by the host's clock as well as
the guest's, with the MMU's cost on the MAME cores.

2026-11-03, `wash/bench` (Executor with the Time Manager fix below), on
this shared x86-64 box. In short, relative to a Quadra 605 (Speedometer's
own scale; a real Quadra 650 is 1.3 on CPU, a Power Mac 8100/80 4.2):

| | 68k code (CPU / Mix) | PowerPC code (CPU / Mix) | QuickDraw (Graphics) |
|---|---|---|---|
| Executor, native | 8.5 / 14.3 (mame-68k) | 56–60 / 171–182 (mame-ppc JIT); 4.6 / 18.5 without | 280–415 |
| Executor, the tab | 1.5 / 2.0 | 3.0–3.1 / 8.7–9.5 | 55–87 |
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
  QuickDraw in the tab is still 2–3.5 times the uae Quadra's.

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

## MacPhoenixBench: our own

`tests/guest/bench` is a benchmark of our own, a plain C program built fat
(68k and PowerPC code, `tools/mpw/mpw build tests/guest/bench`; the
committed `MacPhoenixBench.bin`). Each test does a fixed amount of work, N
times (5 here), timed by `Microseconds` and by the host's clock through
the bridge trap (`HOST_CLOCK`, docs/CommandBridge.md), so a guest clock
that isn't host time shows; min and median of each go to
`bench-results.txt`, and the application quits. Nothing to click:

```bash
tools/bench/run.py --bench own [--only CONFIG,...] [--runs 3] [--own-runs 5] [--tests "dhry copybits"]
tools/bench/run.py --table DIR       # the tables again from DIR/results.jsonl
```

run.py makes a 16 MB HFS disk "Bench" with the application and its
`bench.cfg` (runs; tests), mounts it after the System's, launches
`Bench:MacPhoenixBench` by the bridge (the tab: `request('launch')`),
waits for it to quit and reads the results off the image (the tab: the
image as changed, downloaded). A whole session takes 5 s (Executor) to
5 minutes (the G3 on mame-ppc's interpreter), boot included.

**Tests.** Integer: a Dhrystone-like loop, a sieve, a quicksort of 20,000
longs, CRC-32 of 1 MB. Floating point (`double`): a Whetstone-like loop, a
1024-point FFT, a 48x48 matrix multiply; on 68k twice, with SANE
(`_sane`) and with 68881 code (`_881`, SC `-mc68881 -elems881`;
skipped without an FPU). Memory: `BlockMove` of 1 KB, 64 KB and 1 MB and
`memcpy` of 64 KB (32 MB moved each), every 64th byte of 1 MB 16 times.
QuickDraw, in a window over the screen below the menu bar (640x460):
`CopyBits` window to GWorld and back at 1, 8 and 32 bits (50 copies),
`PaintRect`/`EraseRect`, `FrameOval`, `DrawString` 1,000 times in Monaco
9 and Times 13 (TrueType where the System has it), `UnionRgn` and
`InsetRgn` of a 60-oval region. Toolbox calls: 100,000 each of a plain
function call through a pointer (for scale), `TickCount`,
`GetPort`+`SetPort`, `Gestalt`; on PowerPC, 10,000 Mixed Mode switches to
a 68k `rts`. Files, on the boot volume's root and on the Bench disk: 4 MB
written and read back in 4 KB and 64 KB blocks (open to close), 1,000
opens and closes, 200 creates and deletes. Memory Manager: 10,000
`NewHandle`/`DisposeHandle`, `HLock`/`HUnlock`/`MoveHHi` of 1,000
handles, five rounds of a 2,000-handle heap half freed and compacted.
And the clocks: `Microseconds` and the host's over 120 ticks, and the
smallest step `Microseconds` takes.

### The table

2026-11-04, `wash/bench-app`, this shared box (one emulator at a time;
`~/storage/tmp/bench-app/out`: `results.jsonl`, each session's
`shots/*-results.txt`). 3 sessions of each configuration, 5 runs of each
test in a session. The configurations are the Speedometer table's, and
the MMU baselines (below).

Speed against quadra-uae (= 1; higher is faster): per group, the geometric mean of the tests' ratios of median times by the host's clock; [by the guest's Microseconds] where that says more than 10% otherwise. FP: the 68881 code's tests (PowerPC's against them; SANE's in the times below). clock: Microseconds over 2 s of the host's.

| Configuration | code | Integer | FP | Memory | QuickDraw | Traps | Files | Heap | clock | notes |
|---|---|---|---|---|---|---|---|---|---|---|
| quadra-uae | 68k | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 |  |
| quadra-mame68k | 68k | 0.29 | 0.42 | 0.41 | 0.39 | 0.29 | 0.34 | 0.30 | 1.00 |  |
| quadra-mame68k-mmu | 68k | 0.31 | 0.43 | 0.41 | 0.40 | 0.30 | 0.34 | 0.30 | 1.00 |  |
| g3-kpx | ppc | 2.16 [1.28] | 7.91 [5.44] | 2.14 [1.09] | 0.49 | 0.56 [0.36] | 0.05 [0.06] | 3.75 [2.40] | 0.87 |  |
| g3-kpx-jit | ppc | 14.0 [13398.9] | 31.5 [6538.6] | 16.5 [15584.4] | 2.72 [350.5] | 3.89 [78.5] | 0.34 [0.10] | 24.5 [6112.5] | 6.57 |  |
| g3-mameppc-jit | ppc | 9.53 [1.33] | 7.25 [4.61] | 5.03 [1.14] | 1.74 [0.44] | 1.58 [0.40] | 0.22 [0.07] | 14.9 [2.36] | 2.83 |  |
| g3-mameppc-jit-mmu | ppc | 7.44 [1.33] | 6.70 [4.61] | 4.52 [1.14] | 1.56 [0.44] | 1.46 [0.40] | 0.19 [0.07] | 11.8 [2.34] | 2.46 |  |
| g3-mameppc | ppc | 0.58 [1.26] | 0.39 [3.67] | 0.21 [1.01] | 0.13 [0.41] | 0.07 [0.36] | 0.01 [0.06] | 0.68 [2.19] | 0.16 |  |
| g3-mameppc-mmu | ppc | 0.42 [1.24] | 0.36 [3.58] | 0.15 [0.96] | 0.11 [0.41] | 0.06 [0.36] | 0.01 [0.06] | 0.51 [2.14] | 0.14 |  |
| exec-761-68k | 68k | 0.31 | 0.46 | 3.93 | 22.2 | 0.43 | 3.32 | 4.80 | 1.00 |  |
| exec-761-68k-mmu | 68k | 0.31 | 0.45 | 3.89 | 22.3 | 0.43 | 3.29 | 4.77 | 1.00 |  |
| exec-761-ppc | ppc | 6.41 | 5.17 | 36.9 | 27.1 | 0.76 | 7.24 | 9.85 | 1.00 |  |
| exec-761-ppc-mmu | ppc | 5.57 | 4.75 | 36.4 | 25.9 | 0.75 | 7.21 | 9.67 | 1.00 |  |
| exec-86-jit | ppc | 6.42 | 5.16 | 37.0 | 32.3 | 1.05 | 7.30 | 9.90 | 1.00 |  |
| exec-86-jit-mmu | ppc | 5.55 | 4.95 | 36.3 | 32.8 | 1.05 | 7.26 | 9.98 | 1.00 |  |
| exec-86 | ppc | 0.63 | 0.39 | 16.5 | 29.4 | 0.53 | 5.41 | 5.00 | 1.00 |  |
| tab-761-68k | 68k | 0.05 | 0.08 | 0.92 | 6.91 | 0.05 | 0.84 | 0.76 | 1.00 |  |
| tab-761-ppc | ppc | 0.38 | 0.22 | 10.4 | 8.48 | 0.10 | 2.50 | 2.22 | 1.00 |  |
| tab-86 | ppc | 0.37 | 0.19 | 10.1 | 11.3 | 0.15 | 1.04 | 2.11 | 1.00 |  |

Median times, ms by the host's clock (the median over the sessions of each test's median of its runs). —: skipped or not in that build.

| Test | work | quadra-uae | quadra-mame68k | quadra-mame68k-mmu | g3-kpx | g3-kpx-jit | g3-mameppc-jit | g3-mameppc-jit-mmu | g3-mameppc | g3-mameppc-mmu | exec-761-68k | exec-761-68k-mmu | exec-761-ppc | exec-761-ppc-mmu | exec-86-jit | exec-86-jit-mmu | exec-86 | tab-761-68k | tab-761-ppc | tab-86 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dhry | 20000_loops | 37.0 | 124.6 | 119.9 | 24.5 | 3.42 | 5.74 | 6.85 | 107.9 | 150.3 | 115.5 | 118.5 | 10.1 | 10.3 | 10.0 | 10.4 | 61.3 | 810.9 | 122.5 | 123.6 |
| sieve | 8192x20 | 11.1 | 56.3 | 51.2 | 6.67 | 1.05 | 1.46 | 1.80 | 23.2 | 33.3 | 53.6 | 49.8 | 2.05 | 2.37 | 2.06 | 2.36 | 24.9 | 267.8 | 39.2 | 39.8 |
| sort | 20000_longs | 26.0 | 69.9 | 69.2 | 8.85 | 1.75 | 2.71 | 3.12 | 36.3 | 46.2 | 66.0 | 69.5 | 3.56 | 3.75 | 3.57 | 3.76 | 37.5 | 420.7 | 65.7 | 69.3 |
| crc32 | 1MB | 48.1 | 142.9 | 133.7 | 16.3 | 2.12 | 2.74 | 4.37 | 51.2 | 72.6 | 131.3 | 135.9 | 4.15 | 5.84 | 4.11 | 5.85 | 57.2 | 937.5 | 81.2 | 80.2 |
| whet_sane | 10_passes | 68.2 | 209.9 | 204.9 | — | — | — | — | — | — | 173.6 | 172.5 | — | — | — | — | — | 1592.4 | — | — |
| fft_sane | 1024x8 | 172.8 | 505.4 | 490.9 | — | — | — | — | — | — | 450.3 | 454.5 | — | — | — | — | — | 4057.9 | — | — |
| matmul_sane | 48x48 | 87.2 | 265.4 | 260.0 | — | — | — | — | — | — | 238.9 | 243.2 | — | — | — | — | — | 2223.3 | — | — |
| whet_881 | 10_passes | 9.71 | 24.7 | 23.9 | — | — | — | — | — | — | 19.8 | 20.3 | — | — | — | — | — | 118.5 | — | — |
| fft_881 | 1024x8 | 23.9 | 65.7 | 65.0 | — | — | — | — | — | — | 63.2 | 66.2 | — | — | — | — | — | 372.2 | — | — |
| matmul_881 | 48x48 | 19.3 | 38.0 | 37.4 | — | — | — | — | — | — | 37.0 | 37.8 | — | — | — | — | — | 207.2 | — | — |
| blockmove_1k | 32MB | 55.2 | 129.8 | 129.5 | 36.4 | 5.00 | 16.7 | 19.2 | 439.5 | 585.3 | 26.4 | 26.0 | 6.66 | 6.71 | 6.55 | 6.56 | 16.6 | 218.2 | 41.9 | 41.9 |
| blockmove_64k | 32MB | 42.2 | 86.5 | 88.2 | 26.3 | 3.19 | 14.0 | 15.9 | 357.6 | 489.8 | 1.02 | 1.00 | 0.58 | 0.58 | 0.59 | 0.58 | 0.76 | 3.27 | 1.09 | 1.19 |
| blockmove_1m | 32MB | 42.0 | 85.2 | 87.2 | 26.1 | 3.19 | 13.9 | 16.1 | 358.6 | 493.8 | 0.54 | 0.52 | 0.48 | 0.48 | 0.49 | 0.49 | 0.49 | 0.69 | 0.54 | 0.62 |
| memcpy_64k | 32MB | 61.6 | 148.8 | 140.2 | 19.0 | 2.62 | 10.8 | 12.3 | 278.3 | 356.4 | 131.5 | 140.6 | 0.58 | 0.59 | 0.58 | 0.59 | 0.75 | 917.4 | 1.07 | 1.33 |
| stride | 1MB/64x16 | 7.33 | 25.7 | 25.9 | 2.06 | 0.27 | 0.39 | 0.39 | 6.90 | 10.5 | 24.9 | 25.8 | 0.58 | 0.63 | 0.58 | 0.64 | 7.92 | 148.5 | 13.5 | 10.0 |
| copybits_1_to | 50x_window | 123.0 | 421.4 | 400.3 | 883.2 | 98.8 | 116.6 | 122.0 | 2505.4 | 2889.6 | 2.54 | 2.44 | 2.17 | 2.17 | 2.10 | 2.10 | 2.13 | 3.69 | 3.73 | 3.69 |
| copybits_1_from | 50x_window | 150.4 | 404.7 | 395.4 | 1071.9 | 104.0 | 191.4 | 214.9 | 3595.7 | 4791.5 | 1.69 | 1.62 | 1.44 | 1.95 | 1.40 | 1.20 | 1.24 | 3.23 | 3.10 | 2.38 |
| copybits_8_to | 50x_window | 24.0 | 40.3 | 42.1 | 812.9 | 86.6 | 133.6 | 145.7 | 3095.1 | 3691.6 | 0.70 | 0.62 | 0.55 | 0.55 | 0.48 | 0.48 | 0.51 | 1.76 | 1.81 | 1.41 |
| copybits_8_from | 50x_window | 31.4 | 68.5 | 66.1 | 1155.2 | 101.9 | 193.7 | 251.6 | 3919.4 | 5198.9 | 1.28 | 1.27 | 0.87 | 1.09 | 0.57 | 0.56 | 0.60 | 2.00 | 1.94 | 1.42 |
| copybits_32_to | 50x_window | 305.1 | 1175.9 | 1119.9 | 19.6 | 18.2 | 18.2 | 20.5 | 30.5 | 34.9 | 7.00 | 7.04 | 6.98 | 7.02 | 6.88 | 6.84 | 6.84 | 16.2 | 16.3 | 12.4 |
| copybits_32_from | 50x_window | 918.4 | 2280.3 | 2318.2 | 178.2 | 21.9 | 59.9 | 65.0 | 960.9 | 1305.0 | 217.0 | 217.2 | 218.2 | 217.8 | 256.5 | 255.9 | 256.6 | 711.1 | 720.8 | 576.6 |
| paintrect | 2000_64x48 | 30.1 | 91.6 | 91.9 | 27.3 | 5.26 | 8.28 | 8.62 | 140.7 | 171.2 | 6.55 | 6.70 | 4.71 | 4.75 | 2.05 | 2.02 | 3.49 | 43.2 | 31.1 | 8.48 |
| frameoval | 1000_80x60 | 68.7 | 197.2 | 194.1 | 116.0 | 14.8 | 35.6 | 37.9 | 802.3 | 974.3 | 4.87 | 4.90 | 3.70 | 3.73 | 2.84 | 2.79 | 3.50 | 25.5 | 19.0 | 17.4 |
| drawstring_bitmap | 1000x40ch | 96.4 | 165.6 | 159.0 | 162.2 | 65.8 | 78.9 | 83.2 | 554.0 | 650.7 | 4.78 | 4.83 | 3.13 | 3.13 | 2.29 | 2.27 | 2.86 | 23.9 | 13.9 | 11.3 |
| drawstring_tt | 1000x40ch | 125.6 | 207.1 | 201.5 | 179.9 | 81.2 | 95.0 | 99.4 | 620.9 | 688.1 | 5.28 | 5.81 | 3.52 | 3.57 | 2.66 | 2.79 | 3.17 | 24.5 | 14.5 | 8.18 |
| rgn_union | 200x60_ovals | 186.8 | 594.2 | 601.7 | 188.8 | 27.6 | 43.7 | 51.4 | 811.2 | 993.2 | 5.07 | 5.08 | 4.79 | 4.86 | 4.96 | 4.97 | 5.07 | 12.4 | 7.67 | 7.72 |
| rgn_inset | 100x60_ovals | 417.0 | 1180.9 | 1222.7 | 691.9 | 110.4 | 168.8 | 200.9 | 2639.4 | 3082.9 | 22.8 | 22.9 | 23.0 | 22.9 | 23.3 | 23.0 | 23.2 | 166.3 | 126.4 | 129.3 |
| call | 100000 | 4.61 | 17.8 | 17.9 | 2.98 | 0.32 | 0.91 | 0.97 | 24.4 | 29.8 | 17.2 | 17.5 | 1.12 | 1.28 | 1.15 | 1.29 | 23.1 | 99.7 | 40.4 | 41.4 |
| tickcount | 100000 | 10.1 | 37.4 | 36.5 | 5.15 | 0.57 | 2.01 | 2.12 | 46.2 | 56.7 | 36.8 | 37.0 | 21.5 | 21.8 | 21.5 | 21.6 | 46.5 | 340.4 | 161.7 | 164.0 |
| getsetport | 100000 | 36.0 | 119.6 | 118.0 | 60.7 | 8.42 | 17.3 | 18.8 | 463.1 | 565.8 | 97.1 | 99.2 | 96.2 | 97.0 | 37.6 | 37.0 | 84.7 | 804.2 | 704.9 | 211.2 |
| gestalt | 100000 | 128.0 | 406.4 | 396.2 | 850.9 | 165.5 | 338.3 | 373.7 | 6358.2 | 7258.8 | 159.8 | 161.2 | 50.5 | 51.4 | 50.5 | 49.6 | 78.5 | 1407.3 | 419.5 | 422.3 |
| file_write_4k_boot | 4MB | 17.6 | 54.6 | 54.2 | 408.1 | 69.4 | 104.4 | 122.7 | 1970.1 | 2327.1 | 1.51 | 1.54 | 0.71 | 0.64 | 0.65 | 0.67 | 0.98 | 6.24 | 1.80 | 7.86 |
| file_write_64k_boot | 4MB | 0.88 | 2.61 | 2.58 | 46.0 | 6.95 | 10.3 | 12.2 | 257.3 | 304.1 | 0.20 | 0.18 | 0.12 | 0.12 | 0.11 | 0.11 | 0.14 | 0.50 | 0.20 | 6.07 |
| file_read_4k_boot | 4MB | 6.20 | 21.5 | 22.1 | 310.6 | 58.2 | 81.3 | 94.6 | 1392.5 | 1647.2 | 1.31 | 1.33 | 0.61 | 0.58 | 0.56 | 0.58 | 0.94 | 6.05 | 1.54 | 1.96 |
| file_read_64k_boot | 4MB | 0.64 | 1.79 | 1.83 | 11.4 | 2.47 | 3.31 | 3.72 | 49.0 | 57.6 | 0.17 | 0.17 | 0.10 | 0.11 | 0.10 | 0.11 | 0.14 | 0.51 | 0.21 | 0.32 |
| file_openclose_boot | 1000 | 48.9 | 126.3 | 126.6 | 250.3 | 30.5 | 55.1 | 63.9 | 1231.1 | 1439.1 | 11.7 | 12.0 | 2.65 | 2.66 | 2.65 | 2.71 | 3.23 | 14.8 | 6.01 | 6.35 |
| file_create_boot | 200 | 63.2 | 185.8 | 184.5 | 644.7 | 122.9 | 177.9 | 202.8 | 2974.1 | 3546.5 | 32.6 | 32.7 | 32.2 | 32.6 | 36.2 | 36.6 | 35.8 | 56.6 | 43.4 | 50.1 |
| file_write_4k_disk | 4MB | 8.96 | 31.5 | 31.1 | 273.5 | 38.6 | 64.0 | 71.1 | 1381.2 | 1661.0 | 1.43 | 1.47 | 0.69 | 0.69 | 0.68 | 0.67 | 0.99 | 13.7 | 5.48 | 86.4 |
| file_write_64k_disk | 4MB | 0.88 | 2.41 | 2.41 | 39.3 | 5.14 | 8.37 | 9.62 | 225.7 | 278.2 | 0.17 | 0.18 | 0.10 | 0.11 | 0.11 | 0.11 | 0.13 | 3.94 | 3.75 | 13.7 |
| file_read_4k_disk | 4MB | 6.37 | 21.7 | 21.9 | 230.5 | 33.8 | 51.5 | 58.0 | 1033.1 | 1218.5 | 1.28 | 1.34 | 0.53 | 0.56 | 0.55 | 0.55 | 0.96 | 11.2 | 2.71 | 3.10 |
| file_read_64k_disk | 4MB | 0.69 | 1.74 | 1.73 | 5.74 | 1.01 | 1.46 | 1.67 | 25.0 | 28.9 | 0.16 | 0.16 | 0.09 | 0.10 | 0.10 | 0.10 | 0.12 | 1.38 | 0.99 | 1.14 |
| file_openclose_disk | 1000 | 44.7 | 122.4 | 121.4 | 257.8 | 30.8 | 54.6 | 63.6 | 1243.5 | 1441.1 | 9.94 | 10.1 | 0.75 | 0.76 | 0.76 | 0.75 | 1.35 | 8.57 | 3.04 | 4.05 |
| file_create_disk | 200 | 48.3 | 141.2 | 139.7 | 531.7 | 105.7 | 151.6 | 170.9 | 2562.4 | 2963.9 | 606.6 | 613.0 | 607.5 | 605.2 | 527.3 | 524.6 | 524.5 | 1945.5 | 44.2 | 57.2 |
| newhandle | 10000 | 19.5 | 71.8 | 70.1 | 12.0 | 1.89 | 3.34 | 4.14 | 84.9 | 112.5 | 15.6 | 15.8 | 4.93 | 5.08 | 4.93 | 4.89 | 13.5 | 105.8 | 24.8 | 26.1 |
| lockmove | 1000 | 593.0 | 1701.3 | 1686.4 | 27.3 | 4.05 | 5.28 | 7.00 | 84.7 | 118.0 | 12.2 | 12.1 | 13.7 | 13.7 | 13.6 | 13.5 | 14.3 | 54.5 | 38.7 | 40.1 |
| compact | 5x2000 | 22.7 | 80.1 | 79.2 | 15.1 | 2.35 | 4.47 | 5.53 | 117.2 | 152.1 | 12.5 | 12.7 | 4.09 | 4.18 | 4.05 | 4.00 | 10.9 | 105.5 | 24.9 | 26.7 |
| whet | 10_passes | — | — | — | 1.99 | 0.79 | 2.92 | 3.23 | 44.0 | 46.2 | — | — | 2.67 | 2.73 | 2.68 | 2.74 | 27.6 | — | 63.9 | 61.5 |
| fft | 1024x8 | — | — | — | 3.68 | 0.48 | 3.20 | 3.49 | 72.2 | 76.6 | — | — | 5.62 | 6.24 | 5.63 | 5.97 | 89.5 | — | 148.3 | 173.8 |
| matmul | 48x48 | — | — | — | 1.23 | 0.37 | 1.26 | 1.32 | 24.6 | 27.1 | — | — | 2.16 | 2.46 | 2.16 | 2.26 | 31.1 | — | 47.2 | 62.3 |
| mixedmode | 10000 | — | — | — | 14.5 | 1.86 | 13.8 | 14.8 | 180.8 | 226.9 | — | — | 3.11 | 3.06 | 3.06 | 3.11 | 5.58 | — | 24.9 | 25.3 |

What it says:

- **The guest clocks.** By the host's clock, the G3 on mame-ppc with its
  JIT is 16 times as fast on integer code as without (9.5 against 0.58);
  by its own `Microseconds` the two are alike (1.33, 1.26): its timebase
  counts cycles, so guest-timed benchmarks (Speedometer) can't see the
  JIT. On Kheperix with `--jit`, `Microseconds` almost stops within a
  test (dhry: 2 µs against 3.4 ms), the saturated Speedometer scores;
  without the JIT it runs about 1.6 times fast. uae, mame-68k and
  Executor keep host time (clock 1.00), the tab too.
- **68k code**: Executor runs it at mame-68k's speed (Integer 0.31, the
  Quadra on mame-68k 0.29), a third of uae's interpreter; mame-68k is 3–4
  times slower than uae across the board on the ROM machine.
- **PowerPC code**: Executor on mame-ppc's native back-end (6.4) is about
  two thirds of the G3 ROM machine on the same core (9.5; not looked
  into) and 20 times its own 68k; without the back-end (`exec-86`) 0.63.
- **The Toolbox in C++** is where Executor is far ahead: QuickDraw 22–32
  times uae's Quadra (`CopyBits`, regions, text: native code), the Memory
  Manager 5–10, the File Manager 3–7. Not everywhere: `file_create_disk`
  (200 creates and deletes on a disk image) takes 600 ms on Executor
  against 48 ms on uae's Quadra, and `CopyBits` from a 32-bit GWorld to an
  8-bit screen 217 ms against Apple's 178 ms on the G3.
- **The G3's Files column** (0.05–0.34) is Mac OS 8.6's File Manager on
  that machine, 3–20 times slower than the Quadra's 7.6.1 for the same
  calls; its
  `Gestalt` is slow too (850 ms per 100,000 on Kheperix).
- **The tab** runs 68k code at a sixth of native Executor (Integer 0.05
  against 0.31) and PowerPC code at a seventeenth (0.38 against 6.4:
  mame-ppc's UML interpreter in wasm, no back-end), about native
  `exec-86`'s (0.63) and the G3 on the interpreter's (0.58). Its Toolbox
  is still well ahead of the Quadra: QuickDraw 7–11 times, memory moves
  10 times (PowerPC), files and heap about the Quadra's to 2.5 times.
- **Mixed Mode**: 10,000 switches to a 68k `rts` cost 14 ms on the G3 ROM
  machine (JIT), 3 ms on Executor.

### The MMU's cost

Baselines for the cores with the MMU on, against the same rows without
(the `-mmu` rows above):

- **mame-68k with its PMMU** (`Config::mmu`; `--cpu-mmu` on the ROM
  machine, `--executor-debug mmu68k` on Executor): no measurable cost.
  Executor sets up no tables (translation stays off; Integer 0.31 both,
  every test within noise). On the Quadra the ROM finds a PMMU and is
  free to set up its tables; whether it turned translation on in these
  runs wasn't checked, and the rows are alike too (0.29 and 0.31).
- **mame-ppc translating** (`Config::mmu_identity`, new: data translation
  on whatever MSR[DR] says, every page mapped to itself; `--cpu-mmu` on
  the G3, `--executor-debug mmuppc` on Executor). Identity BATs as such
  aren't enough on the ROM machine: the G3's map spans four 256 MB blocks
  (RAM at 0, ROM and kernel data at $5xxxxxxx, the nanokernel's at
  $6xxxxxxx, the screen at $A0000000), the four BATs a 750 has, with
  nothing left over and no page table behind them, and MSR[IR/DR], which
  every exception entry clears, would have to be kept on behind the
  guest's back.
  So the knob maps every page to itself without a table, as identity
  BATs over all of it would. The recompiler's data accesses then
  each take a TLB lookup before fastram. The cost: integer code 13% on
  Executor (6.41 to 5.57), 22% on the G3 with the JIT (9.53 to 7.44), 28%
  without it (0.58 to 0.42); memory-bound loops the most (crc32 2.7 to
  4.4 ms on the G3, +60%), QuickDraw, files and Toolbox calls 0–15%. Instruction fetch isn't translated per instruction
  (blocks are compiled for a physical PC), so this is the data side's
  cost only.

### Caveats

- The box is shared (the load average is in results.jsonl); medians of 3
  sessions of 5 runs are steady to a few percent on Executor and uae,
  10–20% on the slowest rows.
- Sub-millisecond tests (Executor's `BlockMove`, its file reads) are at
  `Microseconds`' resolution (1 µs natively, 5 µs in the tab).
- The FP column compares PowerPC's tests with the Quadra's 68881 code
  (SANE's are in the times: 7–9 times slower than 68881 code on every
  68k row).
- The tab boots the site's cut-down Systems with 128 MB, native runs
  Apple's full images with 64 MB, as in the Speedometer table.
- **Executor in the tab on 8.6 loses the session** when an application
  calls `WaitNextEvent` for two seconds on end (the clock check did, as
  first written): the application goes, the Finder comes back, and what
  it wrote to its disk is gone (an empty file, though flushed with
  `FlushVol` after each line). Not in the tab on 7.6.1, not natively.
  The same happens to the whole set with 5 runs of each test (each third
  of the set with 5 runs, and the whole set with 1, come through). Not
  looked into here; the clock check now waits busily, the tab rows are
  from that build (f47e5934; the tests are the native rows' build's),
  and `tab-86` is 5 sessions of 1 run each.
