# QuickTime on Executor: the decision (survey, 2026-11-08)

QuickTime is the last open part of M3f ("QuickTime movie plays in
SimpleText"), and STRAGGLERS put it off. This document has the data for
choosing how QuickTime comes to Executor, and a recommendation. Three routes:

- **A**: Apple's QuickTime™ INIT run as it is, switched on per System or app disk.
- **B**: Executor registers Apple's QuickTime components (codecs, sound
  converters, the music synthesizer: the `thng`s) itself and provides
  its own C++ Movie Toolbox, ICM and movie controller. No INIT, no patches.
- **C**: staged, mixing the two.

The owner's worry was that Apple's INIT is invasive. It is, more than the
draft thought (section 2). The surveyed apps use a small, regular part of
the API (section 1). On Executor, Apple's codecs already register and open
without the INIT, and their first missing pieces are small (section 3).

**Recommendation: C, B first** (section 5). First lay the groundwork both
routes need. Then write Executor's own Movie Toolbox, ICM and movie
controller over Apple's codecs, for the calls in section 1. Keep A as a
per-disk switch, used only for applications that need what B doesn't
cover (editing, QTVR, sprites, wired movies).

Data, tools and detail files:

| What | Where |
|---|---|
| Call lists per app, traced (tables) | `tools/qt/survey/calls.md` |
| Call lists per app, static (68k glue, PowerPC imports) | `tools/qt/survey/static.md` |
| Patch inventory, INIT structure, what QuickTime reads (full) | `tools/qt/survey/patches.md` |
| Trace an app on the Quadra (`--trace-atraps`) and snapshot it | `tools/qt/trace_app.py` |
| QuickTime calls in a snapshot, by app (`--resolve`: component instance → its `thng`) | `tools/qt/qt_calls.py` |
| An extension's trap installs on a traced boot | `tools/qt/qt_patches.py` |
| Static scan of an app on an image | `tools/qt/qt_static.py` |
| Route B probe (68k app, MPW) | `tools/qt/probe/QTCodecProbe.c` |
| Snapshots | `~/storage/snapshots/qt-{ares3,movieplayer25,hypercard25,simpletext25,canvas25}` (7.6.1, QT 2.5), `qt3-cythera` (7.6.1 with QT 3.0.2) |

## 1. What the applications call

These were traced on the Quadra 650 (UAE, `--trace-atraps`, 7.6.1 with
QuickTime 2.5, or with 8.6's QuickTime 3.0.2 copied in). Each count is
the app's own calls: from its code, not at interrupt time, not nested
inside QuickTime. Static lists cover the code paths the runs didn't reach.

### MoviePlayer 2.5 (open a movie, play, click into it)

- **Movie Toolbox, 26 routines.** `EnterMovies`, `OpenMovieFile`,
  `NewMovieFromFile`, `SetMovieGWorld`, `SetMovieActive`,
  `SetMovieProgressProc`, `GetMovieBox`/`SetMovieBox` (4/1),
  `GetMovieMatrix`, `GetMovieDuration`, `GetMovieTrackCount`,
  `GetMovieIndTrack` (6), `GetMovieIndTrackType`, `GetTrackEnabled` (6),
  `GetTrackAlternate` (6), `GetMovieUserData`/`GetUserData` (5),
  `QTFindChildByID`, `GetMoviesError` (5), and 2 selectors Universal
  Interfaces doesn't name ($273, $274).
- **Movie controller (`'play'`).** `NewMovieController`, `MCIsPlayerEvent`
  (3880, once per event), `MCDoAction` (10), `MCGetControllerInfo`,
  `MCGetControllerBoundsRect`, `MCSetUpEditMenu`, `MCEnableEditing`,
  `MCSetActionFilterWithRefCon`.
- **Component Manager.** `RegisterComponentResourceFile`, `GetComponentInfo`.
- **Static.** 363 glue sites, about 100 distinct routines: editing,
  export and the rest of the menus. MoviePlayer 3.0 on 8.6 is 68k only,
  with 419 sites.

### SimpleText 1.4 (File > Open with preview, play)

- **Movie Toolbox.** `EnterMovies`/`ExitMovies`, `OpenMovieFile`,
  `NewMovieFromFile`, `CloseMovieFile`, `DisposeMovie`, `SetMovieGWorld`,
  `GetMovieBox`/`SetMovieBox`, `IsMovieDone` (1255), `GetMoviesError`.
- **Movie controller.** `NewMovieController`, `DisposeMovieController`,
  `MCIsPlayerEvent` (1177), `MCDoAction`, `MCGetControllerBoundsRect`.
- **ICM.** `CustomGetFilePreview`, `AlignWindow`.

### HyperCard 2.4.1 (QuickTime Tools stack, the Movie XCMD, play)

- **Movie Toolbox, 38 routines.** `GetMovieTime` (2 direct, 15064
  nested), `GetMovieDisplayBoundsRgn` (1587), `PtInMovie` (1586),
  `GetMovieRate`, `SetMovieRate`, `PrerollMovie`, `GoToBeginningOfMovie`,
  volume, time scale, duration, tracks, media, `GetMediaHandlerDescription`,
  user data.
- **Movie controller.** `MCIdle` (2227), `MCDrawBadge` (1114),
  `MCIsPlayerEvent` (1111), `MCDoAction`, `MCSetVisible`/`MCGetVisible`,
  `MCPositionController`, `MCSetClip`, `MCMovieChanged`,
  `MCSetControllerAttached`, `MCGetControllerBoundsRect`,
  `MCGetControllerInfo`, `MCSetActionFilterWithRefCon`.
- **ICM.** `CustomGetFilePreview`.
- **Where the code is.** All of this is in the stack's XCMDs, not in
  HyperCard's CODE (5 glue sites).

### Canvas 5.0.2 (launch, new document, Open dialog)

- **Traced.** `EnterMovies`, `CustomGetFilePreview`, and its own
  component (`RegisterComponent`, `SetDefaultComponent`).
- **Static.** ICM `DecompressImage` (2 sites, for compressed PICTs).

### Ares 1.2.0 (fat: 68k traced on the Quadra, PowerPC static)

- **Traced.** Intro, menu and a tutorial level make one call:
  `EnterMovies`. Its music and sound effects are Sound Manager
  (`SndPlayDoubleBuffer`, `SndChannelStatus` 2715).
- **The movies.** "Part 1–3" (4 MB each) play at story chapters, which
  the run didn't reach.
- **Static, 19 Movie Toolbox routines,** the same in the 68k code and in
  `QuickTimeLib` imports: `EnterMovies`/`ExitMovies`,
  `OpenMovieFile`/`CloseMovieFile`, `NewMovieFromFile`/`DisposeMovie`,
  `SetMovieGWorld`, `GetMovieBox`/`SetMovieBox`,
  `GetMovieColorTable`/`SetMovieColorTable`, `PrerollMovie`,
  `StartMovie`/`StopMovie`, `MoviesTask`, `IsMovieDone`, `GetMovieRate`,
  `SetMovieVolume`, `GetMoviesError`.
- **No movie controller.**

### EV Nova 1.0.10 (PowerPC; static)

- **What 8.6 runs.** The `EV Nova.ppc` fragment (InterfaceLib); there
  is also an `EV Nova.carbon` one.
- **22 `QuickTimeLib` imports.** `EnterMovies`/`ExitMovies`,
  `OpenMovieFile`/`CloseMovieFile`, `NewMovieFromFile`/`DisposeMovie`,
  `LoadMovieIntoRam`, `SetMovieGWorld`, `GetMovieBox`/`SetMovieBox`,
  `GetMovieDuration`, `GetMovieTimeBase`,
  `GetTimeBaseFlags`/`SetTimeBaseFlags`, `GoToBeginningOfMovie`,
  `SetMoviePlayHints`, `SetMovieRate`, `SetMovieVolume`,
  `StartMovie`/`StopMovie`, `MoviesTask`, `IsMovieDone`.
- **No movie controller.**
- **Its movies.** The "Race N.mov" intros in Nova Files.

### Cythera 1.0.4 (needs QT 3.0; traced on 7.6.1 with 3.0.2)

- **Music only, through the Music Architecture.**
  `OpenDefaultComponent('tune')` → QuickTime™ `'tune'` -19028:
  `TuneSetHeader`, `TuneSetTimeScale`, `TunePreroll`, `TuneQueue`,
  `TuneSetVolume`, and `TuneGetStatus`, polled 6.8 million times in
  90 s (a busy loop). Plus Sound Manager calls.
- **PowerPC imports also include** the note allocator (`NANewNoteChannel`,
  `NAPlayNote`, `NAStuffToneDescription`, `NADisposeNoteChannel`),
  `TuneStop`/`TuneUnroll`, and the preview calls `StandardGetFilePreview`,
  `MakeFilePreview`, `AddFilePreview` and `MakeThumbnailFromPixMap`
  (saved games).
- **No movies.**

### How PowerPC applications call QuickTime

- **QuickTimeLib is glue.** QuickTime™ PowerPlug 2.5's QuickTimeLib
  exports 1182 routines and imports only `CallUniversalProc` (3.0.2:
  1502 exports, plus `CallOSTrapUniversalProc`). Its exports are glue
  into the 68k `$AAAA`/`$AAA3`/`$A82A` traps.
- **Logging them on the G3.** A PowerPC app's QuickTime calls reach the
  ROM's 68k emulator as A-traps. A hook there gives the same `atraps.tsv`
  as UAE: on the nanokernel's A-trap path, or on mame-68k under
  `--m68k-host 2`. That hook is not written; this survey used the static
  import lists for EV Nova and Cythera.
- **On Executor.** Executor's PowerPC import logging, or
  `--executor-ppc-break '!Routine'`, would count them once QuickTimeLib
  has exports.

### What the apps have in common

- **The Movie Toolbox calls used are a small, stable set.** Open, new
  from file, GWorld, box, start/stop, `MoviesTask`/`IsMovieDone`, rate,
  volume, time, duration, preroll, dispose. Add track and user-data
  enumeration for MoviePlayer and HyperCard, about 45 routines in all.
- **Movie controller.** About 15 `MC…` calls: event, idle, action,
  bounds, visible, badge, clip.
- **ICM.** The Standard File previews, and `DecompressImage` for
  compressed PICTs.
- **Music Architecture.** Only Cythera uses it, through the tune player.
- **Nothing else.** No editing beyond MoviePlayer's menus. No QTVR,
  sprites or wired movies in these apps.

## 2. QuickTime's own code: the patch inventory

What QuickTime's INIT changes on a traced 7.6.1 boot (`tools/qt/qt_patches.py`),
and what each patch does (disassembly). Full tables with routine offsets:
`tools/qt/survey/patches.md`.

### What the INIT does

- **Installs.** INIT 0 checks Color QuickDraw, `QDExtensions`, Gestalt,
  and that Gestalt `'qtim'` is absent. It then runs `inst` resources
  through its own loader. Nothing goes through the System's patch loader.
- **`$AAAA` has no SetTrapAddress.** INIT 0 writes `StripAddress(*'code'
  -19103)` straight into trap-table slot **$18A8**. QuickTime's code then
  often calls `jsr ($18A8)` and `jsr ($188C)` (the ICM) directly,
  not through traps.
- **Globals.** QuickTime's own at **$DE8** and **$DF0** (its segment
  loader). The ICM's go at **`($28A)+8`**, inside the Component
  Manager's globals.
