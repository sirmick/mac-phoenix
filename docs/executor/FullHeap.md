# A full application heap

When an application's heap is full, Executor does not do exactly what a
Mac does. This page says how it differs and why, and gives the Mac's
measured behaviour. The code is `executor/core/romlib/mman/fullheap.cpp`.
The entry-point marker is `traps::ServedTrap` in `base/traps.h`.

## What the Mac does

Measured on the Quadra 650 with 7.6.1 (2026-11-07). The probe
`tests/guest/fullheap` fills the application heap with handles until not
even 8 bytes are left, then makes one Toolbox call per session:

| Answer | Calls |
|---|---|
| nil, MemErr -108 | NewHandle, NewPtr, NewRgn, NewPalette(256), NewGWorld (QDError -108), OpenPicture |
| work (no allocation that fails) | FrameOval, PaintOval, FrameRoundRect, FillCRect, DrawString, CopyBits |
| system error 25 (dsMemFullErr), from the ROM's `NewHandleClear` + `_Debugger` | NewPixPat, TENew, NewDialog, InvertRect, FillRect (gray), PenPixPat+PaintRect, ScrollRect, ClipRect+EraseRect, FrameRgn, UnionRgn, XorRgn |

InsetRgn hangs the Quadra when MaxBlock is smaller than its buffer, so the
probe leaves it out. The screens are in `~/storage/tmp/gestalt3/fhs-q761`.
The G3 with 8.6 is not measured: the probe never ran in the sessions'
time.

The allocator calls answer nil. The ROM's own allocations made inside
other calls never answer nil: the machine stops.

## Why Executor differs

Executor is much faster than a Quadra. A benchmark that leaves memory
behind on each pass makes many more passes, so it fills a heap that a
Quadra never fills in the same test. Speedometer 4.0's colour test is the
case found:

- Each test window is GetNewCWindow(507). Its automatic palette (pltt 507:
  256 entries, 4 KB, private word $2004) stays in the heap after
  DisposeWindow. The Quadra keeps it too: the gtest
  `Windows.AutomaticPaletteAfterDisposal` checks every variant of this.
- The Quadra runs GetNewPalette 24 times over the 5 depths (6, 6, 5, 3,
  4; `--trace-atraps`). Its heap stays at about 930 KB free.
- Executor runs it about 72 times per depth. Its heap goes from 957 KB
  free to 580, 256 and then 9 KB: per depth, 83 palettes of 4128 bytes
  and 82 pmSeeds handles of 16 bytes.
- Executor's C++ then wrote through a nil handle at a random place:
  TENew from GetNewDialog, `xdata_for_pattern` from InvertRect, and
  before 4f2b3304 XorRgn from StdOval.

A Quadra would give system error 25 there. A run that a Quadra survives
would then end because Executor is fast, so Executor instead lends the
Toolbox memory from the System heap.

## The rule

The rule depends on the routine being served, not on the call site.

- Every guest entry into Executor sets `traps::served_entry` for the length
  of the call. A guest entry is a 68k trap, a dispatcher's selector, or a
  PowerPC or CFM-68K library call. A `ServedTrap` guard saves and restores
  it, next to `checkBreak68K`/`checkBreakPPC` in `traps.impl.h`.
- Host C++ calling a Toolbox routine sets nothing. An allocation the
  Toolbox makes while serving GetNewDialog therefore belongs to
  GetNewDialog.
- Guest code that Executor calls makes its own guest calls, and each
  marks itself. A WDEF's own NewHandle is the guest's NewHandle.
- Each process's coroutine keeps its own saved values on its own stack.
  After a process switch inside WaitNextEvent, each process returns to its
  own value.

