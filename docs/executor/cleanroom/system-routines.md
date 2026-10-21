# Spec: the System's selector-table dispatcher and detached-package call

Two routines System 7.5.5 keeps pointers to in low memory, for its own code
and for packages and INITs:

| Low memory | Name here (Apple's is unknown) | multiversal |
|---|---|---|
| `$7B0` (long) | SelectorTableDispatch | `LM(SelectorTableDispatch)`, `Package.yaml` |
| `$668` (long) | CallDetachedPackage | `LM(CallDetachedPackage)`, `Package.yaml` |

Executor must provide both: guest code (Apple's Color Picker 2.0 INIT,
the System's Help Manager and Drag Manager entry code, and Executor's own
`helppkg.cpp` and `drag.cpp`) reaches them with

```
    ; A0, A1, D0 set up as below
    move.l  $7B0,-(sp)      ; or $668
    rts
```

so they are entered with the stack exactly as the caller's caller left it:
no extra return address of their own.

Both must be 68k-callable at those addresses. They may be written as 68k
code Executor installs in the System heap, or as Executor callbacks into
C++ (Executor's callback mechanism, `callback_install`, gives a guest
address that runs C++); either is fine as long as the behaviour below
holds. Install them where the current `ROMlib_install_system_routines()`
is called (`romlib/init.cpp`, at each launch's low-memory reset), keeping
that function's name and header (`rsys/sysroutines.h`).

## Calling convention shared by both

Both serve Pascal routines selected by D0:

* **D0.W low byte**: the selector, a signed byte.
* **D0.W high byte**: the size of the routine's parameters, in words
  (0-255).
* **Stack on entry**, from SP upward: the return address into the original
  caller (4 bytes), then the routine's parameters (2 × high byte bytes),
  then the word immediately above the parameters, which for a function is
  its result.

## SelectorTableDispatch (`$7B0`)

Entered with **A0** pointing at a selector table and D0 as above.

**Table format** (big-endian):

| Offset | Size | |
|---|---|---|
| 0 | word | kind: 0, 1 or 2 |
| 2 | byte | lowest selector (signed) |
| 3 | byte | highest selector (signed) |
| 4 | | entries |

Let *n* = selector − lowest selector.

* **Kind 0**: entries are signed words, one per selector; entry *n* is the
  word at table + 4 + 2·*n*. The routine is at table address + that word.
* **Kind 1**: entries are signed words, and the selector values themselves
  are byte offsets into the entry area: the entry is the word at table +
  4 + *n* (selectors in such tables step by 2). The routine is at table
  address + that word.
* **Kind 2**: entries are 32-bit absolute addresses; the routine is the
  long at table + 4 + 4·*n*.

**In range** (lowest ≤ selector ≤ highest, compared as signed bytes): jump
to the routine with the stack unchanged (the return address still on top),
**A0 = the routine's address**, D0 unchanged. D1 may be changed. The
routine returns to the original caller itself.

**Out of range**: pop the return address, pop the parameters (2 × D0's high
byte bytes), write `paramErr` (-50) as a word at the new top of the stack
(the function result), and return to the caller. Registers other than SP
need not be preserved (A0 and D0 may change).

## CallDetachedPackage (`$668`)

Calls a routine in a *detached package*: a package's code held in a
handle that is not in a resource map's chain and may be purged.

Entered with:

* **A0**: the package's handle (may be nil, or purged).
* **A1**: pointer to a word, the package's lock count, shared by every
  call into this package.
* **D0**: selector and parameter size, as above.

The package's code is a standard package (Inside Macintosh: the
package's selector table begins 10 bytes into its code, after the
package header). Behaviour:

1. **Nil handle**: the call fails with `memFullErr` (-108): pop the return
   address and the parameters, write -108 as the word result at the top
   of the stack, return to the caller.
2. **Purged handle** (master pointer nil): load it as `LoadResource` does,
   with RomMapInsert (`$B9E`) and TmpResLoad (`$B9F`) both set to `$FF`
   first. If it is still purged after that, fail as in 1 but with the
   word in ResErr (`$A60`) as the result.
3. **Lock**: if the lock count is 0, `HLock` the handle (D0 must survive
   this). Then add 1 to the lock count.
4. **Call**: enter the package's selector table (A0 = master pointer + 10)
   through SelectorTableDispatch, with D0 unchanged and the stack arranged
   so the routine sees **exactly a direct Pascal call**: its parameters,
   byte for byte, directly above a return address, and the word that was
   above the parameters directly above them. The return address points
   at an epilogue (below). The original return address, A1 and A0 are
   kept somewhere that survives the call (on the stack, above the moved
   parameters, is how the original does it; the stack below the original
   SP is free to use).
5. **Epilogue**, when the routine returns (having popped its parameters,
   as a Pascal routine does): take the result word it left, subtract 1
   from the lock count, `HUnlock` the handle if the count is now 0, then
   return to the original caller with the parameters gone, SP where a
   direct call would have left it, and the result word in the original
   result slot. The result word is restored even for procedures (it is
   just the word above the parameters), so a call is transparent for any
   routine.

Errors from the routine itself are its business (it returns its own
result). Re-entrant use is allowed: a package routine may call into the
same package again through CallDetachedPackage; the lock count keeps it
locked until the outermost call returns.

## Tests to write

gtests in `src/executor/tests` (they run on both 68k cores:
`executor.*` and `executor.mame68k.*`), building small tables and fake
packages in guest memory and calling through `execute68K`:

* Dispatcher: each kind, lowest and highest selector, negative selectors,
  out of range below and above (paramErr written at the right place, SP
  correct for 0, 1 and several parameter words), A0 = routine on entry.
* Detached call: a package handle with a 3-routine table returning a
  value computed from its parameters; nil handle (-108); a purged handle
  that reloads (a resource that can be purged and loaded again); lock
  count 0 → 1 → 0 with the handle locked during the call and unlocked
  after; a nested call into the same package (stays locked until the
  outer one returns); a procedure (no result) leaves the caller's word
  above the parameters untouched.

Existing tests that go through these routines: `drag_executor`,
`drag_executor_mame68k` (Apple's Drag Manager via `drag.cpp`), and the
Executor guest suites. All `executor` ctests must pass.