- **The lpch loader the draft feared runs only on System 6.** The lpchs
  bring System 7's extended Time Manager to System 6. On an unknown ROM
  they put up SysError 98.
- **The 7.x ROM check is `inst -19063`.** It reads `ROMBase+8`. Unless
  the value is a Plus, SE or Power Mac, it runs `'pach' -19070`. That
  binds ROM-internal addresses for five ROM versions and installs
  `$A0B3` (a File Manager internal); an unknown version gets
  SysError 99. Gestalt `'bugx'` bit 11 skips it.
- **Its own Component Manager (`inst -19071`).** It is kept off when
  Gestalt `'cpnt'` ≥ 6 (2.5) or ≥ 7 (3.0.2). On 7.6.1 (`'cpnt'` 6)
  QT 3.0.2 **replaced** the System's ComponentDispatch; 2.5 didn't.
- **Its own Microseconds (`inst -19069`).** It reads VIA1 timer 2 and
  the Time Manager globals at $B30, unless Gestalt `'bugz'` bit 8 is set.

### The traps it patches (2.5; 3.0.2 adds the last four)

| Trap | What the patch does | For |
|---|---|---|
| `$AAAA` _QuickTime | the Movie Toolbox dispatcher ($286 selectors; 3.0.2 $2E3) | movies |
| `$AAA3` _CodecDispatch | the ICM (121 selectors, version 18/19) | movies |
| `$ABC2` (private) | fixed/wide math and matrices: FixMulDiv, CompMul…, TransformRect, InverseMatrix (38 selectors) | movies (codecs call it) |
| InitGDevice, MoveWindow, ZoomWindow, JShowCursor/JShieldCursor hooks | tell the ICM the screen or a movie's window changed, and shield the cursor | movies (direct-to-screen) |
| StdOpcodeProc `$ABF8`, DrawPicture | decompress `$8200`/`$8201` QuickTime opcodes in PICTs | compressed PICT |
| SetStdCProcs, CopyDeepMask | QuickTime's StdPix bottleneck in `newProc1`; deep masks through it | compressed pixmaps, pictures |
| Pack3 | selector 9: SFGetFilePreview | Standard File preview |
| SetCCursor | colour cursor changes at VBL time | side |
| UnloadScrap | nil ScrapHandle → noErr | side |
| PBOpenRF, AliasDispatch, PBMountVol, PBUnmountVol | data-reference aliases, volume tracking, releasing a movie's file | side |
| OpenCPort | `_ABC0` selector 9 on the new port (unidentified 7.x trap) | bookkeeping |
| QDExtensions | NewGWorld depth 24 → 32; shrinks direct GWorlds' inverse table (reads `grafVars^^+26`) | bookkeeping |
| MaxApplZone, InitGraf | bug fix; hooks `_CompactMem` in the system heap's grow-zone proc | bookkeeping (memory) |
| SetApplLimit, InsTime/PrimeTime, MoveHLow | patched by writing the trap table directly | bookkeeping |
| **3.0.2:** ComponentDispatch | its own Component Manager, when `'cpnt'` < 7 | components |
| **3.0.2:** Microseconds | made strictly increasing | movies (timing) |
| **3.0.2:** SectRgn | clears MemErr | bookkeeping |
| **3.0.2:** FSDispatch (PBGetCatInfo), PBGetFInfo | type/creator from the name's extension (`'fttl'` table: .avi, .gif, .jpg…) | side |

