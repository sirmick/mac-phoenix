# QEMU M68K/PPC Extraction Strategy Analysis

**Date:** December 25, 2024
**Purpose:** Evaluate all options for integrating QEMU CPU emulation with BasiliskII/SheepShaver

---

## Current Situation

### What You've Built ✅
- **738KB static library** from 902 QEMU object files
- **Three-layer stub strategy** to resolve dependencies
- **Compiles and links** perfectly with BasiliskII
- **390 lines of stubs** for QEMU subsystems you don't need

### The Problem ❌
- **QEMU's QOM (Object Model) system** requires full initialization
- **Type registration is static** and not designed for library use
- **902 object files** is still pulling in too much QEMU infrastructure
- **Complex dependency chain** makes maintenance difficult

---

## Size Comparison

```
Full QEMU:           1.1 GB (your build directory)
Your QEMU library:   738 KB (902 .o files)
BasiliskII:          37 MB
SheepShaver:         6 MB
```

**The Reality:** Even at 738KB, you're still carrying QEMU's QOM system, module system, runstate system, and other infrastructure you don't need.

---

## Option 1: Use Unicorn Engine (RECOMMENDED ⭐)

### What is Unicorn?

Unicorn Engine is **exactly what you're trying to build**: A lightweight CPU-only emulation framework extracted from QEMU.

**Key Facts:**
- Based on QEMU 5.0+ (has M68K and PPC support)
- **Stripped all non-CPU subsystems** from QEMU
- Clean C API designed for embedding
- **No QOM, no module system, no stubs needed**
- Active project (Unicorn2 released in 2022)
- Used in production by many reverse engineering tools

### What Unicorn Provides

```c
// Simple, clean API - no QOM nonsense
uc_engine *uc;
uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);

// Map memory
uc_mem_map(uc, 0x0, 8*1024*1024, UC_PROT_ALL);  // 8MB RAM

// Write ROM/RAM
uc_mem_write(uc, 0x400000, rom_data, rom_size);

// Set registers
uc_reg_write(uc, UC_M68K_REG_PC, 0x400000);
uc_reg_write(uc, UC_M68K_REG_A7, 0x800000);

// Execute
uc_emu_start(uc, 0x400000, 0x400100, 0, 0);  // Execute 1 instruction

// Read registers
uint32_t pc;
uc_reg_read(uc, UC_M68K_REG_PC, &pc);
```

**No stubs needed. No QOM. No module system. Just CPU emulation.**

### Pros ✅
- **Solves your exact problem** - CPU-only emulation
- **Clean API** - designed for embedding, not full system emulation
- **Small size** - M68K backend is ~500KB (vs your 738KB with stubs)
- **No initialization issues** - no QOM/module system
- **Proven** - used by Qiling, Triton, Capstone, and many security tools
- **Active development** - Unicorn2 based on QEMU 5.0
- **Multi-architecture** - PPC support for SheepShaver too!
- **No stubs required** - clean separation from QEMU internals

### Cons ❌
- **External dependency** - need to build/link Unicorn
- **API learning curve** - different from raw QEMU
- **May lag behind QEMU** - Unicorn2 based on QEMU 5.0 (not latest)
- **Less control** - can't patch QEMU internals easily

### Implementation Effort: **2-3 days**

1. **Day 1:** Build Unicorn with M68K support, create C wrapper
2. **Day 2:** Integrate with BasiliskII, test basic execution
3. **Day 3:** Implement dual-CPU comparison, test with Mac ROM

### How to Do It

```bash
# Build Unicorn
git clone https://github.com/unicorn-engine/unicorn.git
cd unicorn
mkdir build && cd build
cmake .. -DUNICORN_ARCH="m68k;ppc"
make -j8
# Result: libunicorn.a (~500KB for M68K)

# Link into BasiliskII
# Add to configure.ac:
LIBS="$LIBS -lunicorn"

# Replace qemu_c_wrapper.c with unicorn_wrapper.c:
// Much simpler - no QOM, no stubs needed!
```

