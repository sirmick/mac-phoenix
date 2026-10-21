# MacPhoenix documentation

Start with the [README](../README.md). [CLAUDE.md](../CLAUDE.md) is the
compact developer reference: every flag, endpoint and directory on one
page, kept current with the code.

## Using it

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

## How it works

| | |
|---|---|
| [Architecture.md](Architecture.md) | processes, the Platform API, CPU cores, memory, traps and interrupts |
| [ThreadingArchitecture.md](ThreadingArchitecture.md) | the threads in each process |
| [CommandBridge.md](CommandBridge.md) | reading guest memory; BridgeAgent and the bridge trap |
| [LatencyShortcomings.md](LatencyShortcomings.md) | where input and video latency comes from |
| [DeveloperGuide.md](DeveloperGuide.md) | working on the code: common tasks |
| [deepdive/](deepdive/README.md) | memory architecture, Platform API details, UAE quirks |
| [ppc/README.md](ppc/README.md) | the PowerPC machine: memory, execution, ROM patching |

## Executor

[executor/README.md](executor/README.md) is the entry point: where it came
from, what MacPhoenix changed, the licence. Then
[PLAN.md](executor/PLAN.md) (design, milestones, every change to imported
code), [DIRECTION.md](executor/DIRECTION.md),
[STRAGGLERS.md](executor/STRAGGLERS.md),
[MEMORY_MAP.md](executor/MEMORY_MAP.md),
[OpenTransport.md](executor/OpenTransport.md).

## Testing and releases

| | |
|---|---|
| [Testing.md](Testing.md) | ctest labels, guest suites, Playwright, the boot matrix |
| [Packaging.md](Packaging.md) | .deb / .rpm, CI, Homebrew |

## Plans and records

Plans with their milestones and status; the finished ones are kept as the
record of why things are the way they are.

| | |
|---|---|
| [cpu/PLAN.md](cpu/PLAN.md) | moving to MAME's CPU cores ([C1d-PMMU](cpu/C1d-PMMU.md), [C3b-STATUS](cpu/C3b-STATUS.md)) |
| [provisioning/PLAN.md](provisioning/PLAN.md) | the provisioning pipeline |
| [qt6/](qt6/PLAN.md) | the Qt 6 port (done, May 2026); [Windows.md](qt6/Windows.md) is a port that wasn't started |
| [roms/](roms/MacIIFamily.md) | ROM research: the Mac II family, disassembly notes ([dissam/](roms/dissam/README.md)), MAME's Mac drivers |

## Licensing

[LICENSING.md](../LICENSING.md): GPL-2.0-or-later as a whole; MacPhoenix's
own code also MIT; Executor MIT; MAME BSD-3.