### Which Executor already does natively

None. Executor has no `$AAA3`, `$ABF8`, `$ABC2` or `$ABA8`. DrawPicture
skips `$8200`/`$8201` and never calls `opcodeProc`. SetStdCProcs leaves
`newProc1` nil. CopyDeepMask is CopyMask. Pack3 has no selector 9.
Microseconds is the host clock (monotonic, but two calls can return the
same value). Executor's QuickTime is `romlib/quicktime.cpp`, stubs that
return noErr.

### Which are needed for movies

The dispatchers (`$AAAA`, `$AAA3`, `$ABC2`), the ICM's
screen-change notices, and a usable Microseconds. Everything else is a
side feature (compressed PICTs, previews, file typing) or bookkeeping
for Apple's Memory Manager and QuickDraw.

## 3. Route B: Apple's components without the INIT (probe result)

`tools/qt/probe/QTCodecProbe.c`, a 68k app built with `tools/mpw`, was
run on Executor 7.6.1 with QuickTime™ off by policy, as it is now:

1. **`RegisterComponentResourceFile`** on the QuickTime™ file (the
   locked 7.6.1 image's: policy keeps it out of Executor's System Folder)
   returns **115** components on Executor's own C++ Component Manager.
   These are 16 `imdc` (Animation, Graphics, JPEG, Motion JPEG A/B,
   Video, None, Photo CD, Cinepak, YUV2, …), 10 `imco`, 10 `mhlr`,
   5 `dhlr`, 3 `clok`, 1 `play`, 1 `tune`, 1 `sdev`, and 19 `eat `.
   `OpenComponent` on the `raw ` and `rle ` decompressors works.
2. **Every codec call returns -50 without a single trap.** QuickTime's
   codecs set **`cmpFastDispatch`** (thng flags `$40000000`). Their entry
   expects D0 = the `{flags, paramSize, what}` long and the caller's
   parameters left in place, with the instance replaced by its storage.
   Executor's `call_entry` always uses the standard
   (params, storage) frame. This is a missing Component Manager feature,
   about 30 lines.
3. **With the probe making the fast-dispatch call itself,** the codec's
   open succeeds and sets its storage. `ImageCodecGetCodecInfo` then
   calls **HeapDispatch selector 2**, and Executor stops the session.
4. **With the probe also putting HeapDispatch 0–5 in the trap table,**
   `GetCodecInfo` returns 0. `ImageCodecPreDecompress` returns 0 and asks
   for 32-bit pixels, band 1/1. It draws into the GWorld with GetPort,
   SetPort, RGBBackColor, NewRgn and SetRectRgn.
5. **`ImageCodecBandDecompress` then jumps into Executor's
   unimplemented-trap handler,** with D0 = 0 and A0 = the handler's
   address: a call through an address obtained for a routine Executor
   lacks. The fatal alert names `A873`, the last trap dispatched, not
   the culprit. The `raw ` codec's calls outside the Toolbox Executor
   has are:
   - `$ABC2` math, 3 sites;
   - `$AAA3` ICM, 5 sites: `GetImageDescriptionCTable` ($1E) and the
     private $49/$4A;
   - `$AB1D` QDExtensions, 4 sites;
   - `$A0A4`, 3 sites.

   The jump is one of these, reached by address. The probe also put a
   stub on `$AAA3` and that changed nothing, so the culprit isn't
   pinned down. The next probe step is a stub for `$ABC2` and the ICM
   slot `$188C`.

**So B is feasible.** Apple's binaries run on Executor's Component
Manager, and the codec's own calls are ordinary Toolbox calls plus a
small, documented layer:

- **`cmpFastDispatch`** in component.cpp.
- **HeapDispatch 0–5.** These are trivial; 6, 8 and 13/14 are for the
  media handlers.
- **`$ABC2`.** About 38 fixed-point and matrix routines, documented in
  `FixMath.h` and `ImageCompression.h`.
- **The ICM utility calls codecs make back:** `CodecManagerVersion`
  first, then the rest of the codec-side calls (Inside Macintosh:
  QuickTime Components, "Image Compressor Components").
- **The ICM's trap-table slot `$188C` pointing at Executor's ICM.**
  Codecs call through the slot, not the trap.

What the probe did not reach: an actual decoded frame. The next blockers
are among `$ABC2`, the ICM callbacks ($1E, $49, $4A for `raw `) and
QDExtensions. Media handlers
(`mhlr`) and data handlers (`dhlr`) call back into the Movie Toolbox
itself. So B would parse movies and drive the codecs in C++, and leave
those two out.

## 4. Route A: Apple's INIT on Executor (first run and what it needs)

**The first run.** With the INIT allowed on 7.6.1, Executor stops at
**HeapDispatch ($A0A4) selector 0**.

- **What HeapDispatch is.** On 7.6.1 it is the System's own `'gpch' 667`,
  not QuickTime's: an 18-selector Memory Manager dispatcher. Selectors
  0–5 are fast HLock, HUnlock, HPurge, HNoPurge, HGetState and HSetState
  (A1) on the handle in A0. They set the master-pointer flags byte at
  `*h-11` directly, with no checks. 6–9 add or remove handle-trash
  notifications. 10–12 trash a zone, a range or a process. 13–15 are
  NewHandleSys/NewPtrSys variants that respect the system-heap reserve
  (ExpandMem+$1AE). 16/17 set and get the trash list.
- **How QuickTime uses it.** Its code wraps nearly every Movie Toolbox
  call in selectors 2 and 3. Over the 7.6.1 session (MoviePlayer,
  HyperCard and SimpleText each playing a movie), QuickTime's own code
  made 3474 and 3369 such calls. It also uses 0, 1, 6 and 8. Codecs, media handlers and data
  handlers call 0–5.
- **Why it doesn't install its own.** Executor implements `$A0A4`
  (selectors $A and $20 only), so QuickTime's `inst -19064` sees it as
  present.

