# Clean-room work on Executor

Executor is written from documentation and observed behaviour. Where
MacPhoenix's Executor work drew on Apple's code itself (code bytes copied
from a real boot, or behaviour worked out by reading a disassembly), that
part is redone in two roles:

* **Spec side.** Someone who has seen Apple's code writes a specification:
  the interface and observable behaviour (inputs, outputs, registers,
  memory and low-memory effects, errors) in plain prose, with tests that
  would tell a right implementation from a wrong one. No Apple code: no
  bytes, no instruction sequences, nothing that follows the structure of
  Apple's implementation rather than what it does.
* **Implementation side.** Someone who hasn't seen that code (a fresh
  agent or person) implements from the spec, Inside Macintosh and
  Executor's own sources, in a branch where the old implementation has
  been removed. They don't look at the removed code (not in other
  branches, not in git history), at memory snapshots or disassemblies of
  Apple's software, or at `tools/macdecode/ghidra`.

The spec and a record of who did what stay here.

| Item | Spec | Removed | Reimplemented |
|---|---|---|---|
| System routines `$7B0` / `$668` (`romlib/sysroutines.cpp`): Apple's 68k code had been copied from a 7.5.5 boot | [system-routines.md](system-routines.md) | | |
| Private `ResourceDispatch` selectors (`romlib/res/resPrivate.cpp`): written partly from a disassembly | [resource-private-selectors.md](resource-private-selectors.md) | | |

Other Apple-derived data, handled without a clean room:

* The video driver's standard gamma table had been captured from the ROM
  machine; it is now computed (a correction of 1.45, matching entry for
  entry).

Going forward: implement from Inside Macintosh and black-box traces; use a
disassembly only to identify things (a selector's number, which routine a
trap reaches), and say so in a comment where it was used; never copy
bytes.
