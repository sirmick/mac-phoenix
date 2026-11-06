# PowerPC breakpoints and traces (Executor)

For PowerPC code in an Executor session that goes wrong without calling the
Toolbox in a way the trap log (`--executor-logtraps`) shows: break at a guest
address, at a place in a fragment, or at one of Executor's own PowerPC
routines; on each hit see the registers, the back chain, the stack and any
memory asked for, then optionally the next N instructions, disassembled, with
the registers each one changed.

```bash
./build/mac-phoenix --backend executor --executor-system 8.6.img --executor-ppc \
    --executor-ppc-break 'BBEdit Lite 6.1+17a218*4#r30:40' \
    --executor-ppc-break '!AESend@40000'
```

`--executor-ppc-break` is repeatable; `on` arms the tool with no breakpoint, so
that they can be set over the API later. Reports go to stderr (the session's
log) as they happen, and the last eight are kept for the API.

Code: `executor/core/romlib/base/ppc_break.cpp` (`rsys/ppc_break.h`); the core
side is `cpu::Core::set_instruction_hook` (`Config::instruction_hook`), which
mame-ppc implements with MAME's own per-instruction debugger call
(`UML_DEBUG`), generated only when the core is made with it: a session without
`--executor-ppc-break` runs the same code as before. With it the recompiled
code makes a call before every instruction (BBEdit Lite's startup is not
noticeably slower; a tight loop is).

## Breakpoints

`WHERE[@TRACE][*HITS][#MEM]...`

| Part | Meaning |
|---|---|
| `$ADDR`, `0xADDR`, `ADDR` | a guest address (hex) |
| `FRAGMENT+OFF` | offset (hex) in a PowerPC fragment's code section; FRAGMENT is its CFM name or its file's, as in the `[CFM] file 'NAME'` log line. Resolved when the fragment is prepared (before its init routine), again for each new instance |
| `FRAGMENT:SECT+OFF` | the same in section SECT |
| `!NAME` | Executor's PowerPC routine NAME (`!AESend`, `!fopen`, `!WaitNextEvent`): the system call op of its stub, so a hit is a call from PowerPC code, with the caller's arguments in r3-r10 and its return address in lr |
| `@TRACE` | trace the next TRACE instructions after a hit (default 0) |
| `*HITS` | report that many hits (default 1; `*0` every one). A breakpoint that has had its hits costs nothing |
| `#MEM` | memory shown at each hit, repeatable: `rN[+OFF\|-OFF][:LEN]` or `$ADDR[:LEN]` (hex, LEN default 0x20); each leading `^` reads through the address once more: `#^^r3+4:80` is the data of the handle in an AEDesc at r3 |

Offsets are the ones `tools/macdecode/pef.py FILE --dis START END` shows for
the same file (its data fork: `POST /api/copy {"from": ..., "to": "Host:"}` or
`hcopy -r` from an image), so code read there can be broken in directly.

## A hit

```
[ppcbreak] hit 1 of '!AESend*3#r3:8#^^r3+4:120' at $C1034900 Executor's AESend, in 'BBEdit Lite 6.1'
  r0  C1034900  r1  03C16D30  r2  00000000  r3  03C17050
  ...
  cr  40000000  xer 20000000  ctr C1034900  lr  $03A0E0E8 BBEdit Lite 6.1 sect 0+$1ccd90
  back chain from r1 = $03C16D30:
    sp $03C170B0  ret $03A19B24 BBEdit Lite 6.1 sect 0+$1d87cc
    sp $03C17120  ret $038A4CD8 BBEdit Lite 6.1 sect 0+$63980
    ...
  stack at r1:
    $03C16D30: 03C170B0 FFFFFFFF 03A0E09C 464E4452 73707574 03A8C254 03AEF730 00000065
    ...
  #r3:8 = $03C17050:
    $03C17050: 61657674 03AE516C                   aevt..Ql
  #^^r3+4:120 = $03AF5714:
    $03AF5714: 00000000 00000000 00000096 00000004 ................
    ...
    $03AF5744: 72656372 656C0000 00007073 6E200000 recrel....psn ..
```

Addresses in code are named as `ROMlib_cfm_describe` does (fragment, section,
offset, and the nearest export when the fragment has any), or as Executor's
routine. The back chain is each frame's saved r1, with the return address the
callee saved at 8 in its caller's frame; a routine stopped before its
prologue has its return address in lr only.

## A trace

