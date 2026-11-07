# Full-heap probe

`fullheap.c` fills the application heap with handles until not even 8
bytes are left, then calls Toolbox routines that allocate. For each one it
writes a line to `Host:fullheap.txt`: the result (nil, or which heap it
came from), MemErr, QDError, and whether location 0 changed. With
`Host:fullheap.only` naming one step, only that step runs: a step that
stops the machine then leaves the others measurable.

`FullHeap68k.bin` is the Retro68 68k build (`cmake -S . -B b
-DCMAKE_TOOLCHAIN_FILE=.../retro68.toolchain.cmake`). To run it:
`tests/test_guest_suite.sh --app FullHeap68k.bin --results fullheap.txt
[--host-file DIR/fullheap.only]`, with `MACTEST_SNAPSHOT=1` for a screen.

The 7.6.1 Quadra (2026-11-07), one step per session:

- **nil, MemErr -108:** NewHandle, NewPtr, NewRgn, NewPalette(256),
  NewGWorld (QDErr -108), OpenPicture.
- **Come back:** FrameOval, PaintOval, FrameRoundRect, FillCRect,
  DrawString, CopyBits.
- **System error 25 (dsMemFullErr), from the ROM's `NewHandleClear` +
  `_Debugger`:** NewPixPat, TENew, NewDialog, InvertRect, FillRect (gray),
  PenPixPat+PaintRect, ScrollRect, ClipRect+EraseRect, FrameRgn, UnionRgn,
  XorRgn.

InsetRgn is left out: it hangs the Quadra when MaxBlock is under its
buffer.
