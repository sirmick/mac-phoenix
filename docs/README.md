# MacPhoenix documentation

Start with the [README](../README.md). [CLAUDE.md](../CLAUDE.md) is the
compact developer reference: every flag, endpoint and directory on one
page, kept current with the code.

## Using MacPhoenix

| | |
|---|---|
| [Install.md](Install.md) | dependencies, optional parts, macOS, host settings, the guest-side apps' toolchain |
| [Deployment.md](Deployment.md) | modes (web, headless HTTP, headless), transports, reverse proxy |
| [Commands.md](Commands.md) | command line: build, run, test, debug |
| [JsonConfig.md](JsonConfig.md) | the config file |
| [API.md](API.md) | every HTTP endpoint |
| [Networking.md](Networking.md) | net-bridge: NAT, DHCP, several guests, AppleTalk |
| [Provisioning.md](Provisioning.md) | disk images built from pinned installers: media, bakes, snapshots, images |
| [MPW.md](MPW.md) | MPW and CodeWarrior's tools, headless on Executor |
| [MacBrowser.md](MacBrowser.md) | the modern web inside System 7 (experimental) |
| [TroubleshootingGuide.md](TroubleshootingGuide.md) | when it doesn't boot, stream or connect |

## How MacPhoenix works

| | |
|---|---|
| [Architecture.md](Architecture.md) | processes, the Platform API, CPU cores, memory, traps and interrupts |
| [ThreadingArchitecture.md](ThreadingArchitecture.md) | the threads in each process |
| [CommandBridge.md](CommandBridge.md) | reading guest memory; BridgeAgent and the bridge trap |
| [LatencyShortcomings.md](LatencyShortcomings.md) | where input and video latency comes from |
| [DeveloperGuide.md](DeveloperGuide.md) | working on the code: common tasks |
| [DEV-FRICTIONS.md](DEV-FRICTIONS.md) | what costs time in development sessions, struck through when fixed |
| [deepdive/](deepdive/README.md) | [memory](deepdive/MemoryArchitecture.md), the Platform API's [adapters](deepdive/PlatformAdapterImplementation.md) and [interrupts](deepdive/PlatformAPIInterrupts.md), [CPU backend API](deepdive/cpu/CpuBackendApi.md), [CPU models](deepdive/cpu/CpuModelConfiguration.md), [UAE quirks](deepdive/cpu/UaeQuirks.md) |
| [ppc/README.md](ppc/README.md) | the PowerPC machine: memory, execution, ROM patching |

## Executor

The code and how it is laid out: [executor/README.md](../executor/README.md).
On public software, step by step: [executor/SHOWCASE.md](../executor/SHOWCASE.md).
The desktop application: [executor/frontends/desktop/README.md](../executor/frontends/desktop/README.md);
its Windows build: [tools/windows/README.txt](../tools/windows/README.txt).
The documents are in [executor/](executor/README.md), whose README is the
entry point (origin, what MacPhoenix changed, the licence).

| | |
|---|---|
| [PLAN.md](executor/PLAN.md) | design, milestones, every change to imported code |
| [DIRECTION.md](executor/DIRECTION.md) | where it goes after the current milestones |
| [STRAGGLERS.md](executor/STRAGGLERS.md) | the open items and their agreed order |
| [M1.md](executor/M1.md), [SCOREBOARD.md](executor/SCOREBOARD.md) | the target applications, in the tab and against the ROM machines |
| [Standalone.md](executor/Standalone.md) | `executor-standalone`: the tab's server, its API, who may use it |
| [WebSite.md](executor/WebSite.md) | putting the server on a host: tab, library, data |
| [Settings.md](executor/Settings.md), [Disks.md](executor/Disks.md) | the tab's settings and Disks panel |
| [MacOS8-9.md](executor/MacOS8-9.md), [APPLE-DISKS.md](executor/APPLE-DISKS.md) | Apple's 8.6/9.0.4 and 7.x Systems' items on Executor |
| [Appearance.md](executor/Appearance.md), [HiDPI.md](executor/HiDPI.md), [DisplayModes.md](executor/DisplayModes.md) | the Appearance Manager, drawing at 2x, the screen resized live |
| [OpenTransport.md](executor/OpenTransport.md) | Open Transport, native C++ |
| [CFM68K.md](executor/CFM68K.md), [LIBRARIES.md](executor/LIBRARIES.md), [PPCLibs.md](executor/PPCLibs.md) | CFM-68K, ASLM/CFM/components in the definitions, StdCLib and MathLib for PowerPC |
| [FSM.md](executor/FSM.md), [HFSPlus.md](executor/HFSPlus.md) | the File System Manager (PC Exchange), HFS+ |
| [FullHeap.md](executor/FullHeap.md), [MEMORY_MAP.md](executor/MEMORY_MAP.md) | a full application heap's fallback; the System's memory map |
| [CollectionManager.md](executor/CollectionManager.md), [IconServices.md](executor/IconServices.md), [cleanroom/](executor/cleanroom/README.md) | behavioural specifications; the clean-room record |
| [DiffTest.md](executor/DiffTest.md), [PPCTrace.md](executor/PPCTrace.md), [Speed.md](executor/Speed.md) | differential probes against Apple's libraries, PowerPC breakpoints, speed against the ROM machines |

## Testing and releases

| | |
|---|---|
| [Testing.md](Testing.md) | ctest labels, guest suites, Playwright, the boot matrix |
| [Packaging.md](Packaging.md) | .deb / .rpm, CI, Homebrew, Executor's packages |

## Plans and records

Plans with their milestones and status; the finished ones are kept as the
record of why things are the way they are.

| | |
|---|---|
| [cpu/PLAN.md](cpu/PLAN.md) | moving to MAME's CPU cores ([C1d-PMMU](cpu/C1d-PMMU.md), [C3b-STATUS](cpu/C3b-STATUS.md)) |
| [provisioning/PLAN.md](provisioning/PLAN.md) | the provisioning pipeline |
| [qt6/](qt6/PLAN.md) | the Qt 6 port (done, May 2026; [HANDOFF](qt6/HANDOFF.md), [Phase8](qt6/Phase8.md)); [Windows.md](qt6/Windows.md): mac-phoenix on Windows, not carried out (Executor has a Windows build) |
| [roms/](roms/MacIIFamily.md) | ROM research: the Mac II family, disassembly notes ([dissam/](roms/dissam/README.md)), MAME's Mac drivers ([mame/](roms/mame/README.md)) |

## Licensing

[LICENSING.md](../LICENSING.md): GPL-2.0-or-later as a whole; MacPhoenix's
own code also MIT; Executor MIT; MAME BSD-3.
