# Development frictions

Things that cost time in agent sessions, for fixing. Newest last; strike
through or delete when fixed.

1. ~~`tools/mpw/mpw build` waited out its whole timeout (600 s) when Executor
   crashed under it.~~ Fixed 2026-10-26: `run()` polls `alive()` while the
   request is out and fails at once.
2. Every Executor run ends with "FATAL CRASH DETECTED" / `terminate called
   without an active exception` at power-off (STRAGGLERS 55, a joinable
   `std::thread` in `main`'s lambda #8). Passing runs look like crashes; each
   log has to be read past it.
3. Release builds' crash backtraces have few line numbers ("rebuild with
   Debug"). A RelWithDebInfo default (`-O2 -g`) would make most crashes one
   look instead of a debugging round.
4. `tests/test_guest_suite.sh` removes its ExtFS folder, results file and all;
   the results survive only inside the log, where long result lines wrap.
   An option to keep the folder (or the results file copied next to the
   log) would help.
5. No index of what is on the disk images. "Which applications import
   StdCLib / call X" meant a raw scan of every image for PEF containers
   (`~/storage/tmp/ppclibs/survey.py`, minutes per pass). A cached per-image
   list of applications with their PEF imports and exports (tools/macdecode)
   would answer these at once.
6. MPW's `CIncludes` are MacRoman with CR line ends: grep treats them as
   binary (`grep -a`, `tr '\r' '\n'` each time). A UTF-8/LF copy of the
   interfaces (declarations only, clean-room safe) beside the toolchain would
   help.
7. Background jobs (`run_in_background`) die when a session ends and leave no
   output if they write only at the end; long surveys should write as they
   go.
