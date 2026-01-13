/* Test ROM for A-line trap handling */
/* This ROM tests that A-line exceptions work correctly */

    .text

/* Initial SSP and PC vectors */
    .long   0x00020000      /* Initial SSP (128KB into RAM) */
    .long   0x02000800      /* Initial PC - start of code */

/* Vector table (offset 0x08 - 0x3FF) */
    /* Vectors 2-9: Bus error, Address error, etc. */
    .long   0x02000900, 0x02000900, 0x02000900, 0x02000900
    .long   0x02000900, 0x02000900, 0x02000900, 0x02000900
    /* Vector 10: A-line trap */
    .long   0x02000860
    /* Vector 11: F-line trap  */
    .long   0x02000900
    /* Fill rest with unhandled */
    .fill   984, 1, 0xFF

/* Code starts at offset 0x800 */
    .balign 2048
code_start:
    /* Test 1: Simple A-line trap that should be handled and continue */
    move.l  #0x12345678, %d0    /* Set a test value in D0 */
    .word   0xA247              /* A-line trap (arbitrary value) */
    /* If we get here, the trap was handled and returned correctly */
    move.l  #0x87654321, %d1    /* Set success marker in D1 */

    /* Test 2: Multiple consecutive A-line traps */
    .word   0xA001
    .word   0xA002
    .word   0xA003

    /* Success - write success pattern to memory */
    move.l  #0xDEADBEEF, %d0
    move.l  %d0, 0x1000
    move.l  #0x600D600D, %d0
    move.l  %d0, 0x1004

done:
    /* Infinite loop */
    bra.s   done

    /* Padding to handler */
    .balign 16
    .fill   64, 1, 0

/* A-line exception handler at offset 0x860 */
aline_handler:
    /* Save registers */
    movem.l %d0-%d2/%a0-%a2, -(%sp)

    /* Get the PC from the exception stack frame */
    /* Stack layout: saved_regs(24) + SR(2) + PC(4) + format(2) */
    move.l  26(%sp), %a0        /* Get PC from stack frame */

    /* Read the A-line opcode */
    move.w  (%a0), %d0          /* Read the opcode */

    /* Store it in memory for debugging */
    move.w  %d0, 0x2000

    /* Adjust return PC to skip the A-line instruction */
    addq.l  #2, 26(%sp)         /* Add 2 to saved PC */

    /* Restore registers */
    movem.l (%sp)+, %d0-%d2/%a0-%a2

    /* Return from exception */
    rte

    /* Padding to unhandled */
    .balign 256

/* Unhandled exception handler at offset 0x900 */
unhandled:
    /* Unhandled exception - halt */
    move.l  #0xDEADDEAD, %d0
    move.l  %d0, 0x1000
    bra.s   unhandled