# EmulOps vs MMIO: Summary and Recommendation

## Current Situation

### EmulOps (Currently Implemented)
- **Range**: 0x7100-0x713F (overlaps with MOVEQ instructions)
- **Detection**: UC_HOOK_BLOCK at TB boundaries + UC_HOOK_INSN_INVALID for illegal ops
- **Status**: Working but with limitations

### MMIO (Better Alternative)
- **Range**: 0xFF000000-0xFF000FFF (unused high memory)
- **Detection**: Memory access hooks (always trapped)
- **Status**: Partially implemented for trap redirection

## Comparison

| Aspect | EmulOps | MMIO |
|--------|---------|------|
| **Reliability** | Medium - can miss mid-TB | High - always trapped |
| **JIT Compatibility** | Poor - requires TB management | Excellent - works naturally |
| **Performance** | Good - block hook overhead | Good - memory hook overhead |
| **Opcode Space** | Ambiguous (valid MOVEQ) | Clean (memory addresses) |
| **Parameter Passing** | Via registers only | Via memory writes |
| **Return Values** | Via registers only | Via memory reads |
| **Implementation Complexity** | Medium | Low |
| **Debugging** | Harder - looks like normal code | Easier - distinct memory ops |

## Why MMIO is Better for Unicorn

1. **Always Works**: Memory accesses to unmapped regions always trap, regardless of JIT state
2. **No Ambiguity**: Clear distinction from normal instructions
3. **JIT-Friendly**: Doesn't require special TB handling
4. **Flexible Interface**: Can pass arbitrary data through memory
5. **Future-Proof**: Doesn't conflict with CPU instruction extensions

## Implementation Example

### EmulOp Approach (Current)
```asm
; Call EmulOp - might not be detected mid-TB!
moveq #3, d0    ; 0x7103 - RESET EmulOp (looks like MOVEQ!)
; Hope it gets detected...
```

### MMIO Approach (Recommended)
```asm
; Always trapped, even mid-TB
move.l #RESET_CMD, $FF000000    ; Write to MMIO region
; Guaranteed to be detected
```

## Migration Strategy

### Phase 1: Dual Support
Keep EmulOps working while adding MMIO:
```c
// In unicorn_wrapper.c
if (addr >= 0xFF000000 && addr < 0xFF001000) {
    handle_mmio_command(addr, value);
}
```

### Phase 2: ROM Patch Updates
Update ROM patches to use MMIO:
```asm
; Old EmulOp way
dc.w 0x7107  ; PATCH_BOOT_GLOBS

; New MMIO way
move.l #PATCH_BOOT_GLOBS, $FF000000
```

### Phase 3: Test Suite Migration
Convert test ROMs to use MMIO for better reliability.

### Phase 4: Deprecate EmulOps
Keep for compatibility but recommend MMIO for new code.

## Conclusion

**MMIO is clearly superior to EmulOps for Unicorn** because:

1. **100% Reliable** - Always detected, no TB issues
2. **Clean Design** - No instruction/data ambiguity
3. **Better Performance** - No need for block hooks
4. **More Flexible** - Can pass complex parameters
5. **JIT-Optimized** - Works naturally with translation blocks

The only reason to keep EmulOps is backward compatibility with existing ROM patches. New code should use MMIO.

## Recommended Next Steps

1. **Implement full MMIO handler** in `unicorn_wrapper.c`
2. **Create MMIO command definitions** in a header file
3. **Add MMIO test coverage** to the test suite
4. **Document MMIO API** for ROM patch developers
5. **Gradually migrate** existing EmulOps to MMIO

This would give MacEmu a more robust and maintainable interface between 68k code and the emulator, especially important for JIT performance and reliability.