`_NewHandle_flags` and `_NewPtr_flags` can fail outside the System heap
(the application's heap or any zone of its own). They then ask
`ROMlib_full_heap_decide`:

1. **The guest's own allocator calls keep nil and MemErr -108.** This is
   `traps::guest_allocators`:
   - from the probe: NewHandle, NewPtr, NewRgn, NewPalette, NewGWorld and
     OpenPicture;
   - by Inside Macintosh, the rest of the Memory Manager's allocators
     (NewEmptyHandle, ReallocateHandle, SetHandleSize, SetPtrSize,
     MoreMasters, TempNewHandle, HandToHand, PtrToHand, PtrToXHand,
     HandAndHand, PtrAndHand), UpdateGWorld, OpenCPicture, and the
     Resource Manager's loaders (GetResource, Get1Resource, the Named and
     Ind forms, LoadResource).

   The gtest `FullHeapNames` checks that each name is an entry point.
2. **Any other routine being served** retries in the System heap. The
   System heap grows into its reserve. Two cases keep the Memory
   Manager's nil:
   - No routine served at all: Executor's own code, such as its launch,
     or a gtest standing in for the guest. Regions built there
     (`util/handle_vector.h`) still retry in the System heap themselves,
     as they always did.
   - A request larger than the System heap and its reserve could ever
     give. That is a size Executor's code tries and copes without (a
     buffer it then makes smaller, a GWorld too big for the machine), not
     the heap running out.
3. **The floor.** A fallback must leave at least `FULL_HEAP_SYSTEM_FLOOR`
   (256 KB) free in the System heap and its growth reserve together.
   Otherwise it is system error 25: the message
   `[full heap] System error 25 (dsMemFullErr): TRAP needed N bytes ...`,
   LM(DSErrCode) = 25, the host's fatal hook (the snapshot), then exit
   status 70. The Mac's error 25 ends the session too, and it needs a
   restart. Executor's SysError is the splash screen's alert: it waits for
   a click and then returns. The rule ends the session itself so that
   nothing ever gets a nil.
4. **The guest's reserve, once the heap has run out.** After the
   application heap has needed a fallback, a Toolbox allocation that would
   leave it with less than a sixteenth of its size free (at most 64 KB)
   goes to the System heap at once, if the System heap can take it above
   its floor. If the System heap can't, the allocation is made in the
   application heap after all. InitZone clears the state, so a new
   partition at the same address starts afresh. Without the reserve, the
   Toolbox filled Speedometer's heap to the last bytes before falling back.
   Speedometer's own NewRgn then got nil, which is correct but never
   happens on a Quadra. Speedometer passed the nil to OpenRgn/CloseRgn, and
   that crashed the fifth depth.

   The reserve applies only after the heap has run out. A heap that is
   merely low is common, and programs don't expect the Toolbox's handles
   in the System heap. With the reserve in force from the start,
   AppleScript's initialisation in MacPerl failed
   (`macperl_smoke_executor`: "couldn't initialize AppleScript"). Until a
   heap runs out, Executor behaves as the Mac does.
5. **Zones keep their own master pointers.** MoreMasters' block is never
   taken from the System heap (`FullHeapInZone`). An application zone with
   master pointers in the System heap gives its handles negative offsets
   to their zone, which corrupted the heap; DrawPicture's allocations did
   this in Speedometer. When a zone's master pointers run out and it can't
   make more, the handle comes from the System heap whole, master pointer
   and all.
6. **Log and count.** The first fallback for each routine in each process
   (keyed by its ApplZone) logs
   `[full heap] TRAP: system heap fallback (N so far; K left above the floor)`.
   `ROMlib_full_heap_fallbacks()` counts them all.

The floor is there for everyone else who uses the System heap: other
processes' resource loads (a System or Finder resource map, fonts,
'PACK's), file control blocks and the Process Manager's launches.
Executor's System heap is the System's size plus a reserve of up to 4 MB
that it grows into (MEMORY_MAP.md). 256 KB covers what one of those takes
at a time, and it is small against what a fallback run takes. Speedometer's
five depths took up to about 1.7 MB if every palette moved.