---

## Option 2: Extract QEMU TCG+M68K into Standalone Library

### What This Means

Create a **minimal CPU-only library** by forking QEMU and removing everything except:
- `target/m68k/` - M68K CPU implementation
- `tcg/` - Tiny Code Generator (JIT)
- `include/exec/` - Minimal execution headers
- `qom/` - ONLY what's needed for CPU objects (not full QOM)

**This is essentially what Unicorn did**, but you'd maintain it yourself.

### Pros ✅
- **Full control** - patch QEMU exactly as needed
- **Latest QEMU** - not stuck on Unicorn's QEMU version
- **Minimal size** - could get down to ~300KB
- **No external dependency** - your own code

### Cons ❌
- **Massive work** - essentially creating Unicorn from scratch
- **Maintenance burden** - need to update when QEMU changes
- **Complex** - still need to understand QOM minimally
- **Risk** - might miss critical QEMU internals

### Implementation Effort: **2-3 weeks**

1. **Week 1:** Strip QEMU down to TCG + M68K only
2. **Week 2:** Remove QOM or create minimal QOM subset
3. **Week 3:** Test, debug, create stable API

### How to Do It

```bash
# Fork QEMU
git clone https://gitlab.com/qemu-project/qemu.git qemu-minimal
cd qemu-minimal

# Remove everything except:
- target/m68k/
- tcg/
- include/exec/
- accel/tcg/
- qom/ (minimal subset)

# Create new build system (meson)
# Define minimal API (similar to Unicorn)
# Test with BasiliskII
```

**Verdict:** Only do this if Unicorn doesn't work or you need bleeding-edge QEMU features.

---

## Option 3: Move BasiliskII/SheepShaver INTO QEMU

### What This Means

Instead of extracting QEMU CPU into BasiliskII, **embed BasiliskII into QEMU** as a new machine type.

```c
// In QEMU's hw/m68k/macintosh.c
static void macintosh_init(MachineState *machine) {
    // BasiliskII's ROM patches
    // BasiliskII's Mac hardware emulation
    // Use QEMU's M68K CPU directly
}

DEFINE_MACHINE("macintosh-ii", macintosh_machine_init)
```

Then run: `qemu-system-m68k -M macintosh-ii -bios mac-rom.bin`

### Pros ✅
- **No extraction needed** - use QEMU as-is
- **Full QEMU features** - debugger, monitor, snapshots
- **Proper initialization** - QOM works perfectly
- **Upstream potential** - could be merged into QEMU mainline

### Cons ❌
- **Total rewrite** - BasiliskII's architecture doesn't fit QEMU's model
- **Lost independence** - now tied to QEMU's release cycle
- **Different user experience** - QEMU UI vs BasiliskII's UI
- **Massive effort** - essentially rewriting BasiliskII

### Implementation Effort: **2-3 months**

This is a complete architectural change.

### How to Do It

```bash
# Create new QEMU machine type
cd qemu/hw
mkdir macintosh
cd macintosh

# Implement QEMU machine:
# - mac_ii.c (Macintosh II emulation)
# - mac_rom.c (ROM patches)
# - mac_hardware.c (VIA, SCC, SCSI)

# Port BasiliskII logic to QEMU's device model

# Build QEMU with new machine
./configure --target-list=m68k-softmmu --enable-mac
make
```

**Verdict:** Interesting for long-term, but too much work for immediate goal.

---

## Option 4: Separate Process Architecture (IPC)

### What This Means

Run QEMU M68K in a **separate process**, communicate via shared memory or sockets.

```
┌─────────────────┐         ┌──────────────────┐
│  BasiliskII     │         │  qemu-m68k       │
│  (Main Process) │         │  (CPU Process)   │
│                 │         │                  │
│  - UAE CPU      │◄───────►│  - QEMU M68K     │
│  - ROM patches  │  IPC    │  - Execute only  │
│  - Mac hardware │         │                  │
└─────────────────┘         └──────────────────┘
```

