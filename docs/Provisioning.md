# Disk Provisioning & Guest Development

How to create bootable Mac OS disk images from Linux, populate them with software, and drive development tools inside the emulator from the host.

## Template Disk Images

Pre-built "bare OS" disk images for each supported Mac OS version. These contain a minimal bootable System Folder and nothing else, serving as a starting point for provisioning.

### Target Templates

| Template | Mac OS | Format | Notes |
|----------|--------|--------|-------|
| `system-7.5.5.img` | 7.5.5 | HFS | Free from Apple, our primary target |
| `system-7.6.1.img` | 7.6.1 | HFS | Last 68k-native System |
| `macos-8.1.img` | 8.1 | HFS+ | First HFS+, PPC only |
| `macos-9.2.2.img` | 9.2.2 | HFS+ | Last classic Mac OS |

### Creating HFS Images from Linux

The `hfsutils` package provides command-line tools for working with HFS (Standard) volumes:

```bash
# Create a blank 100MB HFS volume
dd if=/dev/zero of=blank.img bs=1M count=100
hformat -l "Macintosh HD" blank.img

# Mount and populate
hmount blank.img
hcopy -m SystemFile.bin ":System Folder:System"
hcopy -m FinderFile.bin ":System Folder:Finder"
humount
```

Key commands:
- `hformat` — format an image as HFS
- `hmount` / `humount` — mount/unmount for hfsutils operations (not a kernel mount)
- `hcopy -m` — copy with MacBinary translation (preserves resource forks)
- `hcopy -r` — copy raw (data fork only, fine for text/data files)
- `hls`, `hdir` — list directory contents
- `hmkdir`, `hdel` — create/delete files and folders

### Resource Fork Handling

Classic Mac applications have two forks: data fork and resource fork. Most transfer methods lose the resource fork. Safe formats:

- **MacBinary** (`.bin`) — single file wrapping both forks + Finder metadata. Use `hcopy -m`.
- **BinHex** (`.hqx`) — text-encoded MacBinary. Use `hcopy -m`.
- **Disk images** (`.dsk`, `.img`) — mount directly in the emulator as a second drive.
- **StuffIt** (`.sit`) — extract with `unar` on Linux, but resource fork handling varies.

### Provisioning Script (Planned)

```bash
# Goal: one command to build a provisioned disk
./tools/provision-disk.sh \
    --template system-7.5.5 \
    --size 200M \
    --add-mpw \
    --add-app MyApp.bin \
    --output my-disk.img
```

