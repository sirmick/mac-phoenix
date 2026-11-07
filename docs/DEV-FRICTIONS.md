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
   Debug"). CMake's default is already RelWithDebInfo (`-O2 -g`), but the
   main checkout's `build/` and `tools/ci/integration.sh` configure
   `-DCMAKE_BUILD_TYPE=Release`; RelWithDebInfo there would make most
   crashes one look instead of a debugging round.
4. ~~`tests/test_guest_suite.sh` removed its ExtFS folder, results file and
   all.~~ Fixed 2026-10-30: `--keep-dir DIR` copies what the guest wrote on
   Host: to DIR, whatever the outcome.
5. ~~No index of what is on the disk images ("which applications import
   StdCLib").~~ Fixed 2026-10-26: `tools/macdecode/libraries.py --scan`
   caches every image's PEF imports per library and symbol
   (`<storage>/macdecode/libraries.json`).
6. MPW's `CIncludes` are MacRoman with CR line ends: grep treats them as
   binary (`grep -a`, `tr '\r' '\n'` each time). A UTF-8/LF copy of the
   interfaces (declarations only, clean-room safe) beside the toolchain would
   help.
7. Background jobs (`run_in_background`) die when a session ends and leave no
   output if they write only at the end; long surveys should write as they
   go.