### Pros ✅
- **Clean separation** - no linking issues
- **Process isolation** - QEMU crashes don't kill BasiliskII
- **Easy QEMU updates** - just rebuild separate binary
- **Could use stock QEMU** - no modifications needed

### Cons ❌
- **IPC overhead** - slower than in-process
- **Complex state sync** - memory, registers, interrupts
- **Two processes** - more complex to manage
- **Latency** - may be too slow for instruction-level comparison

### Implementation Effort: **1-2 weeks**

### How to Do It

```c
// In BasiliskII:
void execute_with_qemu(uint32_t pc) {
    // Send PC to QEMU process
    write(qemu_sock, &pc, sizeof(pc));

    // QEMU executes instruction

    // Read back result
    CPUState state;
    read(qemu_sock, &state, sizeof(state));
}
```

**Verdict:** Good for testing/comparison, but too slow for production.

---

## Option 5: Keep Current Approach, Fix QOM Init

### What This Means

**Fix the QOM initialization issue** you're currently stuck on.

### The Solution

Looking at Unicorn's source, they solved this by:

1. **Calling type registration directly**:
```c
// In unicorn/qemu/target/m68k/cpu.c
void m68k_cpu_register_types(void) {
    type_register_static(&m68k_cpu_type_info);
    // Register all M68K CPU types
}

// In your wrapper:
extern void m68k_cpu_register_types(void);

void qemu_cpu_create(void) {
    module_call_init(MODULE_INIT_QOM);
    m68k_cpu_register_types();  // Direct call

    CPUState *cs = cpu_create("m68040-m68k-cpu");  // Now works
}
```

2. **Bypassing QOM entirely** for simple CPU creation:
```c
// From Unicorn's approach:
M68kCPU *cpu = g_malloc0(sizeof(M68kCPU));
object_initialize(cpu, sizeof(M68kCPU), "m68040-m68k-cpu");
object_property_set_bool(OBJECT(cpu), "realized", true, &error_abort);
```

### Pros ✅
- **Minimal new code** - fix existing approach
- **Keep your work** - 738KB library, stubs, etc.
- **Full control** - can patch QEMU as needed

### Cons ❌
- **Still complex** - 902 object files, many stubs
- **Maintenance burden** - tracking QEMU changes
- **May hit more issues** - QOM is just the first problem

### Implementation Effort: **1-2 days**

Just need to:
1. Export `m68k_cpu_register_types()` in meson.build
2. Call it before `cpu_create()`
3. Test

**Verdict:** Worth trying as a short-term fix, but Unicorn is better long-term.

---

## Comparison Matrix

| Option | Effort | Maintenance | Size | Control | Recommended? |
|--------|--------|-------------|------|---------|--------------|
| **1. Unicorn Engine** | 2-3 days | Low | 500KB | Medium | ⭐⭐⭐⭐⭐ **YES** |
| 2. Extract QEMU TCG | 2-3 weeks | High | 300KB | Full | ⭐⭐ Only if Unicorn fails |
| 3. Move to QEMU | 2-3 months | Low | N/A | Low | ⭐ Long-term only |
| 4. IPC Process | 1-2 weeks | Medium | 0 | Medium | ⭐⭐ Testing only |
| 5. Fix QOM Init | 1-2 days | High | 738KB | Full | ⭐⭐⭐ Short-term fix |

---

## My Recommendation: **Use Unicorn Engine** 🎯

### Why Unicorn is the Best Choice

1. **Solves Your Exact Problem**
   - You want M68K CPU emulation without QEMU infrastructure
   - Unicorn was built for exactly this use case

2. **Proven Solution**
   - Used by Qiling Framework, Triton, AFL++, and many others
   - Thousands of projects depend on it
   - Battle-tested in production

3. **Clean API**
   - No QOM, no module system, no stubs
   - Designed for embedding from day one
   - Simple `uc_open()`, `uc_emu_start()`, `uc_reg_read()`

