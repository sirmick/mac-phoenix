/* Simplified IRQ EmulOp test ROM */
/* Tests the 0x7129 encoding vs 0xAE29 */

    .text

/* Initial SSP and PC vectors */
    .long   0x00020000      /* Initial SSP */
    .long   start           /* Initial PC */

/* ROM version at offset 8 - use 32-bit clean ROM version for DIRECT_ADDRESSING */
    .word   0x067c          /* ROM_VERSION_32 */

/* Skip rest of vector table */
    .skip   0xF6

start:
    /* Set up environment */
    move.w  #0x2000, %sr    /* Supervisor mode */

    /* Write start marker */
    move.l  #0x53544152, 0x1000    /* 'STAR' */

    /* Test correct EmulOp encoding */
    move.l  #0, %d0
    move.l  #0, %d1         /* Counter */

loop1:
    .word   0x7129          /* IRQ EmulOp - correct */
    tst.l   %d0
    bne.s   got_irq

    addq.l  #1, %d1
    cmpi.l  #100, %d1
    blt.s   loop1

    /* No IRQ after 100 tries */
    move.l  #0x4E4F4951, 0x1004    /* 'NOIQ' */
    bra.s   test2

got_irq:
    /* Got IRQ */
    move.l  #0x49525121, 0x1004    /* 'IRQ!' */
    move.l  %d1, 0x1008            /* Save count */

test2:
    /* Test wrong A-line encoding */
    move.l  #0, %d0
    move.l  #0, %d1

loop2:
    .word   0xAE29          /* Wrong - A-line */
    tst.l   %d0
    bne.s   got_aline

    addq.l  #1, %d1
    cmpi.l  #100, %d1
    blt.s   loop2

    /* No response from A-line */
    move.l  #0x4E4F414C, 0x100C    /* 'NOAL' */
    bra.s   done

got_aline:
    /* Got response from A-line */
    move.l  #0x414C4F4B, 0x100C    /* 'ALOK' */
    move.l  %d1, 0x1010            /* Save count */

done:
    /* Write completion marker */
    move.l  #0x444F4E45, 0x1100    /* 'DONE' */

    /* Halt */
    stop    #0x2700