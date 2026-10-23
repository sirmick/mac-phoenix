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
  errors are the ASLM SDK's (E.T.O. #22, Past&Future:Archive:Apple Shared
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
  only after OTRcv answered kOTNoDataErr. A datagram bigger than
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
  ports and the odd calls, the other Internet services).

## Open

* OT is on by default (with Apple's System and Executor's own): an
  application that asks Gestalt `'otan'` picks OT over MacTCP; MacTCP
  applications are unaffected.
* Datagram errors (T_UDERR, OTRcvUDErr) and the ICMP reports behind them.
* Not written: mappers and name registration (AppleTalk's NBP), the
  transaction calls (OTSndRequest and friends: ATP, ADSP), OTCreateOptions /
  OTCreateOptionString, no-copy receive (OTReleaseBuffer, OTReadBuffer),
  transfer of ownership, the `Ntv*` and configuration-database sets, and
  the PPC binding (CFM's OpenTransportLib, OTInetClientLib, OTUtilityLib
  onto the same C++). Timer tasks and most port calls have no 68k glue in
  OT 1.1 (native only), so 68k clients can't reach them.
