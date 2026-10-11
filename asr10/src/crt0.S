/* Entry point: the first byte of the image. Masks interrupts, sets a stack,
   clears .bss and calls main. */
    .section .text.start, "ax"
    .global _start
_start:
    move.w  #0x2700, %sr            /* supervisor, all interrupts masked */
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
