# MPW on Executor

`tools/mpw/mpw` runs Apple's Macintosh Programmer's Workshop (MPW 3.4.2 from
E.T.O. #22: SC, SCpp, Link, Rez, Asm, Make and the 68k libraries, Open
Transport glue included) inside a headless Executor session, so 68k Mac
code builds from the host's command line. Guest test programs (MPW tools
that drive a Toolbox subsystem and print what they find) are built this way.

```bash
tools/mpw/mpw setup                  # once: the MPW disk, from data.img's E.T.O. #22
tools/mpw/mpw build tests/guest/ot   # mirror, build.mpw, products as MacBinary
tools/mpw/mpw run 'Version'          # any MPW command; exit code = {Status}
tools/mpw/mpw status | stop

# CodeWarrior Pro 4's MPW instead (public media): see "CodeWarrior" below
tools/mpw/mpw --toolchain cw setup
tools/mpw/mpw --toolchain cw build tests/guest/suite -o /tmp/cw
```

## How it works

* **The session.** `mpw start` (or the first command that needs it) boots
  Executor on a copy of the 7.5.5 System (`mpw-7.5.5.img`, so the original
  image is never mounted read-write) with a scratch copy of the MPW disk
  (`~/storage/images/mpw-eto22.img`, volume "MPW") and a private Host volume
  (`~/.cache/mac-phoenix/mpw/host`). It is ready when ToolServer answers,
  about 7 s. It stays up until `mpw stop`.
* **Commands.** ToolServer is MPW's headless command server. A command is
  written to a script file in `Host:.mpw:`; a wrapper sent to ToolServer as
  a 'misc'/'dosc' Apple Event (BridgeAgent, `POST /api/exec`) runs it as a
  sub-script (its `Exit` ends only itself) with its output to a file, then
  echoes `{Status}`. Through the bridge trap (docs/CommandBridge.md) that
  is one request, answered when the script is done, however long it takes.
  Commands are serialized with a lock.
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
* **MPW C swaps `'\r'` and `'\n'`** (`'\n'` is 13): compare key codes and
  bytes from elsewhere by number.

## Fat applications

MPW 3.4.2's PowerPC compiler and linker (`MrC`, `PPCLink`) are fat tools,
so they run in the 68k session too, and one build makes an application
with both kinds of code (`tests/guest/suite/build.mpw`, MacPhoenixSuite):

1. `SC` + `Link -t APPL` (`MacRuntime.o IntEnv.o StdCLib.o Interface.o`)
   make the 68k application, `NAME.68k`.
2. `MrC` + `PPCLink` (`InterfaceLib StdCLib` from `{SharedLibraries}`,
   `StdCRuntime.o PPCCRuntime.o`) write the PowerPC fragment into the data
   fork of `NAME`.
3. `Rez -a` adds to `NAME`: `include "NAME.68k";` (its CODE and the rest),
   a `'cfrg' (0)` entry (`kPowerPC, kFullLib, ..., kIsApp, kOnDiskFlat,
   kZeroOffset, kWholeFork`; this `CodeFragmentTypes.r` wants the entry
   without `regularEntry`) and a `SIZE`.

A 68k Mac runs the CODE resources, a Power Mac the fragment. Both need
`QDGlobals qd;` defined by the application.

## CodeWarrior

`--toolchain cw` (or `MPW_TOOLCHAIN=cw`) runs CodeWarrior Pro 4's MPW:
ToolServer 3.4.1, Metrowerks' compilers and linkers as MPW tools (`MWC68K`,
`MWCPPC`, `MWLink68K`, `MWLinkPPC`; `MWPascal68K`/`PPC` are PowerPC-only and
don't run in the 68k session) and Apple's `Link`, `PPCLink`, `Rez` and the
rest. Everything comes from the CodeWarrior Pro 4 Tools CD, a public pin
(the `codewarrior-pro-4` snapshot), so it needs no E.T.O. disc.

* **The disk** (`provisioning/images/mpw-cw.json`, built by `mpw --toolchain
  cw setup`): the CD's `CodeWarrior MPW` folder (MPW and
  Interfaces&Libraries, as on the E.T.O. disk) plus what Metrowerks'
  compilers link with, which the IDE install keeps outside it: MSL C
  (headers and libraries) and MacOS Support's libraries, under
  Interfaces&Libraries; and ToolServer.
* **The session** is its own (`~/.cache/mac-phoenix/mpw-cw`, port 18391).
  Commands go to ToolServer 3.4.1, as with Apple's toolchain: the CD's
  installer doesn't install it, but its `Apple Development Tools` folder has
  it (`ToolServer 3.4.1.sit`, the `toolserver-3.4.1` snapshot), and the disk
  has it in the MPW folder. It is launched by path (the System disk has
  Apple's ToolServer too, same creator), and BridgeAgent sends to the
  running process.
* **Variables** set before every command (ToolServer's `StartupTS` sets
  only Apple's): `{HeadersAndLibs}`; `{MWCIncludes}` (the compilers'
  default include folder) is Apple's `{CIncludes}`; `{MSLCommonIncludes}`,
  `{MSLMacIncludes}`, `{MSLLibraries}`, `{MWMacOS68K}`, `{MWRuntime68K}`,
  `{MWRuntimePPC}` name the Metrowerks folders.
* **A project** can have its own `build.cw.mpw`, used instead of
  `build.mpw` with this toolchain. MacPhoenixSuite's
  (`tests/guest/suite/build.cw.mpw`) makes the same fat application:
  `MWC68K -model far` + `MWLink68K` with `MacOS.lib`, `MSL Runtime68K.Lib`,
  `MSL C.68K Fa(4i_8d).Lib`, `MSL SIOUX.68K.Lib`, MathLib68K; `MWCPPC` +
  `MWLinkPPC` (which writes the `cfrg` itself) with InterfaceLib,
  AppleScriptLib, MathLib and the PPC MSL; `Rez` adds the 68k CODE and DATA
  (`include "NAME.68k" not 'cfrg';`) and the SIZE. Its build passes the
  guest suite on the 68k, Executor and PowerPC machines.

Things to know: Metrowerks' objects aren't Apple's (`Link` can't read
them, `MWLink68K` brings its own runtime module and wants MSL's); quoted
includes need `-i "`` `Directory` ``"` (the compilers don't search the
current folder); `'\n'` is 13 here too (`-mapcr`, the default).

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

`ctest -R guest_builds_current`: the committed guest programs
(MacPhoenixSuite, OTTest) are their sources' builds (built again and
compared by `tests/lib/macbinary_same.py`, which leaves out a PEF's
timestamp and the resource map's scratch), and projects with a
`build.cw.mpw` build with CodeWarrior.
