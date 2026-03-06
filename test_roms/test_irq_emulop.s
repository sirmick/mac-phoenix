/* Test ROM for IRQ EmulOp and interrupt handling */
/* This ROM tests the specific issues we're fixing in the implementation plan */

    .text

/* Initial SSP and PC vectors */
    .long   0x00020000      /* Initial SSP (128KB into RAM) */
    .long   0x02000100      /* Initial PC - start of code */

/* Vector table (offset 0x08 - 0xFF) */
    /* Vectors 2-23: Various exceptions */
    .fill   88, 1, 0xFF
    /* Vectors 24-31: Autovectors for interrupts 0-7 */
    .long   0x02000400      /* Spurious/unused */
    .long   0x02000500      /* Level 1 - Timer (vector 25) */
    .long   0x02000400      /* Level 2 */
    .long   0x02000400      /* Level 3 */
    .long   0x02000400      /* Level 4 */
    .long   0x02000400      /* Level 5 */
    .long   0x02000400      /* Level 6 */
    .long   0x02000400      /* Level 7 - NMI */

    /* Fill rest to code start */
    .fill   192, 1, 0xFF

/* Code starts at offset 0x100 */
    .balign 256
code_start:
    /* Initialize test environment */
    move.l  #0, %d0         /* Clear D0 for IRQ result */
    move.l  #0, %d1         /* Clear counter */
    move.l  #0, %d2         /* Clear test flags */
    move.w  #0x2000, %sr    /* Supervisor mode, interrupts enabled */

    /* Store start marker */
    move.l  #0x53544152, 0x1000  /* 'STAR' */

test_irq_emulop:
    /* Test 1: Direct IRQ EmulOp call (should use 0x7129) */
    move.l  #0, %d0
    .word   0x7129          /* IRQ EmulOp - CORRECT encoding */
    tst.l   %d0             /* Check if interrupt pending */
    bne     irq_received

    /* No interrupt yet, increment counter and loop */
    addq.l  #1, %d1
    cmpi.l  #10000, %d1     /* Loop max 10000 times */
    blt     test_irq_emulop

    /* Timeout - no interrupt received */
    move.l  #0x54494D45, 0x1004  /* 'TIME' - timeout marker */
    bra     test_failed

irq_received:
    /* IRQ EmulOp returned non-zero, interrupt pending */
    move.l  #0x49525121, 0x1004  /* 'IRQ!' - success marker */
    move.l  %d1, 0x1008          /* Store loop count */

test_aline_irq:
    /* Test 2: Incorrect A-line encoding (what we're fixing) */
    /* This tests if 0xAE29 is handled properly */
    move.l  #0, %d0
    move.l  #0, %d1

aline_loop:
    .word   0xAE29          /* WRONG encoding - A-line trap */
    tst.l   %d0
    bne     aline_irq_received

    addq.l  #1, %d1
    cmpi.l  #10000, %d1
    blt     aline_loop

    /* Timeout on A-line version */
    move.l  #0x414C494E, 0x100C  /* 'ALIN' - A-line timeout */
    bra     test_failed

aline_irq_received:
    move.l  #0x414C4F4B, 0x100C  /* 'ALOK' - A-line worked */
    move.l  %d1, 0x1010          /* Store loop count */

test_tight_loop:
    /* Test 3: Tight polling loop (IRQ storm scenario) */
    /* This is the problematic pattern from Mac ROM */
    move.l  #0, %d3         /* Loop counter */
    move.l  #0, %d0         /* IRQ result */

tight_poll:
    .word   0x7129          /* IRQ EmulOp */
    tst.l   %d0             /* Test result */
    beq.s   tight_poll      /* BEQ.S *-4 (backward branch) */

    /* Got interrupt in tight loop */
    move.l  #0x4C4F4F50, 0x1014  /* 'LOOP' - loop success */

test_interrupt_delivery:
    /* Test 4: Check if timer interrupt is actually delivered */
    move.l  #0, 0x1020      /* Clear interrupt counter */
    move.w  #0x2000, %sr    /* Enable interrupts */

    /* Wait for interrupt (busy loop) */
    move.l  #100000, %d4
wait_int:
    subq.l  #1, %d4
    bne     wait_int

    /* Check if interrupt handler was called */
    move.l  0x1020, %d0
    tst.l   %d0
    beq     no_timer_int

    /* Timer interrupt worked */
    move.l  #0x54494D52, 0x1018  /* 'TIMR' - timer success */
    bra     test_complete

no_timer_int:
    move.l  #0x4E4F494E, 0x1018  /* 'NOIN' - no interrupt */

test_complete:
    /* All tests done - write summary */
    move.l  #0x444F4E45, 0x1100  /* 'DONE' */

    /* Success - halt with success code */
    move.l  #0x50415353, %d0     /* 'PASS' */
    stop    #0x2700

test_failed:
    /* Test failed - halt with failure code */
    move.l  #0x4641494C, %d0     /* 'FAIL' */
    move.l  %d0, 0x1100
    stop    #0x2700

/* ============================================ */
/* Exception Handlers */
/* ============================================ */

    .balign 256
    .org 0x400
unhandled:
    /* Unhandled exception */
    move.l  #0x55484E44, 0x2000  /* 'UHND' */
    rte

    .balign 256
    .org 0x500
timer_handler:
    /* Timer interrupt handler (Level 1, vector 25) */
    movem.l %d0-%d1/%a0, -(%sp)

    /* Increment interrupt counter */
    move.l  0x1020, %d0
    addq.l  #1, %d0
    move.l  %d0, 0x1020

    /* Store marker that we got here */
    move.l  #0x494E5452, 0x1024  /* 'INTR' */

    /* Restore and return */
    movem.l (%sp)+, %d0-%d1/%a0
    rte

/* ============================================ */
/* Test Data Area */
/* ============================================ */

    .data
    .balign 4

test_results:
    .long   0   /* 0x1000 - Start marker */
    .long   0   /* 0x1004 - IRQ test result */
    .long   0   /* 0x1008 - IRQ loop count */
    .long   0   /* 0x100C - A-line test result */
    .long   0   /* 0x1010 - A-line loop count */
    .long   0   /* 0x1014 - Tight loop result */
    .long   0   /* 0x1018 - Timer test result */
    .long   0   /* 0x101C - Reserved */
    .long   0   /* 0x1020 - Interrupt counter */
    .long   0   /* 0x1024 - Interrupt marker */

    .end