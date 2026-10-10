# System memory map (68k, System 7) — vocabulary for M1

What a real System 7 machine's RAM looks like after boot, and the names
for each piece. M1 reconstructs this state without a ROM (approach A in
[PLAN.md](PLAN.md)): Executor plays the ROM, Apple's System file
supplies resources, and snapshots of a real UAE + Quadra ROM boot are
the reference.

**Status:** addresses are from Inside Macintosh and memory. Each is
*to verify* against the first snapshot; tick them off as they are
confirmed.

## RAM map, low to high

```
$00000000  Exception vectors        (CPU vector table, VBR = 0)
$00000100  Low-memory globals       ("lowmem", "system globals")
$00000400  OS trap table            256 × 4 bytes
$00000800  more low-memory globals
$00000E00  Toolbox trap table       1024 × 4 bytes
$00001E00  more low-memory globals / ROM-specific vectors
$00002000  System heap              (SysZone = RAMBase; verified on 7.5.5)
           Process Manager heap     the big zone each app's partition is cut from
             └ app partition: [app heap ↑ … ↓ stack][A5 world]
BufPtr →   boot-time allocations from the top (MacsBug, sound buffers, boot globals)
MemTop     end of RAM
$40800000  ROM (ROMBase); none under Executor
```

## Exception vectors ($0–$FF)

The CPU's own vectors. The ones that matter:

| Vector | Address | Purpose |
|---|---|---|
| A-line | `$28` | Every `$Axxx` trap word lands here |
| F-line | `$2C` | Unimplemented FPU and other F-line instructions |
| Autovectors | `$64`–`$7C` | Interrupts: VIA, slots, SCSI, SCC |
| `TRAP #n` | `$80`+ | Software traps, e.g. debuggers |

## Low-memory globals ("lowmem")

Fixed-address variables the OS and old apps read directly. Executor
models them through `LM()` at the real offsets.

| Area | Names and addresses |
|---|---|
| Memory | `MemTop $108`, `BufPtr $10C`, `HeapEnd $114`, `TheZone $118`, `ApplLimit $130`, `SysZone $2A6`, `ApplZone $2AA`, `Lo3Bytes $31A`, `MinStack/DefltStack $31E/$322` |
| Machine | `ROMBase $2AE`, `RAMBase $2B2`, `ROM85 $28E`, `HWCfgFlags $B22`, `MMU32Bit $CB2`, `SysParam $1F8` (in-memory copy of PRAM) |
| Extended | `ExpandMem $2B6`: pointer to `ExpandMemRec` (Gestalt table, Script Manager, other post-128K-ROM globals) |
| Help Manager | `HelpPackage $BE0` (detached `PACK` 14 handle; per-process range, same in every process), lock count ExpandMem+$130, globals ExpandMem+$78 (296 bytes; +$11E balloons on, +$10C/+$110 per-process menu and dialog `hmnu`/`hdlg` IDs); names guessed |
| Layers | `SystemMenuList $286` (Help and Application menus, system-wide), `SavedHandle $A28` (bits under a pulled-down menu; nil here). 7.5.5's `CurLayer $A90` and root layer `$9E2` are not modelled: the Layer Manager's records live in process.cpp |
| Current process | `CurrentA5 $904`, `CurStackBase $908`, `CurApName $910`, `CurApRefNum $900`, `CurJTOffset $934` |
| Resources | `TopMapHndl $A50`, `SysMapHndl $A54`, `SysMap $A58`, `CurMap $A5A` |
| Queues | `EventQueue $14A`, `VBLQueue $160`, `DrvQHdr $308`, `VCBQHdr $356`, `FSQHdr $360` |
| Devices | `UTableBase $11C` (unit table: one DCE handle per driver), `UnitNtryCnt $1D2`, `FCBSPtr $34E` |
| Screen and UI | `ScrnBase $824`, `MainDevice $8A4`, `DeviceList $8A8`, `TheGDevice $CC8`, `WindowList $9D6`, `MBarHeight $BAA` |
| Time | `Ticks $16A`, `Time $20C` |

## Trap dispatch tables

- OS table at `$400` (256 entries), Toolbox table at `$E00` (1024
  entries). Each entry is a 32-bit address.
- Trap word layout:
  - Bit 11 set: Toolbox trap.
  - Toolbox: bits 0–9 are the index; bit 10 is auto-pop.
  - OS: bits 0–7 are the index; bits 8–10 are flags (`A0` saved or
    not, `SYS`/`IMMED` heap selection).
- **Dispatcher**: the code behind vector `$28`. Decodes the trap word,
  saves registers per the OS-trap convention, jumps to the table entry.
