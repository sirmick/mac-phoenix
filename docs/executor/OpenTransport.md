# Open Transport on Executor

Executor answers Open Transport 1.1's 68k client API in C++
(`src/executor/romlib/opentransport/`), on host sockets, so 68k applications
built with Apple's OT glue run unchanged. On by default since 2026-10-23
(`--no-executor-ot` leaves it out: Gestalt then has no `'otan'` and
applications fall back to MacTCP); milestone M3i in PLAN.md.

```bash
./build/mac-phoenix --backend executor --executor-system macos-7.5.5.img
#   --executor-debug ot   logs binds, calls, notifier events, the bytes on sockets
ctest -R ot_executor      # OTTest through Apple's glue, both 68k cores, and
                          # without Apple's ASLM (ot_executor_noaslm)
```

## How applications reach OT

A 68k application links Apple's static glue from MPW (`OpenTransport.o`,
`OpenTransportApp.o`, `OpenTptInet.o`). Every OT function there is a stub:

```
    LEA     stub_record,A0    ; +0 bound address (0 until bound)
    MOVE.L  (A0),D0           ; +4 the function set's client vector record
    BEQ.S   @bind             ; +12 the function's index in the set (1-based)
    MOVEA.L D0,A0
    JMP     (A0)
@bind JMP   __SLM11FuncDispatch
```

`__SLM11FuncDispatch` (also glue) jumps to the Shared Library Manager's
binder at **SLMGlobal+$88** (SLMGlobal: `*(ExpandMem + $10C)`, ExpandMem
`$2B6`), A0 the stub record, D0 the client's `TLibraryManager`. A client
vector record holds 12 bytes the binder uses, two version words, and the
function set's ID as a C string at +16. The function sets and their indices
(`ot_names.inc`) were read from the glue with MPW's DumpObj:

| Function set | Functions | What |
|---|---|---|
| `OTClnt$LclUtils` | 30 | InitOpenTransport, memory, ports, configurations, options |
| `OTClnt$CmnShared` | 70 | atomics, lists, LIFOs, time stamps, memory and strings, tasks |
| `OTClnt$LclClnt` | 73 | providers and endpoints: modes, notifiers, bind, connect, send/receive, ... |
| `ot:inet$LclInetClnt` | 12 | Internet services: the resolver, address helpers, interface info |
| `OTClnt$Ntv*`, `OTClnt$RS_OT`, `OTLib$CfigLib` | 121 | native-side and configuration sets (not bound by the clients seen so far) |

`InitOpenTransport` (glue) asks Gestalt `'otan'` for the present bit
(kOTNotFoundErr without it), starts the Shared Library Manager
(`InitLibraryManager`), calls the client manager's **LoadClass**
("OTClnt$LclUtils"; vtable slot 16, `$40`) and then
`InitOpenTransportPriv(3, &__gOTClientRecord)` through a stub.

## What Executor does

* **Gestalt** `'otan'` (present, loaded, TCP present, loaded) and `'otvr'`
  (1.1) come from `ROMlib_ot_gestalt`. The first call puts the Shared
  Library Manager entries the glue uses in place (`ot_slm.cpp`). No Apple
  code runs on the way to OT:
  * **Without Apple's ASLM** (Executor's own System, or the ASLM Loader not
    allowed by the extension policy) Executor makes SLMGlobal
    (ExpandMem+$10C) itself: a root manager whose vtable +$9C makes a
    client's `TLibraryManager` and +$A0 frees it, and the binder at +$88.
  * **With Apple's ASLM running**, its binder and the root manager's two
    slots are wrapped. A client that asked Gestalt `'otan'` just before
    making its manager (the OT glue does) gets a manager of Executor's;
    other clients, and other function sets, go on to Apple's with every
    register as it came. A manager of Apple's still answers LoadClass for
    OT's sets here.

  Executor's `TLibraryManager` is the SDK's layout (LibraryManager.h: the
  vtable, then fPool, fLibraryFile, fDefaultPool, fGlobalWorld, all nil):
  LoadClass (slot 16, `$40`) answers noErr for OT's function sets and
  kNotFound (-3120) for others, GetLibrary (slot 28, `$70`) nil; any other
  slot is logged and answers 0. The glue patches `ExitToShell` to free its
  manager; a process that quits otherwise has its managers freed and its
  clients' providers closed by `ROMlib_ot_process_exit`. Apple's OT
  libraries stay registered with ASLM (when it runs) and are never loaded.
  The root manager's slots and SLMGlobal's offsets were identified from
  the glue's calls (MPW DumpObj of `OpenTransportApp.o`); the classes and
  errors are the ASLM SDK's (on the MPW disks, docs/MPW.md; E.T.O. #22, Past&Future:Archive:Apple Shared
  Library Manager).
