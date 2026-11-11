/* Entry point. Every exception and interrupt vector points here, so we
 * arrive in supervisor mode from whatever the ROM was doing. Mask all
 * interrupts, take a fresh stack, clear .bss and run main. */
    .section .note.GNU-stack, "", @progbits
    .section .text.start, "ax"
    .global _start
_start:
    move.w  #0x2700, %sr
    lea     STACK_TOP, %sp
    lea     __bss_start, %a0
    lea     __bss_end, %a1
1:  cmp.l   %a1, %a0
    bcc.s   2f
    clr.l   (%a0)+
    bra.s   1b
2:  jsr     main
3:  stop    #0x2700
    bra.s   3b
