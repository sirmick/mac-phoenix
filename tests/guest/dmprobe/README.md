# Display Manager probe

`dmprobe.c` measures what a Mac's Display Manager does when the screen's
resolution changes (docs/executor/DisplayModes.md): it registers notify
procs and Apple Event handlers, lists the main display's modes, switches to
640x480 and back with `DMSetDisplayMode` (alone, with its procs removed, and
inside `DMBeginConfigureDisplays`/`DMEndConfigureDisplays`) and writes what
it got to `Host:dmprobe.txt`.

Build with Retro68 (Universal Interfaces):
`cmake -S . -B b68k -DCMAKE_TOOLCHAIN_FILE=~/opt/retro68/build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake`.
`DMProbe.bin` is that build. Run it on a ROM machine started at a larger
size with `--bridge`: `tests/guest/install_macbinary.py DMProbe.bin BRIDGEDIR
DMProbe`, then `/api/launch {"path":"Host:DMProbe"}`.

`ref/`: measured 2026-11-03 on the Quadra 650 ROM machine (7.6.1, 1024x768)
and the G3 (8.6, 3840x2160).