This would:
1. Copy the template image
2. Resize if needed
3. Copy applications via `hcopy -m`
4. Copy source files via `hcopy -r` (text files don't need resource forks)
5. Install the command bridge INIT if `--add-mpw` is specified

## MPW (Macintosh Programmer's Workshop)

Apple's official development environment (1985-1999), released for free. MPW is uniquely suited to automation because it's a **shell**, not just an IDE.

### What MPW Provides

| Tool | Purpose |
|------|---------|
| `MPW Shell` | Command-line shell with scripting, variables, control flow |
| `SC` / `SCpp` | C / C++ compiler (68k) |
| `MrC` / `MrCpp` | C / C++ compiler (PPC) |
| `Asm` | 68k assembler |
| `PPCAsm` | PowerPC assembler |
| `Link` / `PPCLink` | Linker |
| `Rez` | Resource compiler (text description -> resource fork) |
| `DeRez` | Resource decompiler (resource fork -> text) |
| `Make` | MPW's make tool |
| `Duplicate`, `Delete`, `Rename` | File management |

### MPW Shell Basics

MPW Shell commands look like Unix but with Mac path separators (`:`):

```
# Compile a C file
SC -o myapp.c.o myapp.c

# Link into an application
Link -o MyApp myapp.c.o "{Libraries}Interface.o" -t APPL -c '????'

# Compile resources (menus, dialogs, icons)
Rez myapp.r -a -o MyApp

# Run a build script
Execute MyBuildScript
```

MPW tools are **normal Mac executables** that do file I/O and print text output. They don't need a GUI — they just need someone to invoke them and capture the result. This makes them ideal for automation via the EmulOp bridge.

### Installing MPW on a Template

MPW lives in a folder on the Mac disk. Installation on a template image:

1. Obtain MPW Pro or MPW-GM (Apple's free release)
2. Extract from MacBinary/StuffIt archives
3. Copy the MPW folder to the HFS image via `hcopy -m`
4. Set the `{MPW}` shell variable to point to the install location

## EmulOp Command Bridge

A host-guest communication channel that lets the Linux host send shell commands to MPW running inside the emulator and receive the output back.

### Architecture

```
Linux CLI / API              MacPhoenix Host              Classic Mac OS
───────────────              ──────────────              ──────────────
                                                         Boot -> INIT loads
                                                         Agent installs timer task
                                                         Agent polls EmulOp
                                                               |
curl POST /api/exec          Queues command ──────────>  Agent sees command
  {"cmd": "SC test.c"}                                   Agent writes temp script
                                                         Agent launches MPW Shell
                                                         MPW executes "SC test.c"
                                                         Agent captures output
                                                         Agent sends via EmulOp
curl GET /api/exec/{id}      <────────── Returns result
  {"output": "...", "status": 0}
```

### Host Side: New EmulOp

Add a new EmulOp (e.g., `0x7180 COMMAND_BRIDGE`) to `src/core/emul_op.cpp`:

```
EmulOp COMMAND_BRIDGE:
  Register D0 = sub-operation
  Register A0 = pointer to shared buffer in Mac memory

  D0=0  CHECK_COMMAND (guest polls host)
    If pending command exists:
      Copy command string to buffer at A0
      Set D0 = 1 (command ready)
      Set D1 = command length
    Else:
      Set D0 = 0 (no command)

  D0=1  SEND_RESULT (guest returns output to host)
    Read result string from buffer at A0
    Read D1 = result length
    Read D2 = exit status
    Store on host side, signal waiting API handler

  D0=2  WRITE_FILE (host sends file contents to guest)
    A0 = buffer with: filename (Pascal string) + file contents
    D1 = total length
    Guest writes file to Mac filesystem

  D0=3  READ_FILE (guest sends file contents to host)
    A0 = buffer with file contents
    D1 = length
    Host stores the file data
```

Host-side state:

```cpp
struct CommandBridge {
    std::mutex mutex;
    std::condition_variable cv;

    // Command queue (host -> guest)
    struct PendingCommand {
        uint32_t id;
        std::string command;
    };
    std::queue<PendingCommand> commands;

    // Results (guest -> host)
    std::map<uint32_t, CommandResult> results;
};
```

### Guest Side: Bridge INIT

A small classic Mac application (INIT) that runs at boot and polls the EmulOp for commands. Written in C, compiled with Retro68 or MPW itself.

```c
/* BridgeINIT.c — EmulOp command bridge agent */

#include <Types.h>
#include <Timer.h>
#include <Files.h>

#define EMULOP_BRIDGE 0x7180    /* UAE trap opcode */
#define POLL_INTERVAL 100       /* milliseconds */
#define BUFFER_SIZE   32768

/* Shared buffer in application globals */
static char gBuffer[BUFFER_SIZE];

/* Trigger the EmulOp trap — this is the bridge to the host */
static inline int EmulOp_Call(int operation, char *buffer) {
    register int result __asm__("d0");
    /*
     * Move operation to D0, buffer pointer to A0,
     * then execute the trap opcode.
     * The host intercepts this and handles it in emul_op.cpp.
     */
    __asm__ volatile (
        "move.l %1, %%d0\n"
        "move.l %2, %%a0\n"
        ".short 0x7180\n"
        "move.l %%d0, %0\n"
        : "=r"(result)
        : "r"(operation), "r"(buffer)
        : "d0", "a0", "d1", "memory"
    );
    return result;
}

/* Execute a command string via MPW Shell */
static int ExecuteViaMPW(const char *command, char *output, long maxOutput) {
    OSErr err;

    /* Write command to a temp script file */
    /* ... FSWrite to "Temp:BridgeScript" ... */

    /*
     * Launch MPW Shell with the script.
     * MPW Shell supports: MPW Shell < scriptFile > outputFile
     * We use LaunchApplication or Process Manager to run it.
     */

    /* Read output file contents into output buffer */
    /* ... FSRead from "Temp:BridgeOutput" ... */

    return 0; /* exit status */
}

/* Timer task callback — runs every POLL_INTERVAL ms */
static TMTask gTimerTask;

static pascal void PollCallback(TMTaskPtr task) {
    int hasCommand;

    /* Check if host has a command for us */
    hasCommand = EmulOp_Call(0 /* CHECK_COMMAND */, gBuffer);

    if (hasCommand) {
        char output[BUFFER_SIZE];
        int status;

        /* Execute the command */
        status = ExecuteViaMPW(gBuffer, output, BUFFER_SIZE);

        /* Send result back to host */
        /* Pack status + output into gBuffer */
        memcpy(gBuffer, output, strlen(output) + 1);
        EmulOp_Call(1 /* SEND_RESULT */, gBuffer);
    }

    /* Re-prime the timer */
    PrimeTime((QElemPtr)&gTimerTask, POLL_INTERVAL);
}

/* INIT entry point — called at boot */
void main(void) {
    /* Install recurring timer task */
    gTimerTask.tmAddr = NewTimerUPP(PollCallback);
    gTimerTask.tmCount = 0;
    gTimerTask.tmWakeUp = 0;
    gTimerTask.tmReserved = 0;
    InsXTime((QElemPtr)&gTimerTask);
    PrimeTime((QElemPtr)&gTimerTask, POLL_INTERVAL);

    /*
     * Don't return — the timer task persists in memory.
     * The INIT code resource stays loaded because the
     * Time Manager holds a reference to our callback.
     */
}
```

### API Endpoints

New endpoints in `src/webserver/api_handlers.cpp`:

```
POST /api/exec
  Body: {"command": "SC test.c"}
  Response: {"id": 1}

  Queues a command for the bridge agent.

GET /api/exec/{id}
  Response: {"id": 1, "status": "complete", "exit_code": 0,
             "output": "File 'test.c'; line 12 # Error ..."}

  Returns command result. Status is "pending" or "complete".

POST /api/files
  Body: {"path": "HD:Dev:test.c", "content": "#include <stdio.h>\n..."}

  Writes a file to the Mac filesystem via the bridge.

GET /api/files?path=HD:Dev:test.c
  Response: {"path": "HD:Dev:test.c", "content": "..."}

  Reads a file from the Mac filesystem via the bridge.
```

### Build & Deploy Workflow

Full development cycle from Linux:

```bash
# 1. Write source code on Linux
cat > test.c << 'EOF'
#include <Quickdraw.h>
void main() {
    InitGraf(&thePort);
    /* ... */
}
EOF

# 2. Push source file into the running emulator
curl -X POST http://localhost:8000/api/files \
  -d '{"path": "HD:Dev:test.c", "content": "'"$(cat test.c)"'"}'

# 3. Compile inside the emulator using MPW
curl -X POST http://localhost:8000/api/exec \
  -d '{"command": "SC -o HD:Dev:test.c.o HD:Dev:test.c"}'

# 4. Link
curl -X POST http://localhost:8000/api/exec \
  -d '{"command": "Link -o HD:Dev:TestApp HD:Dev:test.c.o {Libraries}Interface.o -t APPL -c ?????"}'

# 5. Check result
curl http://localhost:8000/api/exec/2
# {"status": "complete", "exit_code": 0, "output": ""}

# 6. Run the app (optional — via Finder or direct launch)
curl -X POST http://localhost:8000/api/exec \
  -d '{"command": "TestApp"}'
```

## Alternative: Cross-Compilation with Retro68

For building the bridge INIT itself (and simple apps without needing MPW inside the emulator):

[Retro68](https://github.com/autc04/Retro68) is a GCC-based cross-compiler that runs on Linux and produces classic Mac executables.

```bash
# Build Retro68 toolchain (one-time)
git clone https://github.com/autc04/Retro68.git
cd Retro68 && mkdir build && cd build
../build-toolchain.sh

# Cross-compile the bridge INIT
m68k-apple-macos-gcc -o BridgeINIT BridgeINIT.c \
  -lRetroConsole -lInterface

# Convert to MacBinary and copy to disk image
# (Retro68's build system handles resource forks via Rez)
```

Retro68 is also useful for building simple test applications without the full MPW stack.

## Alternative: ksherlock/mpw

[mpw](https://github.com/ksherlock/mpw) is an MPW Shell reimplementation that runs MPW tools **natively on Linux** via a built-in 68k emulator. No Mac OS emulator needed for compilation:

```bash
# Run MPW tools directly on Linux
mpw SC -o test.c.o test.c
mpw Link -o TestApp test.c.o ...
```

This gives fast iteration for compilation, with MacPhoenix used only for testing and running the resulting applications.

## Implementation Plan

### Phase 1: Template Disks
- [ ] Create provisioning script (`tools/provision-disk.sh`)
- [ ] Build System 7.5.5 template with hfsutils
- [ ] Document sourcing Mac OS installers and MPW

### Phase 2: EmulOp Bridge
- [ ] Add `COMMAND_BRIDGE` EmulOp to `emul_op.cpp`
- [ ] Add `/api/exec` and `/api/files` endpoints
- [ ] Host-side command queue and result storage

### Phase 3: Bridge INIT
- [ ] Build bridge INIT with Retro68
- [ ] Timer-based polling of EmulOp
- [ ] MPW Shell integration for command execution
- [ ] Include INIT in provisioned template disks

### Phase 4: Developer Experience
- [ ] CLI tool wrapping the API (`tools/mac-exec.sh`)
- [ ] Makefile integration (edit on Linux, compile inside Mac)
- [ ] Error output parsing (MPW errors -> host-readable format)
