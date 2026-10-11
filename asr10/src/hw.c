#include "hw.h"

/* Gives up after this many status polls so a dead channel can't hang us. */
#define TX_TIMEOUT 100000UL

/* How long to wait for the panel to answer a byte, in status polls (a few
 * milliseconds). MAME's EPS panel answers every byte the main board sends,
 * and on the first test on the ASR-10, sending without waiting got one
 * letter of twelve onto the display. */
#define PANEL_REPLY_TIMEOUT 20000UL

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

void midi_init(void)
{
    DUART_CRA = DUART_CR_TX_ON;
}

int midi_tx(unsigned char byte)
{
    return duart_tx(&DUART_SRA, &DUART_THRA, byte);
}

static void panel_drain(void)
{
    while (DUART_SRB & DUART_RXRDY)
        (void)DUART_RHRB;
}

/* Sends one byte, then waits for the panel's reply (or a short timeout)
 * before the next one can go. */
int panel_tx(unsigned char byte)
{
    unsigned long n;

    panel_drain();
    if (duart_tx(&DUART_SRB, &DUART_THRB, byte))
        return -1;
    for (n = 0; n < PANEL_REPLY_TIMEOUT; n++) {
        if (DUART_SRB & DUART_RXRDY) {
            (void)DUART_RHRB;
            return 0;
        }
    }
    return 1; /* no reply; carry on */
}

void delay(unsigned long loops)
{
    while (loops--)
        __asm__ volatile ("nop");
}