What A needs from Executor, beyond HeapDispatch (from `tools/qt/survey/patches.md`
§4 and the boot's call list):

- **Gestalt.**
  - `'bugz'` bit 8, or QuickTime installs a VIA-timer Microseconds that
    returns garbage on Executor's zeroed VIA and empty $B30.
  - `'bugx'` bit 11, or it runs `'pach' -19070` against Executor's
    10-byte fake ROM (version $067C) and installs `$A0B3` with pointers
    into nothing.
- **Component Manager globals at `$28A`.** Executor never sets them,
  and with Executor's CM kept (`'cpnt'` $30000) the ICM would write its
  globals to address 8. The other choice is to answer `'cpnt'` 6 and let
  QT 3.0.2 replace Executor's CM with Apple's 68k one.
- **`$ABA8`.** With `'qd  '` = $0230 QuickTime writes a long at
  `($1CA0)+8`, inside Executor's unimplemented-trap code. That is a hook
  into 32-bit QuickDraw's internal `$ABA8`, which Executor lacks.
- **GrafVars.** The QDExtensions patch reads `grafVars^^+26` as the
  GWorld's device. Executor's GrafVars is 26 bytes, so the patch would
  resize a garbage handle.
- **DrawPicture** must call `opcodeProc` for `$8200`/`$8201`, or
  compressed PICTs stay blank.
- **Patch routing (`stub_NAME`).** Executor's C++ calls DrawPicture,
  MoveWindow, ZoomWindow, InitGDevice, Pack3, SetStdCProcs and the
  cursor hooks directly, so QuickTime's patches never see those calls.
  Movies on screen would not follow window moves, and Executor's own
  pictures and Standard File dialogs would skip QuickTime.
- **MoveHLow `$A09D` and `_ABC0`.** QuickTime calls MoveHLow (105 times
  around NewMovieFromFile and the ICM) and `_ABC0` (from OpenCPort, 40
  times). Executor has neither.
- **PowerPC.** On `--executor-ppc`, QuickTime™ PowerPlug's glue into the
  68k traps goes through Mixed Mode, and 3.0.2's native pieces (`npch`
  math, `pbok` fixes, the data fork's PEF code) through CFM.
- **Other versions.** Each QuickTime version (2.1 on 7.5.5, 2.5,
  3.0.2, 4.0.3) has its own thresholds, ROM tables and patch sets, and
  needs this list checked again.

The inits.yaml list from the draft is mostly done: `OpenComponentResFile`,
`CloseComponentResFile`, the mod seeds and `SetComponentInstanceStorage`
are in component.cpp now. `QuickTime#80E9` and `#81E5` are QuickTime's
own private `$AAAA` selectors (alias search, volume mount), served by
QuickTime itself under A.

## 5. Effort, risk, recommendation

| | A: Apple's INIT | B: Executor's Movie Toolbox over Apple's components | C: staged (B first, A as a switch) |
|---|---|---|---|
| **What Executor writes** | Gestalt answers, `$28A`, `$ABA8` dummy, GrafVars layout, HeapDispatch 0–16, MoveHLow, `_ABC0`, DrawPicture opcodeProc, patch routing for ~10 traps, Mixed Mode for PowerPlug | cmpFastDispatch, HeapDispatch 0–5, `$ABC2`, an ICM (DecompressImage, sequences, previews, codec callbacks), a Movie Toolbox subset (~45 calls) with a `moov` parser, a movie controller (~15 calls), sound through Executor's Sound Manager, QuickTimeLib exports for PowerPC | B, then A's items only for the apps that need them |
| **Effort** | 2–4 weeks to boot clean with 2.5 and play in MoviePlayer. Then an open-ended tail per app, and again per QuickTime version | Groundwork 2–3 days; codecs decoding through the ICM 1 week; Movie Toolbox and playback timing 2–3 weeks; controller 1 week; sound 1–2 weeks; PowerPC exports 1 week. Music Architecture separately (Cythera) | B's, then A's items one at a time |
| **Risk** | High. QuickTime writes into Executor's code and low memory, relies on Apple's block headers, grow-zone proc and GrafVars, and is bypassed by Executor's direct C++ calls. Each fix bends Executor toward Apple's internals | Medium. The API surface is known (section 1) and documented. Codecs proved callable. Unknowns: the full set of ICM callbacks, sound (IMA/MACE through Apple's `sdec`/Sound Converter or our own decoders), and edge cases of timing | Lowest. B covers the measured apps; A stays possible without being the foundation |
| **Fits Executor** | Poorly. Opposite of how Executor runs the Toolbox: patches see none of its C++ | Well. The same shape as Executor's Sound Manager, Component Manager, CFM and PPC libraries: C++ behind the API, Apple's components as binaries | Well |
| **Versions** | 2.1/2.5/3.0.2/4.0.3 each separately | One Movie Toolbox; Apple's codecs from any version (or ours) | As B |
| **PowerPC apps** | PowerPlug through Mixed Mode | Native QuickTimeLib exports in C++ (as docs/executor/PPCLibs.md did StdCLib/MathLib) | As B |
| **Own System / wasm tab** | No (Apple's INIT needed) | Movie Toolbox, ICM and controller are ours. Movies need codecs: Apple's from a QuickTime™ file on the disk, or later ours (`raw `, `rle `, Cinepak are documented) | As B |

**Recommendation: C, B first**, in this order:

1. **Groundwork (both routes).**
   - cmpFastDispatch in component.cpp.
   - HeapDispatch 0–5, plus 6/8 accepted.
   - `$ABC2` math and matrices.
   - Gestalt `'qtim'`/`'icmp'` answered only when Executor's QuickTime
     is there, never from the stubs.
   - A guest test: QTCodecProbe grown into qt-client's QTTest, decoding
     `raw `/`rle `/Cinepak frames and comparing them with the Quadra's.
2. **ICM.** `DecompressImage`/`FDecompressImage`, `DecompressSequenceBegin`
   and `DecompressSequenceFrame`, `GetCodecInfo`, the codec callbacks
   (driven by the probe), and the `$188C` slot. Then DrawPicture's
   `$8200`/`$8201` through it (Canvas), and SFGetFilePreview /
   CustomGetFilePreview (Pack3 selector 9, `$AAA3` $41–$44).
3. **Movie Toolbox subset.** The calls in section 1 (Ares, EV Nova,
   SimpleText first): `moov` parsing, sample tables, a time base on the
   Time Manager, `MoviesTask` drawing through the ICM, sound tracks
   through Executor's Sound Manager.
4. **Movie controller,** the ~15 `MC…` calls (MoviePlayer, SimpleText,
   HyperCard), then PowerPC `QuickTimeLib` exports.
5. **A, only if an app needs what B lacks** (editing, QTVR, sprites,
   the Music Architecture for Cythera if Apple's `'tune'`/`'note'`
   components won't run under B). A per-disk switch, with A's blockers
   (section 4) fixed as they come up.

## 6. Notes from the survey

- **Testing without built movies.** Movies are already on the disks:
  7.6.1's Extensions "Demo click&drag" (and others), HyperCard's
  "Simple Movie", the Kid Pix movies on Creative, Ares' "Part 1–3",
  EV Nova's "Race N.mov", 8.6/9.0.4's "Sample Movie". qt-client's
  generated movies (`tests/lib/qtmovie.py`) remain the basis for
  checksummed tests.
- **Cythera** runs on 7.6.1 with 8.6's QuickTime™ 3.0.2 copied in
  (`~/storage/tmp/qt-survey/qt3/7.6.1-qt3.img`). This is useful for
  tracing 3.0.2 on the Quadra.
- **`--trace-atraps` is UAE-only.** `trap_installs.tsv` from a traced
  boot is the quickest patch inventory for any extension
  (`tools/qt/qt_patches.py --file NAME`). The resolver misattributes
  installs made from code copied into heap blocks.
- **A fix outside this survey.** `tools/macdecode/dispatch.py` failed on
  every run after the multiversal YAML gained component-selector
  dispatchers (`selector-location: Component`). It now skips them
  (a component's selectors, not a trap's).

## 7. Route B: what Executor has (qt-b1, 2026-11-08)

The groundwork of section 5's first step. QuickTime's INIT still never
runs. Gestalt `'icmp'` is answered since qt-b2 (section 8), `'qtim'`
since qt-b3 (section 9).

- **Components at startup.** `extensions.cpp` registers the `thng`s of
  QuickTime™ and QuickTime™ Musical Instruments, from Extensions or from
  where the policy put them ("Extensions (Disabled)"). That is 115 + 1 on
  7.6.1, as RegisterComponentResourceFile would register them. QuickTime™
  PowerPlug's are registered only when PowerPC code runs (a 68k Mac never
  loads them). A component that wants its register message is left out,
  because its code would run without the INIT. A file the policy lets run
  registers its own.
- **Search order.** A 7.6.1 Mac finds the components of one type by
  subtype, then manufacturer, not in registration order (QTTest's lists
  on the Quadra). The Sound panel's `'sndP'` pages and Color Picker's
  `'cpkr'`s, which the old rule was written for, come out the same.
  QTTest's component lists on Executor 7.6.1 now match the Quadra's in
  72 of 73 lines. The odd one is `'sdev'`: Executor's own sound output
  (`'xctr'`) where the Quadra has its hardware (`'asc '`).
- **cmpFastDispatch** (`component.cpp` `call_entry`). D0 is the header
  long. The caller's parameters are copied below the storage and the
  result, and the routine pops them as a Pascal function.
- **HeapDispatch** 0–5 (HLock … HSetState(A1), MemErr left alone) and 6/8
  (trash notices accepted; A0 is a {handle, priority.w, proc} record, or
  A0 = proc and A1 = handle) (`MemoryMgr.yaml`, `tempmem.cpp`).
- **`$ABC2`**: all 33 routines Universal Interfaces 3.3.2 names
  (`ImageCompression.yaml`, `qtmath.cpp`). The rounding and edge cases
  follow a Quadra with QuickTime 2.5, measured by `tests/guest/qtmath`.
  Its 195 compared lines match; ctest `qtmath_executor_761` checks
  Executor, and `qtmath_rom_68k_761` checks that the reference still
  holds. The behaviour measured:
  - CompShift rounds to nearest; CompFixMul rounds down.
  - CompDiv saturates, with remainder $80000000.
  - FixMulDiv rounds by adding the divisor's signed half. An overflow is
    $80000000 | the product's high long; a zero divisor gives
    $80000000 | mul.
  - Perspective matrices are refused: points answer -1 and stay as they
    were, and InverseMatrix answers true without writing.
  - ConcatMatrix(a, b) is b then a. TransformFixedRect is true only
    without rotation. Points round halves away from zero.
  - Apple's sine, exponent, logarithm and power are approximations
    (about 2^-12). Executor's are exact, so the probe prints those as
    notes.
  - CompSquareRoot ($11) is QuickTime 3.0's: on 2.5 it never returns.
- **`$AB27`**, QuickDraw's bits bottleneck below CopyBits (the name in
  `QuickDraw.yaml`, `StretchBitsBottleneck`, is ours). The codecs call it
  through its trap-table slot `$1A9C` to put a band into the destination.
  That was the survey's "jump into the unimplemented handler" in
  BandDecompress.
- **The ICM so far** (`icm.cpp`, `$AAA3`): CodecManagerVersion (18),
  GetImageDescriptionCTable ($1E), ICMDecompressComplete ($63, which the
  `rle ` codec calls at the end of a band). The other 140 of the 143
  selectors ImageCompression.h names pop their arguments (sizes from
  the declarations) and answer paramErr, logged. An application that
  checks the trap's address rather than Gestalt (AppleWorks 5:
  GetGraphicsImporterForFile) now finds `$AAA3` and carries on.
- **The 1998 stubs** stop pretending: EnterMovies and NewMovieFromFile
  answer -4 (unimpErr) while there is no Movie Toolbox.

`tools/qt/probe` (QTCodecProbe), run by `tests/test_guest_suite.sh`, now
uses Executor's own calls. On Executor 7.6.1, `raw ` and `rle ` get
through GetCodecInfo, PreDecompress (the Quadra's flags) and
BandDecompress. `rle `'s pixels equal the Quadra's. `raw `'s are the
input's, where the Quadra, driven this way without its ICM, gives other
values. QTTest's ICM round trips are B2's pixel check.

## 8. Route B: Executor's ICM (qt-b2, 2026-11-08)

`icm.cpp`, `$AAA3`, over Apple's codecs; Gestalt `'icmp'` is answered
(18); `'qtim'` since the Movie Toolbox (B3, section 9).

- **What it answers.** CodecManagerVersion, GetCodecNameList and
  DisposeCodecNameList, GetCodecInfo, FindCodec, GetMaxCompressionSize,
  GetCompressionTime, CompressImage, FCompressImage, DecompressImage,
  FDecompressImage, GetCompressedImageSize; CompressSequenceBegin/Frame
  and their setters (quality, previous frame, key frame rate, frame
  number); DecompressSequenceBegin(S)/Frame(S) and their setters (matrix,
  matte, mask, transfer mode, data proc, accuracy, source rectangle,
  flags); CDSequenceBusy/End/Flush, SetSequenceProgressProc; image
  description color tables and extensions; MakeImageDescriptionForPixMap;
  CompressPicture/FCompressPicture; the Standard File previews $41-$44
  (Standard File's dialogs, Apple's PACK 3 on an Apple System, without the
  preview pane). The other selectors still pop their arguments and answer
  paramErr, logged.
- **Calling a codec.** `ROMlib_call_component` (`component.cpp`) builds
  the parameter block on the 68k stack as an application's glue does.
  Each image is one band. PreDecompress with the real destination says
  what the codec wants; it draws straight into the destination when it
  can do all of it (depth, no scaling, mode, mask, the port's regions);
  otherwise into an offscreen at the depth it wants, CopyBits doing the
  rest. A band that fails there (a depth the codec converts to through
  Apple's private ICM routines) is redone at the image's own depth. Screen
  destinations always go through an offscreen, so QuickDraw sees them
  change.
- **What Apple's codecs expect** (found against the Quadra with QTICM):
  rowBytes without the pixel map flags (they multiply by it as a signed
  word; the flags made rle read garbage and write over the heap);
  srcPixMap's baseAddr is the map's origin and bounds the rectangle to
  compress; locked pixels (a GWorld's baseAddr is its pixels only then).
  Compressors fill dataSize themselves.
- **Private selectors** no header names, from the codecs' call sites:
  `$49` (a color translation: CTabHandle, 0, Handle *, 0, short *flags),
  `$4A` (dispose it), `$60` (lock it), `$58`/`$59` (around a band). The
  translation is 12 bytes, then for each color the device's pixel repeated
  to fill a long (smc fills four 8-bit pixels with one; Cinepak's palette
  codebooks take the long for 32 bits, its high word for 16). Executor
  makes one for a direct device, or for an indexed one with the image's
  colors (the ICM's offscreen for an indexed image), where flags' high bit
  says no translation is needed; Cinepak passes nil flags. Otherwise raw
  and rle draw without one, smc's and rle's indexed paths fail into the
  ICM's offscreen, and Cinepak gives up the frame.
- **Pictures.** DrawPicture draws `$8200` CompressedQuickTime through the
  ICM, the opcode's matrix composed with the frame mapping; PnSize(`$AE`,
  n) right after it skips the n bytes QuickTime writes for a Mac without
  it (the "QuickTime and a ... decompressor are needed to see this
  picture." text). `$8201` is passed over. CompressPicture writes what
  QuickTime 2.5 writes: header, ClipRgn, the opcode (accuracy
  codecHighQuality), the fallback text in Geneva 12 scaled to the frame;
  `'raw '` a plain picture of the pixels.
- **QTICM** (`tests/guest/qticm`; ctest `qticm_executor_761`) against the
  Quadra: 46 of 61 lines identical. Codec information, every `raw `, `rle `,
  `rpza`, `jpeg` and 32-bit `cvid` line, and the compressed pictures byte
  for byte. The 15 others are QuickDraw's (`known_executor_761.txt`):
  dithering (Executor's CopyBits draws ditherCopy as srcCopy: `smc `'s
  8-bit source, Cinepak into 16 bits), Executor's own picture recording
  (the source PICTs), and CopyBits' 2:1 shrink at an odd offset.
- **Open.** The preview pane. Codecs scaling and converting depths themselves
  (Apple's `$49` translations). ditherCopy in QuickDraw would close most
  of the known differences.

## 9. Route B: Executor's Movie Toolbox (qt-b3, 2026-11-08)

`movies.cpp`, `$AAAA`, Gestalt `'qtim'` = `$02508000` (2.5). 115 more
selectors in `QuickTime.yaml`, from Universal Interfaces 3.4's Movies.h.

- **What it answers.**
  - Files: OpenMovieFile/CloseMovieFile, NewMovieFromFile (a `'moov'`
    resource, or the atom in the data fork: resId -1; QuickTime 3's
    compressed `'cmov'` header inflated by `inflate.cpp`, RFC 1950/1951),
    NewMovieFromDataFork/Handle, NewMovie, CreateMovieFile, DeleteMovieFile,
    AddMovieResource, UpdateMovieResource, FlattenMovie, FlattenMovieData,
    PutMovieIntoHandle.
  - Playing: MoviesTask, UpdateMovie, Start/StopMovie, GoToBeginning/End,
    IsMovieDone, PrerollMovie and LoadMovie/TrackIntoRam (no-ops), active,
    GWorld, box, matrix, clip and display clip, bounds regions, PtInMovie,
    color table, play hints.
  - Time: time scale, duration, time, rate, preferred rate and volume,
    poster, selection, active segment, GetMovieNextInterestingTime; time
    bases (time, value, rate, flags, start, stop).
  - Tracks and media: by index, ID and type, enabled, duration, offset,
    dimensions, matrix, layer, alternates, volume, usage; media scale,
    duration, handler description, samples and descriptions, language,
    quality, MediaTimeToSampleNum and back, GetMediaSample.
  - Editing: NewMovieTrack, NewTrackMedia, Begin/EndMediaEdits,
    AddMediaSample, InsertMediaIntoTrack, DisposeMovieTrack and
    DisposeTrackMedia.
  - User data: GetMovie/TrackUserData, GetUserData, CountUserDataType,
    GetNextUserDataType, GetUserDataText.
  - The rest of Movies.h's 402 selectors pop their arguments and answer
    paramErr or nil, logged (`traps.cpp`), as the ICM's do.
- **How.**
  - A movie is read whole from its `'moov'` atom into host structures:
    tracks with their edit lists, each media's sample tables kept as
    chunks (offset, first sample, count, description). The guest's Movie,
    Track, Media, TimeBase and UserData are 8-byte blocks holding the ID.
  - Data references: the movie's own file (`'alis'`, flag 1), another
    file through ResolveAlias, or memory for media made without a file.
    Each file has its own read-only path. AddMediaSample appends to the
    file the media writes to.
  - A time base is a rate and a time at an instant of Executor's
    millisecond clock, worked out when asked; looping time bases wrap.
  - MoviesTask, for each enabled visual track at the movie's time: edit
    list, then media time, then sample. The ICM decodes the sample (a
    decompression sequence per track) into the track's own offscreen at
    the destination's depth. Temporal codecs keep their previous frame
    there; a seek decodes from the sync sample before. CopyBits takes the
    offscreen to the movie's port through the track's and the movie's
    matrices (layer order, srcCopy/ditherCopy as srcCopy, the display
    clip).
  - FlattenMovieData writes `'moov'` then one `'mdat'` (or the movie as a
    resource), the chunks copied, data references self-contained.
- **QTTest** (ctest `qt_executor_761`) against the Quadra's 7.6.1 with
  QuickTime 2.5: 219 of 235 lines identical, 64 of 64 frames the movies'
  own pixels (`raw `/`rle ` at 32 and 16 bits, the two-track movie, the
  flattened copy). The movies QTTest makes in Cinepak, Video and
  Animation decode identically too, as does play.* (done in time).
  The rest is listed in `tests/guest/qt/known_executor_761.txt`:
  - `smc ` (dithering, as in QTICM) and Executor's own picture recording;
  - Photo-JPEG's first two frames (on the Quadra they come out one of two
    ways);
  - the movie controller (B4).
  Movie sound since B5 (section 12): the cell checks QTTest's tones in
  `--audio-dump`.
- **Movies from our disks** (ctest `qt_disk_executor_761`, run_qt.sh
  `--disk`): Ares' Part 1-3 (palette Cinepak, 320x160, 11 kHz sound),
  HyperCard 2.4.1's Simple Movie (smc 8-bit with its own colors, `twos`
  sound, a sprite track) and 7.6.1's Extensions demos (rle 4-bit), 8
  frames each at fixed times and a play, against the Quadra: all 80 lines
  identical, timing to the tick. 8.6's Sample Movie, Simple Movie behind a
  `'cmov'` header that QuickTime 2.5 refuses, draws hc-simple's frames
  (`compare.py --same-frames`). `disk_movies.sh` takes them out of the
  images once into `~/storage/qt/disk-movies`; the cells skip without them.
  Two ICM fixes made the 8-bit movies right: `$49`'s translation (above:
  Cinepak called it with nil flags and gave up every frame; smc's fills
  took the wrong layout), and the `$AAAA` dispatcher matching selectors
  without their high bit (`$821D` from Apple's components is
  QTFindChildByID). The private `$273` (a pointer it fills) and `$274`
  (that value and a Boolean), both OSErr, are sized from their call sites
  in QuickTime 2.5's tune player and answer paramErr.
- **Found on the way** (the ICM, 88e92123): a codec drawing directly into
  a port set its colors under the screen's 8-bit device. A 32-bit GWorld
  got white's CLUT index (0) as its background, and CopyBits into it
  colorized white to black. The ICM now makes the destination (a GWorld
  with its own device) current around the band.
- **Open.**
  - Sound tracks: section 12. The movie controller is section 10.
  - Time base callbacks (NewCallBack, CallMeWhen): none of the surveyed
    applications calls them. A time base runs on Executor's millisecond
    clock, the one TickCount and the Time Manager use.
  - QuickTimeLib exports for PowerPC (EV Nova).
  - GetMovieNextInterestingTime is simplified.
  - Movie clip regions apply only as the display clip.
  - Alternate groups pick their first enabled track, not by language or
    quality.


## 10. Route B: Executor's movie controller (qt-b4, 2026-11-08)

`mc.cpp`, the `'play'` component in C++ (registered before the System's
components, so Apple's `'play'` thng is left out). NewMovieController and 44
MC calls are declared in `QuickTime.yaml` under a component dispatcher
(`MovieController`), as Executor's sound output component is.

- **What it answers.** NewMovieController (top left, centred, or scaled
  down to fit, its proportions kept; `mcNotVisible`, `mcWithBadge`),
  DisposeMovieController; MCSetMovie, MCNewAttachedController, MCGetIndMovie,
  MCRemoveAMovie/AllMovies; MCIsPlayerEvent, MCIdle, MCKey, MCClick, MCDraw,
  MCActivate, MCInvalidate, MCAdjustCursor; MCDoAction (play, preroll and
  play, go to time, volume, step, looping, palindrome, selection, keys, play
  selection, badge, flags, play every frame, rate, time slider rectangle,
  drag, cursor setting, color table, and the notices it ignores);
  MCSetActionFilter and MCSetActionFilterWithRefCon (every action goes to the
  filter first, MCDoAction's too); attached, visible, port, bounds rectangle
  and region, window region, MCPositionController, MCMovieChanged,
  MCSetDuration, MCGetCurrentTime, MCGetControllerInfo, MCSetClip/MCGetClip,
  MCDrawBadge, MCPtInController. Movie Toolbox: HasMovieChanged and
  ClearMovieChanged (track edits; MoviePlayer asks before closing).
- **The bar.** 16 pixels under the movie, as QuickTime 2.5's: a speaker
  (a click mutes and unmutes), play/pause, the time slider (dragged: the
  movie follows), steps back and forward (held: they repeat);
  `mcFlagSuppressSpeakerButton`/`StepButtons` take theirs away; grey when
  inactive. Executor's own drawing, not Apple's pictures.
- **Events.** A null event idles the movie (MoviesTask; at the end it stops,
  loops or turns back). An update draws the movie and the bar and again at
  the next idle (after the application's own update), and isn't consumed. A
  click in the bar or the movie is (the movie: play/pause). Keys only with
  `mcActionSetKeysEnabled`: space or return play/pause, arrows step and
  change the volume.
- **Not done.** Editing (cut, copy, paste, clear, undo): MCSetUpEditMenu
  leaves those items disabled, "Can't Undo". The badge is a small film frame.
- **Tests.** QTTest's `mc.*` lines (`qt_executor_761`) and its component
  list now match the Quadra's (`'play'` described as QuickTime 2.5's: subtype
  `"\0std"`, cmpFastDispatch; Executor enters its native components as
  native whatever their flags). `qt_apps_executor_761`
  (`tests/test_qt_apps_executor.sh`): 7.6.1's Demo click&drag opened in
  MoviePlayer 2.5 and SimpleText 1.4, the movie and the bar drawn, a click on
  play plays it. `/api/ui` gets no answer while MoviePlayer is in front, so
  the cell finds the movie by the bar's frame in the screenshot.
- **HyperCard.** `qt_apps_executor_761` also has HyperCard 2.4.1 play
  Simple Movie: the QuickTime Tools stack open on a writable copy of
  `dev-tools-system-7.img`, a HyperTalk `movie "Host:Simple Movie",
  "windoid","200,120"` over `/api/script`, a click on play. The Movie XCMD
  reads its settings with GetUserDataItem (`$126`).

## 11. Route B: PowerPC, and QuickTime 3.0.2's codecs on 8.6 (qt-b4, 2026-11-08)

- **QuickTimeLib.** `library: QuickTimeLib` on the Movie Toolbox and movie
  controller declarations (`QuickTime.yaml`) and on the ICM calls Executor
  answers (`ImageCompression.yaml`), as PPCLibs.md did StdCLib. The ICM's
  calls are `export: false`, not InterfaceLib's: the generator gives such a
  subtrap with a `library:` to that library alone (`PASCAL_SUBTRAP_LIB`).
  CFM connects Executor's QuickTimeLib before the QuickTime™ PowerPlug in
  8.6's Extensions (Apple's PowerPC QuickTime, glue that needs the INIT),
  kFindLib too, once Executor's Movie Toolbox is there. QTTest's 52
  QuickTimeLib imports and EV Nova's 22 all resolve.
- **8.6's components are QuickTime 3.0.2's.** Gestalt `'qtim'` is now the
  QuickTime™ file's `'vers'` 1 where its components are registered:
  `03028000` on 8.6, as the G3 answers, `02508000` on 7.6.1. The
  PowerPlug's components are left out: native duplicates of the 68k codecs
  (the G3 lists each codec once) that import QuickTimeLib routines Executor
  lacks (ImageCodecBusy, the remap tables, ICMDecompressComplete) and
  CallComponentFunctionWithStorageProcInfo. The 68k codecs run through
  Mixed Mode.
- **QuickTime 3's 68k codecs delegate.** A codec answers few selectors
  itself; its dispatcher hands the rest to its delegate, the `'base'`
  codec, with `move.l header,-(sp); move.l delegate,-(sp); moveq #-2,d0;
  _ComponentDispatch`, reading the delegate from its globals through A0.
  So a fast-dispatch component is entered with A0 pointing at the storage
  in its frame, and ComponentDispatch -2 makes the call to the delegate on
  the codec's own frame and returns to the codec's caller with the result
  in its slot (`component.cpp` `delegate_fast`); only when the instance
  isn't one does it return to the codec, which then answers
  badComponentSelector. The base codec drives the codec through QuickTime
  3's band calls (selectors `$200`-`$204`) and calls QTGetPixelSize
  (`$AAA3 $86`), ICMSequenceLockBits/UnlockBits (`$7C`/`$7D`, noErr) and a
  private Movie Toolbox `$203` (one pointer, its result dropped; sized and
  answered paramErr).
- **QTTest's PowerPC code on Executor 8.6** (`run_qt.sh rom_ppc_86
  --needs-qt --no-audio -- --backend executor --executor-ppc --image 8.6`):
  raw32, raw16, rle32, rle16, av and the flattened movie draw the movies'
  own pixels, `icm.raw_` and `icm.rle_` as the G3; 86 of the first 87 lines
  besides the component lists are the G3's (`gestalt.icmp` is `12`, the
  G3's ICM `13`). The component lists differ: the 68k codecs carry
  cmpFastDispatch (`$40000000`) where the G3's PowerPC ones don't, and
  Executor has 49 decompressors to its 72.
- **ICMSequenceLockBits** (`$7C`) gives the codec the PixMap the band is
  decompressed into: the base codec takes its codec's band address from
  its baseAddr, bounds, rowBytes and pixelSize. Answered noErr with the
  PixMap left as it was (zeros), rpza decoded its frame from address 0
  over the trap table and the next SwapMMUMode jumped into a pixel. Now
  rpza, cvid and Photo-JPEG decompress, with exactly the pixels the
  Quadra's 68k codecs give on 7.6.1: the G3 runs the PowerPlug's PowerPC
  codecs, which round differently, so those CRCs are known to differ
  (`ctest qt_executor_ppc_86`, `tests/guest/qt/known_executor_ppc_86.txt`).
- **pixelFormat.** QuickTime 3 reads a pixel map's format from its
  planeBytes field (Universal Interfaces 3.x's `pixelFormat`). 8.6's smc
  decompressor checks it on a first frame (conditionFlags `$A0C`) and
  answers codecErr unless it is 8, 16 or 32; the ICM gave it 0, so the
  first band of every sequence failed (and the frame after it decoded). The
  ICM's copies of the destination and the compressor's source carry the
  pixel size there when it is 0 (`set_pixel_format`); smc decodes, its
  pixels the 7.6.1 difference (an undithered 8-bit source).
- **MoviesTask's error is its own.** QuickTime 3's base codec asks
  `GetTimeBaseTime` of a nil time base on each frame (paramErr), and that
  stayed the Movie Toolbox's error after MoviesTask: the Photo-JPEG movie
  drew every frame and answered -50. MoviesTask sets its own result, the
  first frame that failed or noErr.
- `compare.py --known`: a known line the reference passes must still pass
  (its detail may differ; `NAME may-fail` lets it fail), so a known codec
  difference can't hide a codec failure.
- Loading a component's code (or GetComponentResource) no longer closes
  its file when it was open already: QuickTime 3's codecs share the
  QuickTime file, and closing it under a codec's OpenComponentResFile
  freed what that codec had loaded.
- **EV Nova 1.0.10** (`games-ppc`, 8.6, `--executor-ppc`, `--ram 128`: it
  asks for a 51 MB partition) passes its QuickTime 3 check, opens Nova Music
  (an MP3: NewMovieFromFile answers -2048, no MPEG importer) and reaches its
  title screen. It had crashed in its picture reader: it builds its own
  pixmaps over handles (pmVersion 2) and GetPixBaseAddr gave the handle, so
  a 16-bit picture was decoded over the master pointers (fixed in
  `qGWorld.cpp`; gtest `QuickDraw.PixBaseAddrOfHandlePixMap`). Parts of the
  title (the logo band, the centre panel) turn to noise a few seconds in.
  Not QuickTime: the title makes no ICM call at all (its pictures are its
  own PICT reader's, 817 GetPicture), only CopyBits between its GWorlds
  (367,000 in 40 s); still noise with ICMSequenceLockBits fixed. On 7.6.1
  it quits (QuickTime 2.5), so there is no Executor reference; the G3's 8.6
  runs at a size and depth EV Nova refuses. Its movies (`Race N.mov`,
  Cinepak) come later.

## 12. Route B: movie sound (qt-b5, 2026-11-08)

`movies.cpp` (`sound_task`), `sound/sounddecode.cpp`, Executor's Sound
Manager (`sound/sound.cpp`). From Inside Macintosh: Sound and QuickTime
(sound sample descriptions, the Sound Manager's commands) and QTTest's
tones measured on the Quadra.

- **How a sound track plays.** Each sound track gets a Sound Manager
  channel of its own (`SndNewChannel`, sampledSynth, in the System heap
  with its buffers). From the movie's time on, its samples are decoded to
  the mixer's own form (8-bit offset-binary mono: Executor's mixer mixes
  nothing else) in buffers of an eighth of a second, a `bufferCmd` with a
  standard sound header each, kept half a second ahead by MoviesTask. The
  sample rate is the description's times the movie's rate (and an edit's).
  Edit lists are followed (an empty edit is silence: the Demos' clicks sit
  behind one), so are the time base's stop and loop flags; a palindrome
  plays its forward half only.
- **Sync.** The movie's clock is its time base on Executor's millisecond
  clock, which paces the mixer too (a Time Manager task each 46 ms). A
  change of rate, time or flags (StartMovie, StopMovie, SetMovieRate,
  SetMovieTime*, GoTo*, SetMovieActive, the time base calls) flushes the
  channel and starts again from the new time, at once (`TimeBaseS::changed`).
  When the sound falls more than 100 ms behind the clock (the mixer runs at
  interrupt time), the clock is set back to the sound's (`slip`), as
  QuickTime's sound clock would hold the movie; none of the cells needed
  it. IsMovieDone waits, a second at most, for the queued sound to be
  played: a movie ends when its sound has.
- **Volume.** The movie's volume times the track's (either negative:
  muted) through a new `volumeCmd` (46, right << 16 | left, $0100 full;
  `getVolumeCmd` 47), which Executor's mixer now applies to sampled sound,
  rounded (truncated, Ares's fades at volume $FF were silent). SetMovieVolume,
  SetTrackVolume and SetTrackEnabled take effect at once. The movie
  controller's speaker and arrow keys go through it.
- **Formats** (Executor's own decoders, `sounddecode.cpp`): `raw `, `twos`,
  `sowt`, `NONE` at 8 or 16 bits, any number of channels (their mean);
  `ulaw` and `alaw` (ITU-T G.711); `ima4` (IMA ADPCM, the IMA's tables, in
  Apple's 34-byte packets of 64 frames, a packet a channel). Why not Apple's
  `'sdec'` components (the System file and QuickTime™ have them): Executor's
  Sound Manager isn't built of sound components, and an `'sdec'` pulls its
  input from a source component through SoundComponentGetSourceData, a
  chain Executor would have to provide for one decoder; IMA is a few dozen
  lines. **MACE** (`MAC3`, `MAC6`) is not decoded: there is no published
  description of it to write a decoder from (only other programs' code),
  and none of the movies we have uses it. Such a track is logged
  (`[unimplemented] sound media 'MAC3'`) and keeps time silently.
- **The movie controller.** MoviePlayer installs an action filter, and the
  speaker's mute (mcActionSetVolume with a negative volume in the pointer)
  aborted Executor: a value in MCDoAction's pointer that is no guest
  address (`-$FF`) can't go through `ptr_to_longint`. `value_of` now
  inverts the address windows without the range check, and the filter gets
  the long as it is.
- **Tests.**
  - `qt_executor_761` and `qt_executor_ppc_86` run with `--audio-dump`:
    QTTest's tones (1000 Hz 2 s, 440 Hz 1 s, av.mov's 1000 Hz 2 s) at the
    right frequency and length, play.* done in time. Executor: 1000 Hz
    1.95 s, 440 Hz 0.95 s, 1000 Hz 1.95 s (50 ms windows; 2.000 / 1.000 /
    2.000 s at 10 ms), 122/61/121 ticks for 120/60/120 on 7.6.1,
    122/61/122 on 8.6. The Quadra: 995 Hz (its emulated output runs 0.5%
    slow), 121/61/121; the G3: 1000 Hz, 121/60/120.
  - `qt_disk_*_761` keep the dump (`compare.py --audio-heard`: at least
    5 s heard with Simple Movie among the movies). Against the Quadra's
    dump: Simple Movie 6.18 s of sound on both, the same spectral peaks
    (84, 132, 199, 332 Hz), envelope correlation 0.91; the Ares chapters'
    peaks the same, 1.90 / 2.05 / 2.00 s heard of their 2 s plays (the
    Quadra 1.90 / 2.05 / 2.05), the fade-ins window for window; the Demos'
    clicks at their edits' times (the Quadra drops one of the seven, a
    different one each run); 8.6's Sample Movie as Simple Movie.
  - `qt_apps_executor_761` (`--audio-dump`, behind the web server): the
    sound heard while MoviePlayer, SimpleText and HyperCard (Simple Movie)
    play; MoviePlayer's speaker mutes it (played again, 2.4 s out and
    nothing heard) and unmutes it.
  - gtest `SoundDecode.*`: the formats, worked out by hand from their
    descriptions.
- **Open.** MACE. Sound at other rates plays at that pitch (QuickTime 2.5
  does too); backwards, silence. The mixer is 8-bit mono, so 16-bit and
  stereo sound lose their low bits and their stereo: balance (SetSoundMediaBalance)
  is kept, not applied. Time base callbacks still none.