```
[ppcbreak] trace of 6 instructions:
  $C10609E0                              (call Executor's WaitNextEvent)
                                        -> r3=00000000
  $039D0F30 BBEdit Lite 6.1 0+$18fbd8    lwz       r2,0x0014(r1)
                                        -> r2=03A8C254
  $039D0F34 BBEdit Lite 6.1 0+$18fbdc    lwz       r5,-0x72D0(r2)
                                        -> r5=03ABED8E
  ...
[ppcbreak] end of trace
```

Each line is an instruction about to run; the `->` line under it is what it
changed (GPRs, FPRs, cr, lr, ctr, xer). A call into Executor shows as `(call
Executor's NAME)`, its effect on the line after; PowerPC code Executor runs
inside that call (a handler, a callback) is traced too, as part of the count,
so a trace from `!AESend` follows the event into the receiving handler. Traces
of a few ten thousand instructions are fine: the log has all of it, the API's
copy the first 256 KB.

## The API

`/api/executor/ppc-break`, for a session started with `--executor-ppc-break`
(a breakpoint or `on`):

| Request | Answer |
|---|---|
| `GET [?since=N]` | `{"armed", "breakpoints": [{"spec", "address" ("$ADDR" or "pending ..."), "hits", "hits_max", "trace"}], "hits": [{"seq", "done", "text"}]}`, the kept reports after sequence number N |
| `POST {"add": SPEC}` | the same, or `{"error"}` for a bad SPEC |
| `POST {"remove": SPEC}` | the same; `"all"` removes every one |

```bash
curl -s -XPOST localhost:11000/api/executor/ppc-break -d '{"add":"!AECreateAppleEvent@20"}'
curl -s localhost:11000/api/executor/ppc-break | python3 -c \
  'import json,sys; [print(h["text"]) for h in json.load(sys.stdin)["hits"]]'
```

Fragment and routine breakpoints added over the API are resolved on the guest's
thread, at the next PowerPC instruction.

## In the tab

The WebAssembly tab takes the same specs in its query, several separated by
`;` (URL-encode `+` as `%2B`): `index.html?ppcbreak=PowerShell%2B2880c@40*2`.
Reports go to the Worker's console (and the server's log); `tab_site.mjs`
passes them with `SITE_QUERY='ppcbreak=...'`.

## Example: BBEdit Lite 6.1's untitled window (-1708)

BBEdit Lite 6.1 on 8.6 sends itself `'core'/'crel'` at launch to open its
untitled window; on Executor its handler answered -1708 (the event was "not
handled"), on the G3 it opened the window.

1. `!AESend@40000` traced the event into BBEdit's `'****'` handler. Replaying
   the trace's register changes (a few lines of Python over the log) listed
   every Toolbox call with its arguments and result: the handler read the
   attributes and parameters as on the G3, found its object accessor, then
   classified the target as its application object (`'self'`, state 0x0C) and
   fell through to its "not handled" path; the -1708 travelled back as a C++
   exception.
2. `!AESend*1#^^r3+4:120` showed the event handle: ID `'crel'`, but class
   `00 00 72 65` where `'core'` should be.
3. `!AECreateAppleEvent` showed BBEdit passing `'core'`. A trace from there to
   `AESend`, with the stores replayed against the handle's address range,
   found BBEdit's own write: it clears the long at offset 44 of the event
   header (twice) while it appends its `kocl` parameter by hand, AEGizmos'
   way.
4. On 8.6 the header's class is at 50 (difftest `aevt`, `layout.new.bytes`);
   Executor's was at 46, so the write took the class's first half. With the
   header laid out as 8.6's (`apple_events.h`), BBEdit opens its window.

## Test

`ctest -R ppc_break_executor` (`tests/test_executor_ppc_break.sh`): CLibTest on
Executor's 7.5.5 System with `!fopen@40` from the command line (resolution,
hit, registers, back chain into CLibTest, a trace back into it) and a
breakpoint at CLibTest's main by fragment and offset added over the API before
the launch (pending, resolved, hit, memory dump, removal).
`ctest -R cpu_cores`: the core's instruction hook on both mame-ppc back-ends.

## Limits

- Executor only, and only its PowerPC code (68k code: `--watch`, the trap
  log, snapshots). The hook is a `cpu::Core` facility: the ROM machine's
  mame-ppc could use it the same way, with plain addresses.
- A breakpoint reports and goes on; it does not stop the guest.
- `--watch` on memory that PowerPC code writes crashed a session (seen with
  BBEdit's AE dispatcher object): use `#MEM` at a breakpoint instead.