- **Patch**: replacing a table entry.
  - **Tail patch**: calls the old entry, then does more work.
  - **Head patch**: does its work, then jumps to the old entry.
  - **Come-from patch**: checks its caller's return address. This is
    how ROM addresses leak into System patch code (`ptch`/`lpch`).
- **Trap map**: which code each entry points at — ROM, System heap
  (patched by the System or an INIT) or app heap. Comes straight out
  of a snapshot.

Today Executor keeps both tables as host arrays (`romlib/base/traps.cpp`)
and dispatches A-line traps in C++ (`romlib/base/dispatcher.cpp`).

## Heaps and zones (Memory Manager)

- **Zone**: one heap. Starts with a **zone header** (`Zone`): `bkLim`
  (end), `hFstFree`, `zcbFree` (bytes free), `gzProc` (grow-zone
  procedure), `moreMast` (master pointers to allocate at a time),
  `purgePtr`, `heapData`.
- **Block** (32-bit heap): 12-byte header — tag and flags, physical
  size, then a relative handle (relocatable) or the owning zone
  (non-relocatable). Three kinds:
  - **Non-relocatable** (`NewPtr`).
  - **Relocatable** via a **handle** (`NewHandle`). A handle points to
    a **master pointer**; master pointers live in master pointer blocks.
  - **Free**.
- Master pointers carried `lock`/`purge`/`resource` flags in the high
  byte in 24-bit mode; in 32-bit mode those flags moved into the block
  header. Match this — some apps peek.
- **System heap** (`SysZone`) holds:
  - the System file's **resource map**, plus `sysHeap` resources (fonts,
    `WDEF`/`MDEF`/`CDEF`, `PACK`s)
  - driver **DCEs**
  - patch code loaded by the System and by INITs
  - `ExpandMemRec`, the Gestalt table, the FCB array, VCBs
- **Process Manager heap** (a.k.a. the MultiFinder zone): between the
  System heap and `BufPtr`. Each app's **partition** is cut from it,
  sized by the app's `SIZE` resource.
- **Application heap** (`ApplZone`): bottom of a partition, upper
  bound `ApplLimit`.

## A5 world (one per application)

At the top of each partition:

```
higher  ┌ jump table          32(A5) and up, from CurJTOffset; loads CODE segments
        │ app parameters      0–31(A5); 0(A5) = pointer to the QuickDraw globals
A5 ───► ├───────────────────
        │ QuickDraw globals   just below A5 (thePort, screenBits, randSeed, patterns…)
        │ app globals         negative offsets from A5
        └ stack grows down from here (CurStackBase) toward ApplLimit
```

`CurrentA5` is the running app's A5. A process switch swaps A5, the
stack, `ApplZone`, `ApplLimit`, the resource chain and a set of per-app
lowmem globals. The Process Manager saves and restores those; they are
what make multitasking work.

## Resource chain

`TopMapHndl` heads a linked list of open resource maps, newest first:
the app's resource file, documents it opened, ending with `SysMap`.
`CurMap` says where a search starts. "Use Apple's System file" mostly
means its map is at the end of this chain, loaded into the System heap.

## Other structures seen in snapshots

- **Unit table**: one entry per driver unit number, each a handle to a
  DCE (Device Control Entry).
- **Drive, VCB and FCB queues**: the File Manager's mounted drives,
  mounted volumes and open files.
- **Interrupt-time queues**: VBL tasks, Time Manager tasks, and the
  Deferred Task queue (`DTQueue`) for work an interrupt handler
  postpones until interrupts are re-enabled.
- **Process Manager private data**: the PCB (process control block)
  list in its own zone. Undocumented; disassembly territory.

## Gap: Executor today versus the target

Target: vectors, lowmem, two trap tables, the System heap with real
contents, the Process Manager heap, and a partition with an A5 world
for the Finder.

Executor already builds lowmem, a System zone, an app zone and an A5
world. Missing:

- real-address trap tables that guest code can patch (done 2026-10-10:
  Executor's tables are at `$400`/`$E00`, see `base/trapglue.h`)
- Apple's System file as the system resource map, with the System heap
  contents a real boot leaves
- the Process Manager heap and partition layout instead of one app zone

## First snapshot: 7.5.5 at Finder idle (UAE, Quadra ROM, 64 MB)

Taken with `POST /api/snapshot`, read with `tools/macdecode/macdecode.py`.

- `SysZone` = `RAMBase` = `$2000`, 2.2 MB: 1045 handles, 296 pointers.
- Trap tables: of the implemented entries, 102 of 234 OS traps and 356
  of 825 Toolbox traps point into the System heap — patched by the
  System file and INITs. Only 6 point into an app heap.
- `MemTop` reads `$003318b0`, not the end of RAM: per-process under
  the Process Manager? To investigate.
- Our UAE layout puts ROM at `$04000000` and the screen at
  `$04110000`, not the Quadra's real addresses.

## Per-process low memory (System 'lmem' -16458)

The Process Manager swaps these ranges on every process switch:
28 `(length word, address long)` pairs, 670 bytes, in the 7.5.5 System
file. It is why `MemTop` reads like a per-process value.

| Range | Globals |
|---|---|
| `$0100` 2 | monkeylives |
| `$0108` 8 | MemTop, BufPtr |
| `$0114` 8 | HeapEnd, TheZone |
| `$015C` 1 | SEvtEnb |
| `$0278` 2 | (unnamed) |
| `$02F8` 1 | ScrDmpEnb |
| `$0316` 4 | MacPgm/heapcheck |
| `$031E` 34 | MinStack, DefltStack, GZRootHnd, GZMoveHnd, EjectNotify, IAZNotify |
| `$08E0` 8 | JSwapFont, WidthListHand |
| `$08F2` 228 | WWExist, QDExist, …, CurApRefNum, CurrentA5, CurStackBase, CurApName, CurJTOffset, CurPageOption, … |
| `$09DA` 4 | SaveUpdate, PaintWhite |
| `$09E6` 8 | OldStructure, OldContent |
| `$09F2` 50 | SaveVisRgn, DragHook, …, TopMenuItem, AtMenuBottom, MenuList, MBarEnable |
| `$0A26` 22 | TheMenu, MBarHook, MenuHook, DragPattern |
| `$0A44` 76 | TopMapHndl, SysMapHndl, SysMap, CurMap, …, DeskHook, … |
| `$0A98` 80 | ANumber, ACount, DABeeper, DAStrings, TEScrpLength, TEScrpHandle, AppPacks, SysResName |
| `$0AEC` 16 | AppParmHandle, DSErrCode, ResErrProc, DlgFont |
| `$0B21` 1 | (unnamed) |
| `$0B2A` 4 | WidthTabHandle |
| `$0B4C` 4 | LastSPExtra |
| `$0B54` 12 | MenuDisable, MBDFHndl, MBSaveLoc |
| `$0BA6` 4 | SysFontFam, SysFontSiz |
| `$0BAE` 6 | (unnamed) |
| `$0BAA` 2 | MBarHeight |
| `$0BC2` 60 | LastFOND, fondid, FractEnable, … |
| `$0D32` 17 | SynListHandle, … |
| `$0DCC` 4 | (unnamed) |
| `$0CC8` 4 | TheGDevice |

## Application partitions (7.5.5, measured)

From the Finder-idle reference, current process BridgeAgent (SIZE
preferred 1024K, CODE 0 abovea5 200, belowa5 4548):

| Item | Value | Rule |
|---|---|---|
| Process Manager heap | `$0022D8C0`..`$03FDF014` | a plain zone from the System heap's end to just below `BufPtr` |
| Partition | locked handle, data `$03E14EB0`, `$104000` bytes | SIZE preferred + 16K; at the top of the PM heap, free space below it (Finder: 185K + 16K) |
| `ApplZone` | `$03E14EB0` | partition start |
| zone `bkLim` / `HeapEnd` | `$03F11E44` | `ApplLimit - 24` |
| `ApplLimit` | `$03F11E5C` | `CurStackBase - $6008` (`DefltStack` = `$6000`) |
| `CurStackBase` | `$03F17E64` | `CurrentA5 - 3980` (not `belowa5`; open) |
| `CurrentA5` | `$03F18DF0` | partition end + 8 - abovea5; the jump table runs to the partition end |
| `MemTop` | `$003318B0` | SysZone `bkLim` + partition size: the app sees a machine as big as its partition |
| `BufPtr` | `$03FE64E2` | global, above the PM heap |

Executor follows these rules (`process_layout_partition` in
`romlib/process.cpp`), except `CurStackBase = CurrentA5 - belowa5` and
`CurrentA5 = partition end - abovea5`, Inside Macintosh's rules, until
the two open offsets are explained.

## Snapshot tooling (on the UAE ROM boot)

- Dump vectors, lowmem and both trap tables; classify each entry by
  the zone or ROM range it points into.
- Walk each heap block by block; tag resource blocks with type and ID.
- Dump the resource chain.
- Disassemble around an address (UAE's 68k disassembler, exposed).
- `diff-world`: compare Executor's state against the snapshot.
