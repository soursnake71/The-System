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

/* Sets channel A up for MIDI from scratch: 8 data bits, no parity, 1 stop
 * bit, no handshake, and both directions clocked from external pins at 16x
 * (clock select E). That is how MAME wires Ensoniq's VFX boards, which
 * feed 500 kHz to those pins: 500 kHz / 16 = 31,250 baud. On the ASR-10,
 * switching the transmitter on without this left it never ready to send. */
void midi_setup(void)
{
    DUART_CRA = DUART_CR_TX_RST;
    DUART_CRA = DUART_CR_RX_RST;
    DUART_CRA = DUART_CR_ERR_RST;
    DUART_CRA = DUART_CR_MR_PTR;
    DUART_MRA = 0x13;   /* MR1: no parity, 8 bits */
    DUART_MRA = 0x07;   /* MR2: normal mode, no CTS/RTS, 1 stop bit */
    DUART_CSRA = 0xEE;  /* external clock pins, 16x, receive and transmit */
    DUART_CRA = DUART_CR_TX_ON | 0x01;  /* transmitter and receiver on */
}

int midi_tx(unsigned char byte)
{
    return duart_tx(&DUART_SRA, &DUART_THRA, byte);
}

/* Sends one byte without waiting, for commands whose answer the caller
 * reads itself (the ROM sends 7F FF FD this way). */
int panel_tx_raw(unsigned char byte)
{
    return duart_tx(&DUART_SRB, &DUART_THRB, byte);
}

/* Returns the next byte from the panel, or -1 after that many polls. */
int panel_rx_wait(unsigned long polls)
{
    while (polls--)
        if (DUART_SRB & DUART_RXRDY)
            return DUART_RHRB;
    return -1;
}

/* Sends one byte, then gives the panel time to take it: waits until a
 * byte comes back (the ROM's handshake reads one) or PANEL_REPLY_TIMEOUT
 * polls pass. Anything received is handed to panel_rx_push, not dropped:
 * after keyboard start-up the panel streams data constantly, and an
 * earlier version that discarded input lost button presses. */
int panel_tx(unsigned char byte)
{
    unsigned long n;

    if (duart_tx(&DUART_SRB, &DUART_THRB, byte))
        return -1;
    for (n = 0; n < PANEL_REPLY_TIMEOUT; n++) {
        if (DUART_SRB & DUART_RXRDY) {
            panel_rx_push(DUART_RHRB);
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
