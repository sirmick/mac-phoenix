/* Test ROM for IRQ EmulOp and interrupt handling */
/* Fixed version with proper section placement */

    .text
    .org 0x0

/* Initial SSP and PC vectors */
    .long   0x00020000      /* Initial SSP (128KB into RAM) */
    .long   0x02000100      /* Initial PC - start of code */

/* Vector table (offset 0x08 - 0xFF) */
    /* Vectors 2-23: Various exceptions */
    .fill   22, 4, 0x02000400
    /* Vectors 24-31: Autovectors for interrupts 0-7 */
    .long   0x02000400      /* Spurious/unused */
    .long   0x02000500      /* Level 1 - Timer (vector 25) */
    .long   0x02000400      /* Level 2 */
    .long   0x02000400      /* Level 3 */
    .long   0x02000400      /* Level 4 */
    .long   0x02000400      /* Level 5 */
    .long   0x02000400      /* Level 6 */
    .long   0x02000400      /* Level 7 - NMI */

    /* Fill to code start */
    .fill   48, 4, 0xFFFFFFFF

/* Code starts at offset 0x100 */
    .org 0x100
code_start:
    /* Initialize test environment */
    move.l  #0, %d0         /* Clear D0 for IRQ result */
    move.l  #0, %d1         /* Clear counter */
    move.l  #0, %d2         /* Clear test flags */
    move.w  #0x2000, %sr    /* Supervisor mode, interrupts enabled */

    /* Store start marker */
    move.l  #0x53544152, %d3       /* 'STAR' */
    move.l  %d3, 0x1000

test_irq_emulop:
    /* Test 1: Direct IRQ EmulOp call (should use 0x7129) */
    move.l  #0, %d0
    .word   0x7129          /* IRQ EmulOp - CORRECT encoding */
    tst.l   %d0             /* Check if interrupt pending */
    bne.s   irq_received

    /* No interrupt yet, increment counter and loop */
    addq.l  #1, %d1
    cmpi.l  #10000, %d1     /* Loop max 10000 times */
    blt.s   test_irq_emulop

    /* Timeout - no interrupt received */
    move.l  #0x54494D45, %d3       /* 'TIME' - timeout marker */
    move.l  %d3, 0x1004
    bra.s   test_failed

irq_received:
    /* IRQ EmulOp returned non-zero, interrupt pending */
    move.l  #0x49525121, %d3       /* 'IRQ!' - success marker */
    move.l  %d3, 0x1004
    move.l  %d1, 0x1008            /* Store loop count */

test_aline_irq:
    /* Test 2: Incorrect A-line encoding (what we're fixing) */
    move.l  #0, %d0
    move.l  #0, %d1

aline_loop:
    .word   0xAE29          /* WRONG encoding - A-line trap */
    tst.l   %d0
    bne.s   aline_irq_received

    addq.l  #1, %d1
    cmpi.l  #10000, %d1
    blt.s   aline_loop

    /* Timeout on A-line version */
    move.l  #0x414C494E, %d3       /* 'ALIN' - A-line timeout */
    move.l  %d3, 0x100C
    bra.s   test_failed

aline_irq_received:
    move.l  #0x414C4F4B, %d3       /* 'ALOK' - A-line worked */
    move.l  %d3, 0x100C
    move.l  %d1, 0x1010            /* Store loop count */

test_tight_loop:
    /* Test 3: Tight polling loop (IRQ storm scenario) */
    move.l  #0, %d3         /* Loop counter */
    move.l  #0, %d0         /* IRQ result */

tight_poll:
    .word   0x7129          /* IRQ EmulOp */
    tst.l   %d0             /* Test result */
    beq.s   tight_poll      /* BEQ.S *-4 (backward branch) */

    /* Got interrupt in tight loop */
    move.l  #0x4C4F4F50, %d3       /* 'LOOP' - loop success */
    move.l  %d3, 0x1014

test_complete:
    /* All tests done - write summary */
    move.l  #0x444F4E45, %d3       /* 'DONE' */
    move.l  %d3, 0x1100

    /* Success - halt with success code */
    move.l  #0x50415353, %d0       /* 'PASS' */
    stop    #0x2700

test_failed:
    /* Test failed - halt with failure code */
    move.l  #0x4641494C, %d0       /* 'FAIL' */
    move.l  %d0, 0x1100
    stop    #0x2700

/* Exception Handlers */
    .org 0x400
unhandled:
    /* Unhandled exception */
    move.l  #0x55484E44, %d0       /* 'UHND' */
    move.l  %d0, 0x2000
    rte

    .org 0x500
timer_handler:
    /* Timer interrupt handler (Level 1, vector 25) */
    movem.l %d0-%d1/%a0, -(%sp)

    /* Increment interrupt counter */
    move.l  0x1020, %d0
    addq.l  #1, %d0
    move.l  %d0, 0x1020

    /* Store marker that we got here */
    move.l  #0x494E5452, %d0       /* 'INTR' */
    move.l  %d0, 0x1024

    /* Restore and return */
    movem.l (%sp)+, %d0-%d1/%a0
    rte