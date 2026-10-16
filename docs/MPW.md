# MPW on Executor

`tools/mpw/mpw` runs Apple's Macintosh Programmer's Workshop (MPW 3.4.2 from
E.T.O. #22: SC, SCpp, Link, Rez, Asm, Make and the 68k libraries, Open
Transport glue included) inside a headless Executor session, so 68k Mac
code builds from the host's command line. Guest test programs (MPW tools
that drive a Toolbox subsystem and print what they find) are built this way.

```bash
tools/mpw/mpw setup                  # once: the MPW disk, from data.img's E.T.O. #22
tools/mpw/mpw build tests/guest/ot   # mirror, build.mpw, products as MacBinary
tools/mpw/mpw run 'SC -help'         # any MPW command; exit code = {Status}
tools/mpw/mpw status | stop
```

## How it works

* **The session.** `mpw start` (or the first command that needs it) boots
  Executor on a copy of the 7.5.5 System (`mpw-7.5.5.img`, so the original
  image is never mounted read-write) with a scratch copy of the MPW disk
  (`~/storage/images/mpw-eto22.img`, volume "MPW") and a private Host volume
  (`~/.cache/mac-phoenix/mpw/host`). It is ready when ToolServer answers,
  about 7 s. It stays up until `mpw stop`.
* **Commands.** ToolServer is MPW's headless command server. A command is
  written to a script file in `Host:.mpw:`; the wrapper sent to ToolServer
  as a 'misc'/'dosc' Apple Event (BridgeAgent, `POST /api/exec`) runs that
  script with its output to a file and writes `{Status}` after it. The files
  are the result, so a build longer than the Apple Event's one-minute
  timeout still completes. Commands are serialized with a lock.
* **Projects.** `mpw build DIR` mirrors DIR into `Host:<name>:` (text files
  UTF-8 → MacRoman and LF → CR, by extension; others as they are; unchanged
  files untouched so Make sees the right dates), runs `build.mpw` there with
  `{Exit}` 1, or `Make -f MakeFile` and its output. Products named on a
  `# products: NAME ...` line come back as MacBinary in `DIR/build/` (or
  `-o OUTDIR`).
* **Messages.** MPW's `File "x.c"; line 12 #Error: ...` lines are repeated as
  `DIR/x.c:12: Error: ...` for editors.
* **Files in the Host volume.** A plain host file is a TEXT file; `NAME.bin`
  in MacBinary is the Mac file it holds (both forks, its type and creator),
  so a built tool copied back in runs as `NAME.bin`. A file the guest gives
  a resource fork or a type is kept as `NAME` plus `%NAME` (AppleDouble).

## Things to know

* **`-model far`** for anything linked with Apple's OT glue (32-bit
  references), on both SC and Link.
* **Library order:** `Stubs.o` last, if at all (`IntEnv.o` covers it).
* **One session per state directory.** Tests run their own (`MPW_STATE`,
  `MPW_PORT`, `MPW_SYSTEM`); see `tests/test_mpw_toolchain.sh`.
* `emulator.log` in the state directory has Executor's output; a CPU crash
  ends the command with an error rather than a timeout.

## Executor fixes it needed

* HFS: a second access path to a fork that is already open shares its
  length and extents (`hfs/hfsFile.cpp`). MPW command substitution
  (`` `Exists ...` ``) writes `MPW.Pipe` through one path and reads it back
  through another; without this, Startup set no `{Commands}`.
* Host volume: closing or flushing a written path stamps the modification
  date (`file/localvolume/localvolume.cpp`). Link zeroes its output's date
  while it works ("assumed to be bad") and relies on the close.
* Host volume: listing a folder after the host deleted a file in it walked
  a vector it was erasing from and aborted the emulator
  (`file/localvolume/simplecnidmapper.cpp`).

## Test

`ctest -R mpw_toolchain_executor`: startup, run, build, the tool, a compile
error, a rebuild after host-side deletes. Skips without the MPW disk.
