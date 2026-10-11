#include "hw.h"

/* Gives up after this many status polls so a dead channel can't hang us. */
#define TX_TIMEOUT 100000UL

static int duart_tx(volatile unsigned char *status, volatile unsigned char *data,
                    unsigned char byte)
{
    unsigned long n;

    for (n = 0; n < TX_TIMEOUT; n++) {
        if (*status & DUART_TXRDY) {
            *data = byte;
            return 0;
        }
    }
    return -1;
}

int midi_tx(unsigned char byte)
{
    return duart_tx(&DUART_SRA, &DUART_THRA, byte);
}

int panel_tx(unsigned char byte)
{
    return duart_tx(&DUART_SRB, &DUART_THRB, byte);
}

void delay(unsigned long loops)
{
    while (loops--)
        __asm__ volatile ("nop");
}
