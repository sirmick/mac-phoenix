# QuickTime 2.5 / 3.0.2 INIT: what it patches, and what an emulated Toolbox must match

Static analysis (by the qt-survey, 2026-11-08; scratch dumps in ~/storage/tmp/qt-survey/qt3/analysis) of `qt25.bin` (QuickTime™ 2.5, the 7.6.1 image) and `qt302.bin`
(QuickTime™ 3.0.2, the 8.6 image), against the traced installs on a Quadra 650
running 7.6.1 (`patches-25.txt`, `patches-302.txt`). Every resource was dumped to
`25/` and `302/` here (`TYPE_ID.bin`, disassembly `TYPE_ID.s`, made by `dump.py` and
`dis.py`; `lpch.py`/`lpch2.py` decode the linked patches). Offsets are into the
resource as stored (none of these resources is compressed). "(unsure)" marks guesses.

Notes on the trace files:

* 3.0.2's "by 'code' -19103 +XX" is a misattribution: the SetTrapAddress
  instructions are in `ptch -19131`'s own installer, at **+0x5a0+XX** (+4a ->
  0x5ea OpenCPort, +66 -> 0x606 QDExtensions, ..., +1da -> 0x77a UnloadScrap).
* **UnloadScrap's new routine is `ptch -19131 +578` (3.0.2), `+4f0` (2.5)**. 2.5
  installs it too (`ptch -19131 +6c4`), but the 2.5 trace has no row for it.
* The "PenPat" rows (ptch -19131 +8a6 / +95c) are the copy of QuickTime's
  replacement **MoveHLow** ($A09D) code. It is installed only when Gestalt
  'heap' fails, and on 7.6.1 it was not.
* The task's `$E00+$2AA*4` is **$18A8**, not $16A8. INIT 0 writes `$18A8`
  itself (see 2c).

## 1. Patched traps

ICM = Image Compression Manager (`ptch -19135`, behind $AAA3 _CodecDispatch).
"ICM n" means a call through the trap-table slot `jsr ($188C)` with D0=n. QuickTime
does not trap there, it calls the routine in the slot directly.
"Executor" column: whether Executor's own C++ already does what the patch *adds*.
The base trap is implemented in every case unless the row says otherwise.

### 1a. QuickTime 2.5 on 7.6.1