4. **Small Size**
   - M68K backend: ~500KB (vs your 738KB with stubs)
   - PPC backend: ~600KB (for SheepShaver)
   - Combined: ~1MB for both architectures

5. **Active Development**
   - Unicorn2 released in 2022
   - Based on QEMU 5.0
   - Regular updates and bug fixes

6. **Multi-Architecture**
   - M68K for BasiliskII ✅
   - PPC for SheepShaver ✅
   - One library, both emulators

### Implementation Plan (3 days)

**Day 1: Build & Test Unicorn**
```bash
# Build Unicorn with M68K + PPC
git clone https://github.com/unicorn-engine/unicorn.git
cd unicorn
mkdir build && cd build
cmake .. -DUNICORN_ARCH="m68k;ppc" -DCMAKE_BUILD_TYPE=Release
make -j8
sudo make install  # Or link statically

# Test basic M68K emulation
./samples/sample_m68k
```

**Day 2: Create BasiliskII Wrapper**
```c
// qemu-cpu/unicorn_wrapper.c (replaces qemu_c_wrapper.c)
#include <unicorn/unicorn.h>

static uc_engine *g_uc = NULL;

bool unicorn_cpu_create(void) {
    uc_err err = uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &g_uc);
    return err == UC_ERR_OK;
}

bool unicorn_setup_ram(uint64_t addr, void* host_ptr, uint64_t size) {
    // Map memory
    uc_err err = uc_mem_map_ptr(g_uc, addr, size, UC_PROT_ALL, host_ptr);
    return err == UC_ERR_OK;
}

bool unicorn_execute_one(void) {
    uint32_t pc;
    uc_reg_read(g_uc, UC_M68K_REG_PC, &pc);

    // Execute 1 instruction
    uc_err err = uc_emu_start(g_uc, pc, 0xFFFFFFFF, 0, 1);
    return err == UC_ERR_OK;
}

// Much simpler than QEMU's QOM!
```

**Day 3: Test & Validate**
```c
// Test with BasiliskII
1. Load Mac ROM
2. Execute first 1000 instructions with Unicorn
3. Compare with UAE CPU
4. Verify correctness
```

### What You Gain

- ✅ **No more stubs** - delete 390 lines of basilisk_qemu_stubs.c
- ✅ **No more QOM** - no initialization issues
- ✅ **Smaller binary** - 500KB vs 738KB
- ✅ **Proven solution** - used by thousands of projects
- ✅ **Both CPUs** - M68K + PPC in one library

### What You Lose

- ❌ **Bleeding-edge QEMU** - stuck on QEMU 5.0 (Unicorn2)
  - *But: QEMU 5.0 M68K is mature and complete*
- ❌ **Direct QEMU patches** - can't modify QEMU internals easily
  - *But: Can fork Unicorn if needed*

---

## Alternative: Fix Current Approach (If You Insist)

If you want to keep your current QEMU integration, here's the fix:

### Create `m68k_type_registration.c` in qemu-cpu/

```c
/*
 * M68K CPU Type Registration Helper
 *
 * This file provides a simple function to register M68K CPU types
 * into QEMU's QOM system, bypassing the module initialization system.
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "cpu.h"

// Import the M68K CPU type array (defined in QEMU's target/m68k/cpu.c)
// We'll need to make this extern or copy it here
extern const TypeInfo m68k_cpus_type_infos[];

/**
 * Register all M68K CPU types with QOM
 * Call this BEFORE calling cpu_create()
 */
void register_m68k_cpu_types(void) {
    // Count how many types (array is NULL-terminated)
    int count = 0;
    while (m68k_cpus_type_infos[count].name != NULL) {
        count++;
    }

    // Register the array
    type_register_static_array(m68k_cpus_type_infos, count);
}
```

### Update meson.build

```meson
# Add line 91:
m68k_registration = files('m68k_type_registration.c')

# Update line 117 to compile it:
libqemu_basilisk = static_library(
  'qemu-basilisk',
  basilisk_stubs,
  m68k_registration,  # ← Compile our registration helper
  objects: all_objects,
  # ...
)
```

