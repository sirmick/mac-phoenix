/*
 * filter_thunk.s - jGNEFilter entry point (pure asm)
 *
 * jGNEFilter calling convention:
 *   A1 = EventRecord pointer
 *   D0 = result flag (low byte)
 *   Must preserve all registers
 *
 * Saves registers, calls handle_commands (C), restores, chains to old filter.
 */
    .text
    .globl filter_thunk
    .globl gOldFilter

filter_thunk:
    movem.l %d0-%d2/%a0-%a2, -(%sp)    | save registers
    bsr.w   handle_commands              | call C handler (PC-relative)
    movem.l (%sp)+, %d0-%d2/%a0-%a2     | restore registers

    | chain to old filter (must use absolute — gOldFilter is a global)
    lea     gOldFilter(%pc), %a0         | PC-relative lea to get address of gOldFilter
    move.l  (%a0), %a0                   | load the pointer value
    cmpa.l  #0, %a0
    beq.s   .Ldone
    jmp     (%a0)
.Ldone:
    rts