| Trap | Patch routine | What it does | Class | Executor native equivalent |
|---|---|---|---|---|
| $ABC2 (QuickTime-private) | ptch -19070 +0 (inst -19070) | A selector dispatcher (2.5: 38 selectors; 3.0.2: 39). QuickTime's 64-bit/fixed math: wide shifts, multiplies, divides; sel 30 builds a mapping from src/dst rects (used by CopyDeepMask). Installed only if $ABC2 is unimplemented. 3.0.2 on PowerPC uses 'npch' -19070 ("QTMathPPC"/mathDispatchPatchRD) through CFM instead. | MOVIES | no ($ABC2 unknown to Executor; QT installs its own, fine) |
| $AAA3 _CodecDispatch | ptch -19135 +0 (inst -19134 +aa) | The ICM itself. It dispatches 121 selectors through QuickTime's segment loader at $DF0 (`($DF0)+8`). CodecManagerVersion (sel 0) answers 18 (2.5) or 19 (3.0.2). Its globals are a 354-byte system-heap block stored at **($28A)+8** (the Component Manager's globals, see 4). It is installed only if $AAA3 is unimplemented or the ICM there is older than 18. | MOVIES | no (Executor has no $AAA3; Gestalt 'icmp' 17 is compiled out) |
| $ABF8 StdOpcodeProc | ptch -19134 +0 | If `$D6E`==0 and the opcode is $8200 (CompressedQuickTime) or $8202 → ICM -84 (decompress the picture's compressed image); $8201 (UncompressedQuickTime) → ICM -85. Only if the ICM's version is exactly 18 (2.5) or 19 (3.0.2). Otherwise it calls the previous StdOpcodeProc. | SIDE (compressed PICT) | **no**: Executor has no $ABF8. Its DrawPicture (qPicstuff.cpp) skips opcodes ≥$8100 by their length and never calls an opcodeProc, so with Apple's QT on Executor this patch is never reached and QT-compressed PICTs draw blank. |
| $AA4E SetStdCProcs | ptch -19133 +0 | Calls the original, then fills `cProcs->newProc1` (+56) with ICM -1(32): QuickTime's **StdPix** bottleneck. | SIDE (StdPix for compressed pixmaps/pictures; movies drawing into ports with custom procs may use it, unsure) | no: qIMV.cpp sets newProc1 to nil and never calls it |
| $AA51 CopyDeepMask | ptch -19132 +0 / +8 | It has four entries (D0 0..3). It uses +0 when Gestalt 'qd  '<$240 and System ≥7, otherwise +8. It maps src→dst with $ABC2 sel 30. If the port's grafProcs->newProc1 (StdPix) is set and is not the standard one, it routes the call there, so it can be recorded in pictures and bottlenecked. Otherwise it runs its own masked copy; with no old CopyDeepMask, saved at +16, it does everything itself. inst -19134 also stores entry+4 at `(($28A)+8)+$7C`. | SIDE (deep masks, StdPix) | partial: C_CopyDeepMask is CopyMask ("poorly implemented"), no StdPix routing |
| $AA00 OpenCPort | ptch -19131 +4b6 | Calls the original, then `_ABC0` selector 9 with the new port's grafVars handle (port+8). $ABC0 is a 7.x trap, unsure which. Installed if System ≥7 and 'qd  '<$242. | BOOKKEEPING (unsure) | no: Executor has no $ABC0 |
| $AB1D QDExtensions | ptch -19131 +1e4 | Only NewGWorld (sel 0, clears *offscreenGWorld first) and UpdateGWorld (sel 3). Maps pixelDepth 24 → 32. After a successful call, if the new GWorld's pixmap is direct (pixelType 16), it shrinks the GWorld device's inverse table to 4 bytes and zeroes it: `grafVars^^+26` = device handle, `device^^+6` = gdITable. Installed if 'qd  '<$242. | BOOKKEEPING (QD fix: depth 24 and memory) | no. **Hazard**: Executor's GrafVars is 26 bytes (cquick.h) and keeps GWorld state in a side table, so QT would read past the block and SetHandleSize a garbage handle |
| $A8F6 DrawPicture | ptch -19131 +1b2 | `clr.w $D6E`, then jumps to the original: it re-enables StdOpcodeProc's decompression for each picture. Installed if 'qd  '<$235. | SIDE (compressed PICT) | no (see StdOpcodeProc) |
| $AA1C SetCCursor | ptch -19131 +a8 | If called at VBL time (VBLQueue $160 qFlags bit 6, inVBL) with a cursor of type $8000/$8001 and hotspot <16, it does `_SetCursor` on the 1-bit image instead (data = mask when crsr1Data is empty). This allows colour cursor changes from interrupt tasks. Otherwise it calls the original. | SIDE (cursor) | n/a: Executor's SetCCursor is not interrupt-sensitive |
| $AA2E InitGDevice | ptch -19131 +11a | ICM -95 with the device's gdRect, then the original. The ICM is told a screen changed (direct-to-screen decompression). 3.0.2 also loads QuickTime segment 40 around the call through `($DF0)+12` (D0=-4 / -5). | MOVIES | no |
| $A91B MoveWindow | ptch -19131 +142 | Calls the original. Then, if `(($28A)+8)+9` (ICM "watch windows" flag) is set, ICM -99 with the window: movies drawing to the screen follow the window. | MOVIES | no. Executor's DragWindow etc. call C_MoveWindow from C++, so the patch would not see those moves (unsure: depends on trap routing) |
| $A83A ZoomWindow | ptch -19131 +17c | Same as MoveWindow: original, then ICM -99. | MOVIES | no |
| $A063 MaxApplZone | ptch -19131 +4d2 | If ApplLimit($130) − HeapEnd($114) < 20, it clears MemErr and returns without calling MaxApplZone; otherwise it calls through. | BOOKKEEPING | no (harmless) |
| $A9EA Pack3 | ptch -19131 +2a4 | Pack3 selector 9 → pops the selector and calls ICM $43 (**StandardGetFilePreview**). Every other selector goes to the original. | SIDE (Standard File preview) | no: Executor's Pack3 is the System's PACK 3 (stdfilepkg.cpp) with no selector 9, and there are no SFGetFilePreview/CustomGetFilePreview (CodecDispatch $41-$44) |
| $A86E InitGraf | ptch -19131 +348 | At every InitGraf it looks in the system heap's grow-zone proc (`SysZone->gzProc`, +$DE or +$DE+$2B0) for a `_CompactMem` ($A04C). It replaces that with `jsr` to its own purger (+37e, or the 12-byte-header variant +7c0 copied over it when `SysZone+30` heapType≠0). The purger walks system-heap blocks itself, honours ExpandMem+$1AE, and empties purgeable handles. Then the original runs. Installed if System ≥7 and Gestalt 'heap' fails. | BOOKKEEPING (memory) | no; would patch whatever Executor has as gzProc |
| $A00A PBOpenRF | code -19103 +ad0 | Calls the original, saved at QT globals `($DE8)+12`. On opWrErr (-49) with write permission asked (ioPermssn bit 1), it calls $AAAA $8095(pb). That is QuickTime-private, presumably to release QT's own path to the file (unsure). | SIDE (file sharing with open movies) | no |
| $A823 AliasDispatch | code -19103 +b04 | MatchAlias (sel 5) only. If WWExist ($8F2) ≠ 0 it forces kARMNoUI. It calls the original; on dirNFErr/nsvErr/nsDrvErr without kARMSearch(bit 3) it retries through $AAAA $80E9, QT's own volume search for data-reference aliases. | SIDE (alias data refs) | no (C_MatchAlias, alias.cpp) |
| $A00F PBMountVol | code -19103 +b8c | Calls the original; on success $AAAA $81E5(ioVRefNum): QT learns of the new volume. | SIDE (volume tracking) | no. Executor's mounts do go through _MountVol, so the patch would see them |
| $A00E PBUnmountVol | code -19103 +bb8 | Gets the volume (GetVolInfo) and loads QT segment 69 (`($DF0)+12`, -4). It calls the original; on success $AAAA $81EE(vRefNum), then unloads segment 69 (-5). | SIDE (volume tracking) | no |
| $A9FA UnloadScrap | ptch -19131 +4f0 (no trace row) | If ScrapHandle ($964) is nil it returns noErr without calling through; otherwise the original runs. | SIDE (scrap) | partial: Executor's UnloadScrap writes only when ScrapState>0 |

Installed by 2.5 but not as traps: JShowCursor ($804) → +66 and JShieldCursor
($808) → +4 (see 4); SetApplLimit through a direct trap-table write; MoveHLow in
place. The JShowCursor hook is only installed when 'sysa' is absent or 68k (1).
The JShieldCursor hook is only installed when 'qd  '<$235.

### 1b. QuickTime 3.0.2 on 7.6.1

Same routines, moved within `ptch -19131` (3.0.2's is 3154 bytes: +2e
before +1e0, more after). Differences from 2.5 are in bold.

| Trap | Patch routine | What it does | Class | Executor native equivalent |
|---|---|---|---|---|
| **$A82A ComponentDispatch** | ptch -19071 +0 (inst -19071 +3e4) | QuickTime's own Component Manager, 4288 bytes. Its globals pointer is at **$28A**. **It replaced the System's (gpch 667 +804) because 3.0.2 keeps an existing CM only if Gestalt 'cpnt' ≥ 7, and 2.5 only if ≥ 6.** So the 7.6.1 value is 6 (inferred). | MOVIES (components) | yes (component.cpp, C++). Executor answers 'cpnt' $00030000, so both QTs would **keep** Executor's |
| $ABC2 | ptch -19070 +0 (inst -19070 +50) | Math dispatcher as in 2.5 (39 selectors). **inst -19070 takes the PowerPC 'npch' path when 'sysa'=2.** | MOVIES | no |
| $AAA3 | ptch -19135 +0 | ICM, version **19** | MOVIES | no |
| $ABF8 StdOpcodeProc | ptch -19134 +0 | As 2.5, ICM version check **19** | SIDE | no |
| $AA4E SetStdCProcs | ptch -19133 +0 | Identical to 2.5 | SIDE | no |
| $AA51 CopyDeepMask | ptch -19132 +0 | As 2.5 (minor register changes) | SIDE | partial |
| $AA00 OpenCPort | ptch -19131 +53e | As 2.5 (`_ABC0` sel 9) | BOOKKEEPING (unsure) | no |
| $AB1D QDExtensions | ptch -19131 +212 | As 2.5 (depth 24→32, direct-GWorld gdITable shrink) | BOOKKEEPING | no (same hazard) |
| **$A8E4 SectRgn** | ptch -19131 +4e4 | `clr.w MemErr($220)`, then the original | BOOKKEEPING (QD fix) | n/a |
| $A8F6 DrawPicture | ptch -19131 +1e0 | `clr.w $D6E`, then the original ('qd  '<$235) | SIDE | no |
| **$A093 Microseconds** | ptch -19131 +4f8 | Calls the original. It keeps the last value (at +4f0) and, if the new one is not greater, returns last+1: a strictly increasing Microseconds. | MOVIES (timing) | partial: Executor's Microseconds is the host clock (monotonic, but two calls can return the same value) |
| $AA1C SetCCursor | ptch -19131 +a8 | As 2.5 | SIDE | n/a |
| $AA2E InitGDevice | ptch -19131 +11a | As 2.5, **plus loading segment 40 around it (`($DF0)+12`, -4/-5)** | MOVIES | no |
| $A91B MoveWindow | ptch -19131 +170 | As 2.5 | MOVIES | no |
| $A83A ZoomWindow | ptch -19131 +1aa | As 2.5 | MOVIES | no |
| $A063 MaxApplZone | ptch -19131 +55a | As 2.5 | BOOKKEEPING | no |
| $A9FA UnloadScrap | **ptch -19131 +578** (installer +77a) | nil ScrapHandle → return noErr; otherwise the original | SIDE | partial |
| $A9EA Pack3 | ptch -19131 +2d2 | sel 9 → ICM $43 StandardGetFilePreview | SIDE | no |
| $A86E InitGraf | ptch -19131 +376 | As 2.5 (gzProc CompactMem hook) | BOOKKEEPING | no |
| $A00A PBOpenRF | code -19103 +c44 | As 2.5 | SIDE | no |
| $A823 AliasDispatch | code -19103 +c78 | As 2.5 | SIDE | no |
| $A00F PBMountVol | code -19103 +d00 | As 2.5 | SIDE | no |
| $A00E PBUnmountVol | code -19103 +d2c | As 2.5 | SIDE | no |
| **$A060 FSDispatch** | pboy -19064 +56 (copied to the system heap; installed through 'fttl' -19071) | Only sel 9 (PBGetCatInfo). After success, if the file type is 0/'TEXT'/'????'/'DATA'/'BINA'/'BIN '/'bin ' and the name ends in a 1–4 character extension, it lower-cases the extension and looks it up in the `($DE8)+24` table (the detached **'fttl' -19071**: 'avi'→'VfW '/'TVOD', 'gif'→'GIFf'/'ogle', 'jpg'→'JPEG'/'ogle', 'aif', 'dv', 'flc', ...). Then it rewrites ioFlFndrInfo type/creator. | SIDE (PC-file typing by extension) | no |
| **$A00C PBGetFInfo** | pboy -19064 +88 | Same extension→type/creator mapping for GetFInfo | SIDE | no |

The FSDispatch and PBGetFInfo pair is installed only when QuickTime's 'ftmp'
preference allows it. INIT +484..+4fc asks $AAAA $82D4/$821D/$8216 for it, and it
is on unless the pref's first byte is 0.

## 2. The INIT's structure

### 2a. INIT 0 (2.5: 2664 bytes; 3.0.2: 2864 bytes, same plan)

Startup checks:

* **Old System.** SysEnvirons; System ≤ $600 → error.
* **System 7.** fp-43 = (System ≥ $700).
* **A/UX.** HWCfgFlags ($B22) bit 9, hwCbAUX, sets fp-44. Under A/UX, inst -19068
  and inst -19069 are skipped.
* **ROM85.** `ROM85($28E) & $C000` ≠ 0 → error 9 (no Color QuickDraw).
* **QDExtensions.** $AB1D unimplemented (compared against $009F) → error 3
  (needs 32-bit QD).
* **Gestalt.** Gestalt itself unimplemented → error 4.
* **Already installed.** Gestalt 'qtim' exists → error 2.
* **Flags.** 'sysa'=2 → PowerPC flag. Gestalt 'bugx' → bug-fix flags (fp-4).

Errors draw the crossed icon and post a Notification Manager alert from
STR# -19067.

Then:

1. **System 6 only** (System < 7.0): inst -19068 (linked-patch loader, not under
   A/UX), inst -19067, inst -19066.
2. inst -19064 (HeapDispatch, if unimplemented).
3. inst -19101 (2.5 INIT +716; QuickTime's segment loader: 'code' -19102 → `$DF0`).
4. The 'pboy' bug-fix bodies. Each is detached, called with its own handle, and
   most are disposed afterwards:
   * pboy -19071: $ABF5 is implemented ≠ $A89F → keep MemErr across $ABF5.
   * pboy -19070: Sound Manager version (SoundDispatch $000C0008) is 3.0.0.8000 ≤ v < 3.1
     → SndNewChannel ($A807) patch. On memFullErr/-2004 it retries with
     ExpandMem+$1AE set to $800.
   * pboy -19069: Gestalt 'powr' bit 0 → Power Manager (Sleep/PowerMgrDispatch) fix.
   * 'pbok' -19068 (PPC, 'bugx' bit 13 clear): a PowerPC fragment through
     CFM `GetMemFragment` (CodeFragmentDispatch 3).
   * pboy -19067 (PPC, System ≥ $752, 'bugx' bit 14 clear).
   * **3.0.2 only:** pboy -19065 when the System is exactly $0760 (a 7.6.0 fix that loads
     a 'pwpc' fragment, unsure).
5. inst -19063 ('pach' ROM fixes, see 2d).
6. inst -19071 (Component Manager, error 5). Then, if FindNextComponent('mhlr','mhlr')
   finds nothing, RegisterComponentResourceFile(CurResFile, 1). 3.0.2 registers with
   flags 9 when one exists.
7. inst -19070 ($ABC2, error 8).
8. If Gestalt 'icmp' fails: inst -19134 (the ICM installer, error 6; it installs
   $AAA3, $ABF8, $AA4E, $AA51 and runs `ptch -19131`'s installer).
9. inst -19069 (not under A/UX): the Microseconds/Time Manager fixes.
10. 'code' -19103 (INIT +678 in 2.5, +724 in 3.0.2): the **$AAAA dispatcher** and QT's
    globals at `$DE8` (see 2c).
11. inst -19065 (Gestalt 'qtim'/'icmp' functions, from ptch -19067).
12. PPC: 'pbok' -19066 through CFM.
13. **3.0.2 only:** the 'ftmp' preference → pboy -19064 (FSDispatch/GetFInfo); the
    'fttl' -19071 table pointer → `($DE8)+24`.
14. The `$DE8` deferred-extension list. A chain of 'qext' records that other
    extensions queued before QuickTime loaded. Each holds an FSSpec at +12; QuickTime
    opens the file with FSpOpenResFile (HighLevelFSDispatch 13) and runs every
    'INIT' in it. The chain ends at -1/nil or a 'Movy' header. In 3.0.2 the chain is
    handed to QT, `($DE8)+20` and $AAAA $82D3('qtim'), instead.
15. If all went well: inst -19102. It checks KeyMap ($174) for a key chord at boot and
    draws a box with the ICM (an easter egg or diagnostic, unsure).

Installer runner: INIT +63c (2.5) or +6e8 (3.0.2). It does `Get1Resource('inst', id)`,
HLock, `jsr (*h)`, ReleaseResource, and returns ResError.

### 2b. The 'inst' resources

| inst | Installs | When |
|---|---|---|
| -19134 (24 KB / 51 KB) | ICM ptch -19135 → $AAA3, ptch -19134 → $ABF8, ptch -19133 → $AA4E, ptch -19132 → $AA51. Then it calls `ptch -19131` +0, whose installer at +502/+58a installs every patch in 1a/1b, and trims its handle. The rest of the resource is component registration data ('mhlr', 'dhlr', music instruments, ...). | Gestalt 'icmp' absent. Needs ROM85 OK, QDExtensions and ComponentDispatch implemented |
| -19102 | Key-chord check at boot, ICM box drawing (unsure) | Last, if the INIT succeeded |
| -19101 | 'code' -19102 (QuickTime's segment loader) detached and locked; `$DF0` = NewPtr(32); its init sets `($DF0)+8` (call-segment-routine) and `+12` (load/unload segment) | Always |
| -19071 | QuickTime's Component Manager, ptch -19071 → $A82A (see 2e) | Always called |
| -19070 | ptch -19070 (or 'npch' -19070 on PPC) → $ABC2 | $ABC2 unimplemented |
| -19069 | Microseconds ($A193) from its own VIA-timer code. It also writes InsTime/PrimeTime directly into the OS trap table at $560/$568, and sets Gestalt 'bugz' bit 8 | 'bugz' bit 8 clear (and not A/UX). InsTime/PrimeTime only if 'snd ' bit 2 is clear |
| -19068 | The linked-patch loader for the 'lpch' resources (see 3) | System 6 only, not A/UX |
| -19067 | ptch -19069 → $A823 AliasDispatch (an Alias Manager stand-in) | System < 7 (or no Gestalt) |
| -19066 | ptch -19068 → $AA52 HighLevelFSDispatch (FSSpec calls) | System < 7 |
| -19065 | ptch -19067 as the Gestalt 'qtim' and 'icmp' functions (NewGestalt). Or, if $AAA3 and $AAAA are both already implemented and 'vers'≥5, ReplaceGestalt with the values from $AAAA $E4 / CodecDispatch 0. Compares the trap-table slots $188C/$18A8 with $107C (Unimplemented) | After 'code' -19103 |
| -19064 | If $A0A4 HeapDispatch is unimplemented: ptch -19066 (8-byte headers) or ptch -19065 (12-byte headers), chosen by `SysZone+30` → $A0A4. Plus ptch -19064 in front of $AAA2 PaletteDispatch and $A996 RsrcZoneInit (each preceded by `bra.s +6; jmp old`). It calls HeapDispatch 16 with a 10-byte handle | Always called; does nothing on 7.6.1 or Executor (both implement $A0A4) |
| -19063 | 'pach' ROM fixes (see 2d) | 'bugx' bit 11 clear |

### 2c. How $AAAA _QuickTime gets its dispatcher

There is no SetTrapAddress. INIT 0 (+6b8 in 2.5, +764 in 3.0.2) writes
`StripAddress(*'code' -19103)` **directly into the toolbox trap table slot $18A8**
(`$E00 + $2AA*4`). The resource is detached and locked first. Then it calls
the entry at `*h + size - 4` (`jmp` to the init at +dc4 in 2.5). That init:

* allocates the 140-byte QT globals and stores them at **$DE8**, with 'Movy' at +0;
* relocates the dispatch table;
* installs PBOpenRF/AliasDispatch/PBMountVol/PBUnmountVol;
* registers a handle-trash notification, HeapDispatch 6 with {0, 1000, proc +c70}.

The handle is then trimmed (SetHandleSize).

The dispatcher, at 'code' -19103 +0, takes D0 = (param bytes << 16) | selector. 2.5
has $286 selectors, 3.0.2 $2E3, and negative selectors run through a second
range. It clears `($DE8)+4` (the error). An entry of 0 pops the parameters and
returns -1. A positive (segment, routine) entry goes to `($DF0)+8`, QT's segment
loader. A negative entry is a relocated direct address. QuickTime's own code often
calls through `($18A8)` directly instead of trapping.

### 2d. ROM-version code

* **inst -19063** reads `ROMBase($2AE)+8`. $077D (Power Mac) → 'pach' -19071. $0075
  (Plus) or $0276 (SE) → nothing. **Any other value → 'pach' -19070**: it is
  detached, MoveHHi'd, locked, and jumped to.
* **'pach' -19070** (68k):
  * It sets Gestalt 'bugx' bit 11 itself.
  * It installs OS trap **$A0B3** with a File Manager-internal routine. That routine
    uses low memory $340/$346/$37C/$380 and the File Manager vectors kept in the OS
    trap table at $700–$794.
  * On 'proc' ≥ 3 it flushes the code cache over 6 stubs (HWPriv 9).
  * It resolves its ROM-internal addresses from a table keyed on the ROM version:
    $0075, $0276, $0178, **$067C**, $037A, each with offsets such as ROMBase+$4EC6 and
    ROMBase+$3A76.
  * **Unknown ROM version → `clr.l ResumeProc($A8C)`; SysError 99.**
* **'lpch' loader (inst -19068)**: see 3. Unknown ROM → **SysError 98** (dsNoPatch). It
  is not run on System 7, so it does not matter for 7.x/8.x.

### 2e. inst -19071, the Component Manager installer

* $A82A unimplemented → install ptch -19071.
* Gestalt 'cpnt' fails → only RegisterComponentResourceFile.
* 'cpnt' = 1 or 2 (QuickTime 1.x's CM) → walk the old CM's component table at
  `($28A)+28` (entries: type/subtype/manufacturer/flags/mask at +4..+16, +46, +48).
  Remember the registrations and record whether MoveHLow exists at `($28A)+22`. Then
  install ptch -19071 → $A82A and call ComponentDispatch 23 (QT-private: end of
  code, to trim the handle). On System < 7.1 also call sel 34. Finally
  RegisterComponentResourceFile(CurResFile, global).
* **Otherwise: 2.5 keeps the existing CM when 'cpnt' ≥ 6; 3.0.2 when 'cpnt' ≥ 7.**
  Below the threshold:
  * Sub-routines at +a68/+bae/+c5a (3.0.2) look at the existing CM's code size (64, 68
    or 80 bytes; unsure what is measured) and patch it in place: 3.0.2 +77c/+8ce/+a3e,
    2.5 +77a/+8cc.
  * If 'cpnt' < 5 and Gestalt 'mach' ≠ 73, one more fix: 3.0.2 +c2e, 2.5 +a3c.
  * Then QT replaces the CM. That is what happened on 7.6.1 under 3.0.2, so 7.6.1's
    'cpnt' = 6 (inferred from the two thresholds and the traces).
* ptch -19071 keeps its globals at **$28A**. It writes $28A at +eea/+f24 (2.5).

## 3. The 'lpch' resources (the System's linked-patch format)

They are loaded only by inst -19068 (System 6.0.x, not A/UX). The loader:

* reads the ROM version at ROMBase+8 and matches it against a table at +3cc:
  $067C → bit 16 (Universal: IIci/IIfx/Quadra...), $037A → 8 (Portable),
  $0178 → 4 (Mac II), $0276 → 2 (SE), $0075 → 1 (Plus). Unknown → SysError 98.
* Get1Resources every 'lpch' 1..31 whose ID has that bit.
* The lowest ID loaded starts with the number of lpchs. The highest ID (31) starts
  with (ROM-reference count, jump-table entries−1) and holds the install table.
  Then each lpch has:
  * a code size (long), then the code;
  * a ROM-address list: a start slot word, then one 3-byte ROM offset per ROM bit in
    the lpch ID, ended by a high bit;
  * a relocation list (3-byte offsets of in-code reference chains, 0-terminated);
  * a jump-table stream (bytes <$FC: routine lengths in words; $FC n: skip n
    entries; $FD/$FE: ends/markers; `FC 00`: end).
* The install table, in lpch 31 only, is a series of: 3 bytes condition (ROM bits 0–4,
  plus feature bits $FFFE0 set from 'vm  ', CPUFlag $12F, MMUType $CB1, $A05C,
  UTableBase drive flags, ROM release ROMBase+$12 = $12F1, 'addr', Sound
  Manager, $A0DB in ROM), then (skip count, trap word) pairs; `FE` starts a new
  condition and `FF 0000` ends.

| lpch | ROM bits | Size | Content (decoded with lpch.py/lpch2.py; partly unsure) |
|---|---|---|---|
| 1 | Plus | 13 | Empty (count word 2, no code: `0002 00000000 ffff 000000 fc00`) |
| 2 | SE | 13 | Empty |
| 8 | Portable | 13 | Empty |
| 4 | Mac II | 529 | Code $1F6. **The extended Time Manager for System 6**: installs InsTime $A058, RmvTime $A059, PrimeTime $A05A (`moveq #$58..$5A; _SetTrapAddress $A247`), grows the Time Manager globals at **$B30** from 16 to 28 bytes, sets $1A6 (unsure). Jump-table entries 8+ |
| 16 | Universal | 161 | Code $7A. The same Time Manager fix for Universal ROMs: RmvTime $A059, PrimeTime $A05A, $B30 grown to 28 bytes, $1A6. 3 ROM references (ROMBase+$AE96, +$AF3E, +$B02E) |
| 20 | Mac II + Universal | 185 | Code $AA. A Time Manager helper reading VIA1 ($1D4)+$1200 timer registers and $B30 (the drift-free timer arithmetic). No ROM references |
| 31 | all | 175 | Code $56, one routine (jump-table entry 6) installed on trap word **$C0DB**. It is selector-checked (`$00090003` on the stack) and calls 3 ROM routines per ROM (slot 3–5: Universal ROMBase+$11B0C/+$108C6/+$108CE, Mac II +$C0F4/..., SE, Portable, Plus +$51E8/+$3FF2/+$3FFA). The $C0DB encoding (OS trap $A0DB with flag bits?) and its meaning are unsure. The rest of the install table: cond $000004 (Mac II) and $000010 (Universal) each install one more jump-table entry with trap word $0000 (a no-trap/ROM-vector patch, unsure) |

The lpchs therefore give System 6 machines the System 7 extended Time Manager
(and one more fix) that QuickTime needs. They never run on 7.x/8.x.

## 4. What reads low memory, ROM or other managers' internals directly

What an emulated Toolbox (Executor: its own C++ QuickDraw, Memory Manager and
Component Manager, no ROM) would have to match, in order of risk.

### Trap-table slots (Executor: `tooltraptable`/OS table in guest memory)

* **$18A8** ($AAAA): written directly by INIT 0. QT code calls `jsr ($18A8)`.
* **$188C** ($AAA3): ICM entry, called directly everywhere. Compared with **$107C**
  ($A89F Unimplemented) to detect the ICM; $103C ($A88F OSDispatch) likewise.
* **$10A8** (SectRect): called directly by the JShieldCursor hook.
* **$1CA0** ($ABA8). On 'qd  ' < $242, ptch -19131 +502 writes
  `(ptch-19131+266) − ($1CA0)` as a long at **`($1CA0)+8`**: a hook inside
  32-bit QuickDraw's $ABA8 routine. The QuickDraw-internal code at +266/+2bc
  converts 32→16-bit pixels and dithers, using the host routine's frame locals
  fp-165/-552/-681.
  * **Executor answers 'qd  ' = $0230 (gestalt32BitQD13)**, so this runs.
  * $ABA8 is unimplemented in Executor, so `($1CA0)` is Executor's unimplemented-trap
    code and QT overwrites 4 bytes at +8 of it.
  * Fix: give $ABA8 a private ≥12-byte dummy, or answer 'qd  ' ≥ $242.
* **$1660** (GetCTable), System 6 only: patched directly. **$4B4** (SetApplLimit):
  patched directly (call through; on error ApplLimit = HiHeapMark($BAE) =
  ApplZone^ bkLim).
* **$560/$568** (InsTime/PrimeTime): patched directly by inst -19069. They wrap
  TMTasks (qType bit 6, byte +21).

### QuickTime's own low memory

* **$DE8**: QT globals ('Movy', +4 error, +12 old PBOpenRF, +20 qext list, +24 'fttl'
  table, +28/+22 volume tables, +40). Before QT loads it is the 'qext' chain other
  extensions build.
* **$DF0**: segment-loader globals (+4 table, +8 call, +12 load/unload).
* **$D6E**: StdOpcodeProc's "don't decompress" flag (QT-owned, unsure).
* **$D4C**: compared with `(($28A)+8)+10` in the JShowCursor hook (unsure what owns it).
* Executor must leave these zero/free. It does not use them.

### Component Manager globals

* **$28A** is the Component Manager's globals pointer. The ICM stores its globals at
  **`($28A)+8`**. The window/cursor patches test `(($28A)+8)+8/+9/+10`, and
  inst -19134 writes `(($28A)+8)+$7C`.
* inst -19071 reads `($28A)+18`, `+22` and `+28` (the component table) on its
  migration path.
* **Executor never sets $28A** (component.cpp has no such pointer), so with
  Executor's CM kept ('cpnt' $30000 ≥ 7) the ICM would write its globals pointer to
  address 8. Executor would need a guest block at $28A whose +8 is free (≥ 12 bytes).
* Alternatively, answer 'cpnt' < 7 (7.6.1's 6) and QT 3.0.2 replaces Executor's CM
  with Apple's 68k one.

### Memory Manager internals

* **HeapDispatch $A0A4.** Executor implements $A0A4 (selectors $A and $20 only), so
  inst -19064 does not install QuickTime's own. QT then calls the System's selectors:
  * **0–5** (HLock/HUnlock/HPurge/HNoPurge/HGetState/HSetState on the handle in A0).
    These are in inst -19134, ptch -19132, and nearly every codec ('cdec'), media
    handler ('mhlr') and data handler ('dhlr').
  * **6** (register a handle-trash notification, A0 → {long, word priority, proc}).
    Used by code -19103, ptch -19135, ptch -19071 and 'musi'.
  * **8**, **10** (forget/trash a zone, A0), **13/14** (system-heap allocation that
    respects ExpandMem+$1AE, the reserve), **16**.
  * QT's own versions (ptch -19066/-19065) show 0–5, 6/7 (add/remove a trash
    proc), 8/9, 10 (trash zone), 11 (trash range A0–A1), 12 (trash a process,
    OSDispatch), 13 (NewHandleSys with reserve check), 14 (the check), 15
    (NewPtrSys variant), 16/17 (set/get the trash list), partly unsure.
  * **Executor needs 0–5 (trivial), 6–9 (accept), 10 (has), 11–14.** The other choice
    is to leave $A0A4 unimplemented, but 7.6.1's own component code calls $A when it is
    implemented.
* **Block headers and zone layout.**
  * The InitGraf purger (+37e/+7c0) and HeapDispatch 13/14 walk the system heap from
    `SysZone+52`: the first block, with 8- or 12-byte headers chosen by `SysZone+30`
    (heapType). They read tag bits (byte 0 >> 6), master-pointer flags and size
    (header+4), and the relative handle (header+8, or −4 relative to the zone).
  * 'code' -19102 sets and clears the lock bit itself: bit 7 of the master pointer in
    24-bit heaps, of `*h−11` (header+1) in 32-bit heaps.
  * Executor's zone+30 is $FF (maxNRel = -1) and its 12-byte block_header_t is flags,
    master_ptr_flags, reserved, size_correction, size, location. That matches the
    32-bit layout.
  * The gzProc hook looks for `_CompactMem` at gzProc+$DE / +$38E. Executor's
    system zone gzProc is presumably C++ or nil, so it is not found and nothing
    happens (unsure).
* **ExpandMem($2B6)+$1AE** (the system-heap reserve) is read by the purger, HeapDispatch
  13 and the SndNewChannel fix. ExpandMem+$110 → +$22 is the Sound Manager's, read by
  the Mac II VBL task.
* SysZone($2A6), ApplZone($2AA), ApplLimit($130), HeapEnd($114), HiHeapMark($BAE),
  MemErr($220), Lo3Bytes($31A).

### QuickDraw internals

* GrafVars: `grafVars^^+26` read as the GWorld's offscreen GDevice handle and
  `device^^+6` as gdITable (QDExtensions patch). **Executor's GrafVars is only 26
  bytes.**
* CGrafPort grafProcs (+$68) → newProc1 (+56); CCrsr fields
  (+20 crsr1Data, +52 mask, +84 hotSpot); PixMap pixelType (+30); gdRect (+34).
* JShowCursor($804) / JShieldCursor($808): QT chains them, calling the old ones.
  Executor calls JShieldCursor from vdriver/dirtyrect.cpp but not JShowCursor, so the
  ShowCursor-side ICM -94 call never happens.
* VBLQueue($160) bit 6 (inVBL).

### Time Manager / VIA

inst -19069's Microseconds reads VIA1 ($1D4) timer 2 (+$1000, +$1200,
IFR +$1A00 bit 5) and the Time Manager globals at **$B30** (+8, +16..+26).
It is skipped when Gestalt 'bugz' bit 8 is set (real 7.6.1 evidently sets it: no
Microseconds row in the 2.5 trace). **Executor does not answer 'bugz'**, so QT 2.5
and 3.0.2 would install it. Executor's VIA is a zeroed 8 KB block and $B30 is 0,
so Microseconds would return garbage. Answer 'bugz' with bit 8 set.

### ROM

* ROMBase+8 version: inst -19063, the lpch loader, and inst -19069's VBL task on $0178.
  * **Executor's ROMBase is a 10-byte array with version $067C**, so inst -19063 runs
    'pach' -19070. That installs $A0B3 with ROMBase+$4EC6... addresses: pointers into
    nothing, harmful if anything ever calls $A0B3.
  * Skip it by answering Gestalt 'bugx' with bit 11 set (also bits 13/14 for the PPC
    pboks).
* ROMBase+$12 (ROM release, lpch loader only).

### Others

* $8F2 WWExist; $964 ScrapHandle; $CC0 ASCBase (+$804, Mac II VBL task only);
  $15A SysVersion; $28E ROM85; $B22 HWCfgFlags; $174 KeyMap.
* 'qd  ' version thresholds that choose patches: <$235 DrawPicture/JShieldCursor;
  <$240 CopyDeepMask entry; <$242 QDExtensions/OpenCPort/$ABA8.
* Gestalt selectors QuickTime reads: 'qtim' (must be absent), 'icmp', 'sysa',
  'bugx', 'bugz', 'cpnt', 'snd ', 'powr', 'heap', 'qd  ', 'mach', 'proc', 'vers'.

## 5. Executor today (executor/core/romlib)

* quicktime.cpp (168 lines): EnterMovies…DisposeMovie stubs that log
  `warning_unimplemented` and return noErr. There is nothing behind $AAA3, $ABF8 or
  $ABC2, and Gestalt 'icmp' is compiled out (gestalt.cpp, `THESE_REALLY_ARENT_NEEDED`).
* component.cpp: a full C++ Component Manager. 'cpnt' = $00030000, so Apple's QT
  would keep it. $28A is not provided (see 4).
* QuickDraw:
  * DrawPicture skips $8200/$8201 data (qPicstuff.cpp ~1930) and never calls
    opcodeProc.
  * SetStdCProcs leaves opcodeProc/newProc1 nil (qIMV.cpp:40).
  * CopyDeepMask = CopyMask (qIMIV.cpp:362).
  * NewGWorld has no 24→32 mapping (qGWorld.cpp, unsure).
  * OpenCPort, InitGDevice, SetCCursor and SectRgn are native, without QT's additions.
* Microseconds: the host clock (emustubs.cpp:414, time.cpp:282).
* Pack3: the System's PACK 3 (stdfilepkg.cpp); no preview selectors.
* HeapDispatch: only $A and $20 (MemoryMgr.yaml).
* PBMountVol/PBUnmountVol: native, and mounts go through the traps, so a patch sees them.
* MatchAlias is native (alias.cpp:821).
* There is no native equivalent of any QuickTime-added behaviour in 1a/1b. The
  missing pieces for running Apple's QT INIT on Executor are listed in 4. The ones
  that would break it outright:
  1. 'bugz' bit 8 and 'bugx' bit 11 not answered;
  2. $28A unset;
  3. the $ABA8 write into the unimplemented-trap code;
  4. the GrafVars+26 read;
  5. HeapDispatch 0–6.
