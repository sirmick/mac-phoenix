# MacEmu Test ROM Suite

This directory contains a comprehensive test suite for the MacEmu emulator, focusing on edge cases and difficult-to-emulate scenarios.

## Test Results Summary

| Test ROM | UAE Backend | Unicorn Backend | Description |
|----------|-------------|-----------------|-------------|
| edge_test_suite.rom | ✅ PASS (D7=18) | ✅ PASS (D7=18) | Basic boundary tests |
| a6_skip_test.rom | ✅ PASS (D7=6) | ✅ PASS (D7=6) | A6 boundary skip bug test |
| advanced_edge_tests.rom | ❌ CRASH | ❌ TIMEOUT | Complex boundary operations |

## Test ROM Descriptions

### 1. edge_test_suite.rom
**Magic:** 0x45444745 ('EDGE')
**Purpose:** Comprehensive test of basic edge cases
**Tests:**
- Setting address registers to boundaries (7 tests)
- Memory reads at boundaries (3 tests)
- Memory writes at boundaries (2 tests)
- Stack operations at boundary (4 tests)
- Indirect addressing with boundaries (3 tests)

**Expected Results:**
- D7 = 0x12 (18 decimal) - test counter
- D0 = 0xDEADBEEF - success marker

### 2. a6_skip_test.rom
**Magic:** 0x41365350 ('A6SP')
**Purpose:** Test the specific A6=0x02000000 instruction skipping bug
**Tests:**
- Sets A6 to normal address (0x01000000)
- Executes instructions (should work)
- Sets A6 to boundary (0x02000000)
- Executes more instructions (was being skipped in bug)

**Expected Results:**
- D7 = 6 - all register checks pass
- D0 = 0xDEADBEEF - success
- D0 = 0xBADC0DE0 - failure (if bug present)

### 3. advanced_edge_tests.rom
**Magic:** 0x41445645 ('ADVE')
**Purpose:** Complex boundary crossing scenarios
**Tests:**
- Boundary crossing with different instruction sizes (2 tests)
- PC-relative operations across boundaries (2 tests)
- Indexed addressing modes with boundary bases (2 tests)
- Complex indirect operations (2 tests)
- Pre/post increment/decrement operations (2 tests)

**Expected Results:**
- D7 = 11 - test counter
- D0 = 0xCAFEBABE - success marker

**Current Issues:**
- Causes UAE to segfault
- Causes Unicorn to timeout
- Needs further investigation

### 4. Other Test ROMs

#### quadra_halt.rom
**Magic:** 0x54524F4D ('TROM')
**Purpose:** Minimal ROM that just halts
**Use:** Basic sanity check

#### test_halt.bin
**Purpose:** Absolute minimal test - just STOP instruction
**Use:** Verify basic CPU initialization

#### boundary_test.rom
**Magic:** 0x424F554E ('BOUN')
**Purpose:** Early boundary test prototype

#### a6_boundary_test.rom
**Magic:** 0x41364254 ('A6BT')
**Purpose:** Initial A6 boundary bug investigation

## Building Test ROMs

Each test ROM has a corresponding Python build script:
```bash
python3 build_edge_test_suite.py
python3 build_a6_skip_test.py
python3 build_advanced_edge_tests.py
# etc...
```

## Running Tests

### Individual Test
```bash
# UAE backend
env EMULATOR_TIMEOUT=1 CPU_BACKEND=uae ../build/macemu-next --rom <test.rom> --no-webserver

# Unicorn backend
env EMULATOR_TIMEOUT=1 CPU_BACKEND=unicorn ../build/macemu-next --rom <test.rom> --no-webserver
```

### With Tracing
```bash
# Trace first 100 instructions
env EMULATOR_TIMEOUT=1 CPU_BACKEND=uae CPU_TRACE=0-100 ../build/macemu-next --rom <test.rom> --no-webserver
```

## Known Issues

1. **Advanced edge tests crash both backends**
   - UAE: Segmentation fault in m68k_do_execute()
   - Unicorn: Timeout (possible infinite loop)
   - Likely related to complex memory operations at boundaries

2. **Original A6 boundary bug (FIXED)**
   - Unicorn was skipping instructions after setting A6 to 0x02000000
   - Fixed by reducing batch execution size
   - Both backends now pass a6_skip_test.rom

## Test ROM Magic Values

All test ROMs use a 32-bit magic value at offset 0x10 to identify them:
- 0x54524F4D - 'TROM' - Test ROM
- 0x424F554E - 'BOUN' - Boundary test
- 0x41364254 - 'A6BT' - A6 Boundary test
- 0x45444745 - 'EDGE' - Edge case suite
- 0x41445645 - 'ADVE' - Advanced edge tests
- 0x41365350 - 'A6SP' - A6 skip test

The ROM patcher checks for these magic values and skips patching for test ROMs.

## Future Work

1. Debug and fix the advanced_edge_tests crashes
2. Add tests for:
   - Exception handling at boundaries
   - Interrupt handling during boundary operations
   - MMU-related boundary issues
   - Cache effects at boundaries
3. Create automated test runner script
4. Add performance benchmarks for boundary operations