### Update qemu_c_wrapper.c

```c
QEMUCPUHandle* qemu_cpu_create(void) {
    extern void register_m68k_cpu_types(void);  // Our helper

    // Initialize QOM
    module_call_init(MODULE_INIT_QOM);

    // Register M68K types directly
    register_m68k_cpu_types();

    // Now this should work
    CPUState *cs = cpu_create("m68040-m68k-cpu");
    // ...
}
```

**Problem:** The `m68k_cpus_type_infos` array is static in QEMU's cpu.c. You'll need to either:
1. Modify QEMU's `target/m68k/cpu.c` to export it
2. Copy the array into your `m68k_type_registration.c`

**This is why Unicorn is better** - they already solved all this.

---

## Final Recommendation

### For Immediate Success: **Use Unicorn Engine**

**Timeline:**
- **Day 1:** Build Unicorn, test basic M68K emulation
- **Day 2:** Create wrapper, integrate with BasiliskII
- **Day 3:** Test with Mac ROM, compare with UAE

**Result:**
- Clean, working M68K emulation
- No stubs, no QOM issues
- Production-ready code
- Also works for SheepShaver (PPC)

### For Long-Term (Optional): **Contribute to Unicorn**

If you need newer QEMU features:
1. Use Unicorn now (get it working)
2. Fork Unicorn later
3. Update to latest QEMU (6.0+)
4. Contribute back to Unicorn project

This way you help the community and get the latest QEMU.

---

## Resources

### Unicorn Engine
- **Website:** https://www.unicorn-engine.org/
- **GitHub:** https://github.com/unicorn-engine/unicorn
- **Docs:** https://www.unicorn-engine.org/docs/
- **Blog:** https://www.unicorn-engine.org/docs/beyond_qemu.html

### Building Unicorn
```bash
# Clone
git clone https://github.com/unicorn-engine/unicorn.git
cd unicorn

# Build
mkdir build && cd build
cmake .. \
  -DUNICORN_ARCH="m68k;ppc" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF  # Static library
make -j8

# Install (or just link build/libunicorn.a)
sudo make install
```

### Example Code
```c
// samples/sample_m68k.c in Unicorn repo
// Shows how to:
// - Initialize M68K CPU
// - Map memory
// - Set registers
// - Execute code
// - Hook instructions
```

---

## Questions to Consider

1. **Do you need bleeding-edge QEMU features?**
   - If yes: Fix current approach or extract QEMU TCG
   - If no: Use Unicorn (QEMU 5.0 is fine for M68K)

2. **How much maintenance can you handle?**
   - Low: Use Unicorn
   - Medium: Fix current approach
   - High: Extract QEMU TCG yourself

3. **Do you need both M68K and PPC?**
   - Yes: Unicorn supports both
   - M68K only: Any approach works

4. **Timeline?**
   - Need it working in 3 days: Unicorn
   - Have 2-3 weeks: Extract QEMU TCG
   - Long-term project: Move into QEMU

---

## My Strong Recommendation

🎯 **Use Unicorn Engine**

It solves your exact problem, is proven in production, and will save you weeks of fighting with QEMU's internals. You can always switch later if you need bleeding-edge QEMU features.

**Next steps:**
1. Build Unicorn with M68K support (1 hour)
2. Test basic emulation (1 hour)
3. Create wrapper for BasiliskII (4 hours)
4. Test with Mac ROM (4 hours)
5. **Total: 1-2 days to working solution**

vs. your current approach:
1. Fight QOM initialization (2-3 days) ✅ Already spent
2. Fight next issue (?)
3. Maintain 390 lines of stubs (ongoing)
4. Update when QEMU changes (ongoing)

**Unicorn eliminates all of this.**

---

**Decision Time:** Try Unicorn for 1 day. If it works, great! If not, you can always come back to fixing the QOM issue. But I'm 95% confident Unicorn will solve your problem cleanly.