`util/handle_vector.h` (QuickDraw's regions) used to retry in the System
heap itself, with no floor, and for the guest's NewRgn too. When a routine
is served, it now leaves the retry to this rule.

The guest's OpenPicture used to write through its own nil handle. It now
answers nil and MemErr -108 and records nothing, as on the Quadra
(`quickdraw/qPicture.cpp`).

## Executor with the rule (7.6.1, 2026-11-07)

The probe ran every step in one session and wrote `done`
(`~/storage/tmp/fullheap/probe-executor-7.6.1.txt`, with its log).

| Answer | Calls |
|---|---|
| nil, MemErr -108, as on the Quadra | NewHandle, NewPtr, NewRgn, NewPalette(256), NewGWorld (-108), OpenPicture |
| works, memory from the System heap (the Quadra: system error 25) | NewPixPat, NewDialog, TENew, InvertRect, FillRect (gray), PenPixPat+PaintRect, ScrollRect, ClipRect+EraseRect, FrameRgn |
| works, QDError -108 (the guest's destination region can't grow in its heap) | UnionRgn, XorRgn |
| works | FrameOval, PaintOval, FrameRoundRect, FillCRect, DrawString, CopyBits |

That session made 43 fallbacks. Each routine's first one was logged:
NewPixPat, FrameOval, PaintOval, FrameRoundRect, InvertRect, FillRect,
FillCRect, ScrollRect, DrawString, EraseRect, FrameRgn, NewDialog,
NewWindow, wdef0 and TENew. The System heap had about 4970 K left above
the floor.

## Speedometer (7.6.1, 2026-11-07)

`~/storage/tmp/gestalt3/spcolor.py` runs Speedometer 4.0's colour test
(Color QuickDraw at the 5 depths) in one Executor session. All 5 depths
ran and wrote their reports (runs spck6, spck7 and spfinal in
`~/storage/tmp/fullheap`):

- Before this rule, Executor crashed after 2 or 3 depths (Speed.md).
- With the reserve at a flat 64 KB from the start: 327,347 fallbacks, and
  the System heap ended 2,331 K above the floor.
- With the reserve proportional from the start: 1,157,354 fallbacks,
  1,902 K above the floor (about 2.1 MB of the System heap in one session).
- The rule as committed (the reserve only after the heap has run out,
  run spfinal): 512,066 fallbacks. The System heap ended 4,741 K above
  the floor, about 140 K below where the session started.

Most of the fallbacks are short-lived: regions and pattern buffers made
and freed within one drawing call. A temporary per-trap heap walk found
the MoreMasters corruption. At boot it also reported one block in a
200 KB zone whose master pointer slot holds the System heap's start. That
report did not change for the rest of the run, the zone's free count
added up, and nothing failed, so it is probably the walk misreading a
non-standard block. It is not explained.

## Costs

- The System heap does not shrink back. Blocks that the fallback made stay
  there after the application quits, unless the application disposes of
  them first. A Mac would have freed them with the partition. The floor
  limits the damage: the next process still has 256 KB. Speedometer's
  colour test kept about 140 K there (spfinal). Freeing a process's
  fallback blocks when it quits would need the blocks marked as the
  process's; not done.
- Some guest calls work where the Quadra stops: NewPixPat, TENew,
  NewDialog, and the region and pattern calls. No application relies on
  system error 25.

## Checks

- gtest `executor.FullHeap.*` (`executor/tests/fullheap.cpp`), in a zone
  filled in the test:
  - guest 68k NewHandle, NewPtr and NewRgn answer nil and -108 with no
    fallback;
  - the guest's `_NewString` gets its handle from the System heap, counted
    once;
  - a region made while serving FrameOval comes from the System heap, and
    one made while serving NewRgn is nil;
  - the floor's three answers, and nil when no routine is being served;
  - past the floor: exit 70 and "System error 25";
  - MoreMasters keeps a zone's master pointers in the zone;
  - `FullHeapReserve`: with the application heap inside its reserve, a
    Toolbox allocation comes from the System heap and the guest's own
    from the application heap.
- The probe on Executor (7.6.1).
- Speedometer's colour test, all 5 depths in one session (7.6.1).