* **Conventions.** Each C++ function is declared with its convention and
  argument sizes from Apple's header (`ot_internal.h`, `Def`): pascal (pushed
  left to right, words and longs, the result in a slot above them, the
  callee pops) or MPW C (right to left, a long each, D0, the caller pops).
  The `…Priv` entries are C and take the glue's client record last. A
  function no part implements is a fatal error naming it when it is bound.
* **Providers** (`ot_provider.cpp`): a ProviderRef is a cookie. Endpoints
  are non-blocking host sockets. A blocking-mode call waits for its socket
  in 20 ms slices and runs the interrupts that came due in between (Time
  Manager, VBL, OT's own pump: notifiers, deferred tasks), as a Mac's
  interrupts go on while an application spins in a blocking call; one of
  them may end the wait with OTCancelSynchronousCalls. Non-blocking calls
  answer kOTNoDataErr / kOTFlowErr. A watcher thread polls
  the sockets that wait for something and raises an Executor interrupt; at
  interrupt time the pump sets what OTLook reports (T_CONNECT, T_DATA,
  T_ORDREL, T_DISCONNECT, T_LISTEN, T_GODATA) and calls the notifiers.
  Completions of asynchronous calls (T_OPENCOMPLETE, T_BINDCOMPLETE, ...)
  are queued and delivered the same way, after the call returned; a
  notifier is never re-entered. Notifiers run in the A5 world the client had
  when it installed them. T_DATA is sent once for what is waiting and again
  after OTRcv answered kOTNoDataErr or took everything there was (Apple's
  OT 1.1.1 and 2.6.1; Hotline Connect reads a tracker's listing that way
  and waits for the next T_DATA). A datagram bigger than
  OTRcvUData's buffer comes in pieces, T_MORE on all but the last. Addresses
  may be DNSAddresses (`"name:port"`): OTConnect (looked up on a host thread
  for an asynchronous or non-blocking endpoint, then T_CONNECT as usual),
  OTSndUData, OTResolveAddress. OTRcvUDErr never has an error to report
  (ICMP isn't seen); OTIoctl accepts I_OTGetMiscellaneousEvents and nothing
  else.
* **Options** (`ot_options.cpp`): OTOptionManagement on host socket
  options: T_NEGOTIATE, T_CHECK, T_DEFAULT, T_CURRENT (T_ALLOPT for a
  level), per-option status and the worst in the answer's flags.
  XTI_GENERIC: XTI_LINGER, XTI_SNDBUF, XTI_RCVBUF, XTI_SNDLOWAT,
  XTI_RCVLOWAT, XTI_DEBUG; INET_TCP: TCP_NODELAY (on unless the client sets
  it), TCP_MAXSEG, TCP_KEEPALIVE; INET_IP: IP_TOS, IP_TTL, IP_REUSEADDR,
  IP_DONTROUTE, IP_BROADCAST, multicast TTL, loop, join and leave;
  UDP_CHECKSUM. Options set before a TCP endpoint has its socket (made at
  OTBind or OTConnect) are applied when it is made. OTNextOption and
  OTFindOption walk option buffers.
* **Tasks** (`ot_tasks.cpp`): deferred tasks run from the pump (deferred
  task time), system tasks from SystemTask in their own process, both in
  their creator's A5 world. OTCanMakeSyncCall and OTIsAtInterruptLevel
  follow the pump, deferred tasks and OTEnterInterrupt brackets. Atomic
  operations, OTEnqueue/OTDequeue, OTFindLink and the list calls are in
  `ot_utils.cpp` (SC compiles the atomic ones inline as BSET/CAS).
* **Ports**: one, built-in Ethernet (`"enet"`, kOTEthernetDevice on the
  motherboard bus): OTGetIndexedPort, OTFindPort, OTFindPortByRef,
  OTGetProviderPortRef, and the OTPortRef packing calls.
* **Internet services** (`ot_inet.cpp`): the host's resolver, synchronous
  in place or on a host thread for an asynchronous provider
  (T_DNRSTRINGTOADDRCOMPLETE with the caller's InetHostInfo as cookie);
  OTInetGetInterfaceInfo reports the host's default-route address, its mask,
  the first name server in /etc/resolv.conf. OTInetSysInfo (HINFO),
  OTInetMailExchange (MX, lowest preference first) and OTInetQuery (any
  class and type, as DNSQueryInfo records with names in the data expanded)
  ask the host's name servers with res_query, synchronous or asynchronous
  (T_DNRSYSINFOCOMPLETE, T_DNRMAILEXCHANGECOMPLETE, T_DNRQUERYCOMPLETE).
* **Defaults.** A new provider is synchronous and non-blocking.
* **OT 1.1.1's additions** (Gestalt `'otvr'` says 1.1.1, $01118000, as
  Apple's on Mac OS 7.6.1; applications built for it ask: Hotline Connect
  1.9 wants $01110000 or more), for PowerPC clients: timer tasks
  (`OTNtvUtilLib`, the 68k set `OTClnt$NtvUtils`): a Time Manager task each,
  whose expiry runs the task at deferred task time; scheduling again
  replaces the time, OTCancelTimerTask answers whether it was scheduled.
  `OTEnterNotifier`/`OTLeaveNotifier` (`OTClientLib`): the provider's
  events wait until the client leaves; false from inside its notifier.
  `OTUseSyncIdleEvents`: kOTSyncIdleEvent to the notifier at each 20 ms
  slice of a blocking call's wait. `OTSetMemoryLimits`
  (`OTNativeClientLib`): accepted, there is no pool. `OTExitPatchPriv`: the
  client closed (OT 1.1.1's glue's way out calls it as the process quits).
  The calls with no 68k set are `ot: native` in the definitions.

## Tests and tools

* `tests/guest/ot/OTTest.c`: an MPW tool linked with Apple's glue (`-model
  far` on SC and Link: the glue makes 32-bit references), built with
  `tools/mpw/mpw build tests/guest/ot -o tests/guest/ot`; the binary is
  committed. `tests/test_ot_executor.sh` runs it on a session with
  `--executor-ot` against `tests/lib/echo_server.py`: 20 cases (Gestalt and
  init, utilities, atomics and queues, deferred and system tasks, the
  resolver both ways, interface info, TCP blocking, non-blocking,
  asynchronous, refused and listening, UDP and a datagram in pieces,
  options, a blocking call cancelled from a Time Manager task, DNSAddresses,
  ports and the odd calls, the other Internet services). Its PowerPC builds
  add `native` and `native_net`: OT 1.1.1's additions, found through the
  Code Fragment Manager (no headers or import libraries have them all),
  and T_DATA after a read that took everything; Apple's OT 1.1.1 (7.6.1)
  and 2.6.1 (9.0.4) on the G3 answer as Executor does
  (`ot_retro68_ppc_rom_761`, `ot_retro68_ppc_rom`).

## Open

* OT is on by default (with Apple's System and Executor's own): an
  application that asks Gestalt `'otan'` picks OT over MacTCP; MacTCP
  applications are unaffected.
* Datagram errors (T_UDERR, OTRcvUDErr) and the ICMP reports behind them.
* Not written: mappers and name registration (AppleTalk's NBP), the
  transaction calls (OTSndRequest and friends: ATP, ADSP), OTCreateOptions /
  OTCreateOptionString, no-copy receive (OTReleaseBuffer, OTReadBuffer),
  transfer of ownership, the rest of the `Ntv*` sets, most of the
  configuration database (below). Most port calls have no 68k glue in
  OT 1.1 (native only), so 68k clients can't reach them.

## The functions in the definitions; PowerPC clients

Every function Executor implements is an entry in
`multiversal/defs/OpenTransport.yaml` (docs/executor/LIBRARIES.md, step
4): its Apple name, the CFM library PowerPC code imports it from, its
68k function set and that set's name for it (`aslm:`; the sets name their
Pascal routines in capitals), its convention and argument sizes, and the
routine (`uint32_t f(const ot::Call &)`). The generator turns each into
`OT_FUNCTION`, which registers it both ways (`OTFunction::init` in
`ot.cpp`): for the binder above, and as a PowerPC export.

PowerPC applications reach OT through Apple's PowerPC glue
(OpenTransportAppPPC.o & co.), which imports the same private routines
from `OTClientLib` (the `OTClnt$LclClnt` set), `OTClientUtilLib`
(`OTClnt$LclUtils`), `OTUtilityLib` (`OTClnt$CmnShared`),
`OTInetClientLib` (`ot:inet$LclInetClnt`) and `OTGlobalLib` (data:
`__gOTGlobal`, two timing records; zeroed). With OT on, Executor's Code
Fragment Manager answers these before Apple's files (Apple's need Apple's
OT kernel; their init routines fail). `ot::Call` reads a PowerPC caller's
arguments from r3-r10 and the parameter area (`Call::byte` for a UInt8:
a Pascal word's high byte on 68k, the low byte on PowerPC); notifiers,
deferred and system tasks and `OTFindLink`'s search routine installed
from PowerPC code are called as PowerPC code (`callPPC`). OTTestPPC (the
same OTTest.c, MrC + PPCLink) passes its cases on Executor
(`ot_ppc_executor`), `cancel` (a PowerPC Time Manager task cancelling a
blocking call) since Mixed Mode keeps the PowerPC registers (STRAGGLERS
64).

## The C side: headers and PowerPC glue

`OpenTransport.yaml` and `OpenTptInternet.yaml` are also Open Transport's
C interface, as Retro68 programs see it (`OpenTransport.h`,
`OpenTptInternet.h`, `OpenTptLinks.h`, all the generated `Multiverse.h`):
OT 1.1's types, structures and constants from Apple's headers'
declarations and Inside Macintosh: Networking with Open Transport,
`size_t` as `OTByteCount` (32 bits for both processors and for
Executor's 68k layouts, each structure's size checked there), the names
the host's headers have as well (`E*`, `O_*`, `AF_INET`, `IP_*`,
`TCP_*`) for the C side only. Every routine has its real types; the
generator still gives `ot::Call` a letter per argument from the type's
size (Boolean `B`, a word or byte `W`, else `L`), so the registrations
are what they were.

The libraries export the `...Priv` routines, not `InitOpenTransport`,
`OTOpenEndpoint`, `OTAlloc`, the task creators and the rest that take
the client: those are glue, `multiversal/custom/OpenTransportGlue.c`,
compiled into `libppc/libOpenTransportApp.a` when Retro68's PowerPC
compiler is on the generator's PATH (`custom/libraries.yaml`, `PowerPC:`).
A program links `OpenTransportApp OpenTransportLib OpenTptInternetLib`
(the last two Retro68's import libraries).

For 68k the same library (`lib68k/libOpenTransportApp.a`) also has a
wrapper for every `aslm:` entry, which the generator writes: it looks its
routine up once, through the Shared Library Manager, by its function set
and its 1-based index there (`aslm: { ..., index: }`, the order of
`ot_names.inc`), and calls it. The lookup is ASLM's documented client
interface (the ASLM SDK's LibraryManager.h): the application makes a
TLibraryManager of its own with the root manager (SLMGlobal+$10, vtable
+$9C, after asking Gestalt `'otan'`), then `LoadClass` (slot 16, $40) and
`GetFunctionPointer` by index (slot 20, $50) per set. By index, because
Apple's ASLM has no names for OT's sets (by name it answers kNotFound);
Executor's managers answer both (`ot_slm.cpp`). CloseOpenTransport (or
`atexit`) frees the manager. The 68k record: SLMGlobal and the manager kept
after it, words 3 and 2 pointing at them, word 7 `$01000100` and the byte at
32 set, as Apple's 68k glue has them (seen on Executor).

What the glue gives OT was seen from outside, Apple's PowerPC glue run
against Executor (which logs what it gets) and against Mac OS 9.0.4's OT
(a probe reading the record before and after): InitOpenTransportPriv gets
3 and the client's record; the record is 36 bytes of OT's, but for words
2-4, which the client points at storage of its own after the record (a
long, two longs, and a routine descriptor), and the byte at 32, set. The
descriptor is a PowerPC routine, Pascal, returning a long: the client's
way out, which closes it (CloseOpenTransportPriv) and answers 0; OT keeps
it to close the client itself. Without it OT 2.6 jumps into nothing.
Every other `...Priv` routine but two gets the record last; the task
creators get the record's first word, which OT sets at init (0 on
Executor, which ignores it).

OTTest built this way (`tests/guest/ot/build-retro68.sh`,
`OTTestRetroPPC.bin` and `OTTestRetro68K.bin`, committed) passes its 20
cases on Executor (`ot_retro68_ppc_executor`, `ot_retro68_68k_executor`),
on Mac OS 9.0.4 for PowerPC (`ot_retro68_ppc_rom`) and on the Quadra with
Mac OS 7.6.1, Apple's ASLM and OT 1.1.1 for 68k (`ot_retro68_68k_rom`), as
Apple's glue does. `multiversal_retro68` compiles OTTest.c for both
processors against the headers and checks both glue libraries.

## Checked against Apple's

OTTest's cases are what Apple's OT does: 1.1.1 (Mac OS 7.6.1) and 2.6.1
(Mac OS 9.0.4) on the G3 under mame-ppc, against net-bridge's echo
service (`test_guest_suite.sh --echo-gateway`; STRAGGLERS 65). What
that settled, where Executor had it otherwise:

* `OTCancelSynchronousCalls(ep, err)` stays: the endpoint's receives,
  blocking or not, answer err until the client cancels with kOTNoError.
* System tasks run at WaitNextEvent (`toolevent.cpp`), not at
  SystemTask, GetNextEvent or EventAvail.
* `OTResolveAddress` wants a bound endpoint (else kOTOutStateErr).
* Option management: an option of a level the stack doesn't know fails
  the request with kOTBadOptionErr; an unknown option of a known level is
  T_NOTSUPPORT. IP_TTL is a one-byte option (default 255).
* TCP and UDP endpoints sit on IP: `OTGetProviderPortRef` is 0, and
  `I_OTGetMiscellaneousEvents` answers kENOENTErr.
* The resolver's names end with the root's dot (`localhost.`).
* A reverse lookup asks the name servers, which may have no name for
  127.0.0.1 (Executor's host resolver has one).

## The configuration database

What the TCP/IP control panel reads and writes (`OTLib$CfigLib` on 68k,
27 private functions; `ot_config.cpp`). The database is a resource file,
the panel's "TCP/IP Preferences": each configuration has an ID, its name
is the `'cnam'` resource of that ID, each attribute is a resource of
that ID whose type names it (`'iitf'` the interface, `'idns'`, `'ihst'`,
`'irte'`, `'isdm'`, `'port'` "Ethernet", `'prot'` "tcp", `'dtyp'`,
`'pwrd'`, `'stng'`, `'unld'`, `'ulvl'`, `'wpos'`...), and `'ccfg'` 1 holds
the active configuration's ID. The calls are private, so their arguments
come from what the panel passes (`--executor-debug ot` logs the stack of
an unimplemented C call) and what it does with the answers, and were then
checked against Apple's OT 1.1.1 on the Quadra and the G3 by CfgTest
(`tests/guest/otcfg`: its 68k build calls them by index through the
Shared Library Manager, its PowerPC build imports them from `OTConfigLib`
by name; `ctest -R otcfg`: Apple's answers `otcfg_rom`, `otcfg_rom_ppc`,
Executor's `otcfg_executor`, `otcfg_executor_ppc`). Each is a C routine; `#` is its index in the set.

| # | Call | Arguments, result |
|---|---|---|
| 2 | `OTOpenConfigurationsDB` | `(const FSSpec *, permission, DBRef *)`, OSStatus; the DBRef is the file's resource refnum |
| 3 | `OTCloseConfigurationsDB` | `(DBRef *)`, an OSErr (a word); the reference is set to −1 |
| 4 | `OTCreateAConfiguration` | `(DBRef, UInt32 *ID)`, OSStatus: a nameless one, the lowest free ID from 128 |
| 5 | `OTCopyAConfiguration` | `(DBRef from, fromID, DBRef to, toID, ConstStr255 name)`, OSStatus: every attribute, the copy named; `toID` made if need be (the panel copies into ID 1, "TempConfig") |
| 6 | `OTDeleteAConfiguration` | `(DBRef, ID)`, OSStatus |
| 7 | `OTGetConfigsCount` | `(DBRef)`, the count |
| 8 | `OTGetIndexedConfigID` | `(DBRef, index)`, the ID; index from 1, 0 for none |
| 9 | `OTGetActiveConfigID` | `(DBRef)`, the ID (`'ccfg'` 1) |
| 10 | `TCPCheckChangeConfigurationConsequences` | `(DBRef, ID)`, Boolean: would putting it in force interrupt TCP/IP services (the panel then warns so). False on Apple's when TCP/IP isn't up; always on Executor's, which stores and doesn't apply |
| 11 | `TCPChangeConfiguration` | `(DBRef, ID)`, OSStatus: put it in force (Executor: stored, not applied) |
| 12 | `OTSetActiveConfigID` | `(DBRef, ID)`, OSStatus |
| 13 | `OTGetConfigName` | `(DBRef, ID, Str255)`: no result to speak of (PowerPC's r3 is left as it was; Executor's answers noErr, resNotFound) |
| 14 | `OTSetConfigName` | `(DBRef, ID, ConstStr255)`: no result to speak of (Apple's leaves 1 in D0, for an ID that isn't there too) |
| 15 | `OTGetConfigID` | `(DBRef, ConstStr255 name)`, the ID, 0 for none; the name compared without case |
| 19 | `OTGetAttributeSize` | `(DBRef, ID, OSType)`, the size, 0 for none |
| 21 | `OTSetAttributeValue` | `(DBRef, ID, OSType, const void *, size)`, OSStatus; size 0 removes it |
| 22 | `OTGetAttributeValue` | `(DBRef, ID, OSType, void *, size)`, OSStatus; for an attribute it hasn't, noErr and the buffer untouched |
| 23 | `OTGetDefaultPort` | `(device type)`, the OTPortRef |
| 24 | `OTGetUserPortNameFromPortRef` | `(OTPortRef, Str255)`: "Ethernet" |
| 26 | `OTIsPortCompatibleWith` | `(const OTPortRecord *, const char *protocol)`, Boolean |

Not reached by anything the panel does (they answer kOTNotSupportedErr
and are logged): `OTCreateConfigurationsDB` (1; the panel makes an
export file itself, with the Resource Manager, and copies into its
refnum), `OTGetAttributesCount` (16), `OTGetIndexedAttributeID` (17),
`OTSetAttributeValueHandle` (18), `OTGetAttributeValueHandle` (20),
`OTGetPortIconFromPortRef` (25), `OTIsPortReal` (27).

The panel also checks a configuration's interface against the ports
(`OTFindPort`, `OTGetSlotFromPortRef`), so Executor's Ethernet port now
has the record Apple's OT 1.1.1 gives a Quadra's (a probe there):
`enet0` (`OTFindPort` takes `enet` too), module `enetDRVR`, fInfoFlags 1,
fCapabilities $0B, not marked active.

With these the panel opens on 7.6.1 (Apple's System) and shows the
stored configuration: "Connect via: Ethernet", "Configure: Using DHCP
Server".

**A DBRef is a resource file refnum.** Which address fields a server
fills in comes from the panel's own `'srvr'` resources (14 bytes, a word
per field, items 5 to 11; ID 100 + 10 × user mode + 30 for TCP over
Ethernet + the method − 1: 132 is "TCP / Basic / DHCP"), read with
`OTGetAttributeValue(ref, 132, 'srvr', buffer, 14)` where `ref` is the
panel's own resource file refnum, kept in a word. So Apple's
database reference is the file's refnum, and any open resource file is
a database; Executor's was a pointer to a block of its own, the read
failed, every field stayed an edit box. Found by tracing the panel on the
Quadra (gdb, a hardware watchpoint on its code; the field table in its
object), the panel's code only observed. A field a server fills in is
static text: on the Quadra "< will be supplied by server >", since
Apple's OT has not brought TCP/IP up when the panel opens; on Executor,
whose OT is always up on the host's network, the address the interface
has (the router reads "< not available >": Executor's interface info
has none).

**Saving.** On closing with changes and "Save", the panel first copies
the active configuration into a temporary one (ID 1, "TempConfig"), makes
that active, writes the edits into it and asks
`TCPCheckChangeConfigurationConsequences`; then it restores the active
ID, deletes the temporary configuration, writes the edits into the
active one (`OTSetAttributeValue` for each attribute, size 0 for those it
drops: `'ipcp'`, `'alis'`) and calls `TCPChangeConfiguration`. `'ccfg'` 1
is not an attribute of configuration 1: deleting the temporary
configuration leaves it (and `'vers'`). The configurations dialog (File >
Configurations) duplicates (`OTGetConfigID`, `OTCreateAConfiguration`,
`OTCopyAConfiguration`), renames, deletes, makes active, exports (into a
resource file the panel makes) and imports (a name taken: replace or
rename) on Executor as on the Quadra. Executor's networking stays the
host's: a saved configuration is shown and kept, not applied.

**PowerPC.** The same calls are `OTConfigLib`'s exports for PowerPC
code (`library:` in the definitions; Executor answers the library, as
it does OTClientLib & co., and finds its OT libraries for `kFindLib`
too): CfgTest's PowerPC build passes on Executor and on the G3's OT 1.1.1
alike. The panel's PowerPC code (`--executor-ppc`) also imports
`OTATalkClientLib`, which Executor answers now (`ot_atalk.cpp`: no
AppleTalk, kOTNotSupportedErr): Apple's file needs Apple's OT kernel
(`OTNtvUtilLib`'s initialisation fails) and the panel then says Open
Transport isn't there. It also uses the Shared Library Manager's PowerPC
side (below), and with that its PowerPC code runs: it opens, shows and
saves configurations as its 68k code does, and quits from its File menu
with its manager, death watcher and OT client given back.

Left: the Duplicate and Rename dialogs show only
their edit field on Executor (no prompt, no buttons, the text not
selected; they work: Return takes them), and a dialog's or alert's area
isn't fully redrawn after it closes (a Dialog/Window Manager matter).

## The Shared Library Manager's PowerPC side

The TCP/IP control panel's PowerPC code is linked with the ASLM SDK's
PowerPC glue (`LibraryManagerPPC.o`, on the MPW disks): its operator new
and delete, its TRY/CATCH blocks and its death watcher are ASLM's. The
glue finds ASLM through the fragment `SLMGlobal` (GetSharedLibrary with
kLoadLib, then the data export `__gSLMGlobal`, a pointer to ASLM's
globals) and calls through what that points to as PowerPC code. Executor
answers the library itself (`ot_slm_ppc.cpp`; Apple's "Shared Library
Manager PPC" stays off), as far as the glue goes for what the panel does.
Which entries those are was seen in the panel's linked copy of the glue;
ASLM's own code was not read. The classes, fields, function sets and
errors are the ASLM SDK's headers', and the indices of ASLM's own function
set `"!$fset"` the glue's stub records' (58 functions, named in the
source).

| Where | What | Executor |
|---|---|---|
| SLMGlobal+$10 | the root manager; the glue takes it not being nil as "ASLM is loaded". Its vtable (as every PowerPC vtable here: pointers to transition vectors): +$74 makes a client's TLibraryManager `(root, **out, frame, pool size, zone, memory type, nonApp)`, +$78 frees it (true: unload ASLM too), +$60/+$64 `InstallDeathWatcher`/`RemoveDeathWatcher` | a manager of its own per client; false; true, never notified |
| SLMGlobal+$28 | a trace object | nil |
| SLMGlobal+$64 | a routine descriptor that loads (1) or unloads (2) ASLM | nil: always loaded |
| SLMGlobal+$7C, +$88 | the constructor and function binders: a stub record in r12 (bound vector, client vector record with the set's ID at +16, index at +12), r3-r10 also in the caller's parameter area | never binds the stub, so each call comes through; `TProcNotifier(proc, refPtr)` (`"!$pnot"` 3); `"!$fset"` below |
| TLibraryManager | the vtable, fPool, fLibraryFile, fDefaultPool, fGlobalWorld | a pool of its own as fPool and fDefaultPool |
| a pool | operator new calls its +$24 `(pool, size)`; operator delete takes the block's pool from the long before it (low four bits masked) and calls +$2C `(pool, block)` | blocks from the client's heap |

Of `"!$fset"`, which are definitions (`multiversal/defs/LibraryManager.yaml`,
`aslm:` without `library:`: registered with the binder by set and index;
LIBRARIES.md): `PushException` (19), `PopException` (20: takes the
TException and any inside it off the process's stack, nothing if Fail
did already), `Fail` (21: to the innermost TRY, whose `fBuffer` is
SetupException's save of r0-r31 and LR at +$80; `fError` and `fMessage`
set, SetupException returning 1), `MatchException` (22),
`GetCurrentClient` (29: the manager of the process's A5 world),
`ASLMGetCurrentGlobalWorld` and `ASLMSetCurrentGlobalWorld` (49, 50: a
PowerPC client's global world is its CurrentA5, as on the G3). Any other
function or class is logged and answers 0. The classes have no form in the
definitions and stay in `ot_slm_ppc.cpp`.

SLMTest (`tests/guest/slm/SLMTest.cp`, MPW MrCpp with the SDK's glue)
checks the same on Apple's ASLM PPC with Mac OS 7.6.1 on the G3
(`slm_rom_ppc`) and on Executor (`slm_executor_ppc`): InitLibraryManager
and CleanupLibraryManager (twice), operator new and delete (200 blocks),
TRY/CATCH/CATCH(e)/RERAISE/Fail with a message, the current client, the
global world, a TProcNotifier as a death watcher.

