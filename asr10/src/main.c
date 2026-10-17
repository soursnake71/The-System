/* Panel and keyboard test.
 *
 * Follows OS 3.53's start-up order: panel start-up (E7, 71), FC, the
 * ROM's keyboard tuning request up to four times (7F, FF, FD, each sent
 * without waiting; the panel's answer to FD is the result, C9 = failed),
 * then D5 (D0 + the OS's keyboard setting, 5 on this disk). D5 alone left
 * the keyboard silent.
 *
 *   Line 1: "THE SYSTEM", the answer to each tuning attempt ("--" = none),
 *           and later a count of bytes received from the panel.
 *   Line 2: "NEW" the last three bytes that were not 22, 00 or 79 (on the
 *           ASR-10 the panel sends 22 00 and 79 00 continuously after D5),
 *           and "BTN" the last byte of 80 or more (a button press).
 *   Nothing the panel sends is dropped, even while the display is drawn.
 *   MIDI Out: every byte the panel sends, as SysEx F0 7D 'P' hi lo F7
 *   (hi and lo are its two hex digits as numbers 0-15), sent as they
 *   arrive, so a MIDI monitor shows the whole stream.
 *
 * Display commands (confirmed on the ASR-10): 66 clear, 60 nn cursor
 * (0-79, line 2 starts at 40), 77 c steady character.
 */
#include "hw.h"

#define PANEL_INIT1     0xE7
#define PANEL_INIT2     0x71
#define PANEL_CLEAR     0x66
#define PANEL_CURSOR    0x60
#define PANEL_CHAR      0x77
#define PANEL_KEYBOARD  0xD5   /* D0 + OS 3.53's keyboard setting (5) */
#define PANEL_PRE_TUNE  0xFC   /* sent by OS 3.53 where it requests tuning */
#define TUNE_FAILED     0xC9
#define TUNE_TRIES      4
#define TUNE_GAP        200000UL   /* polls between 7F, FF and FD */
#define TUNE_WAIT       20000000UL /* polls to wait for the answer (seconds) */
#define LINE2           40

#define RING            256    /* bytes waiting to go out on MIDI */
#define REDRAW          300000UL /* loop passes between display redraws */

static unsigned char ring[RING];
static unsigned int head, tail;

/* What the display shows, updated as bytes arrive. */
static unsigned char fresh[3], button;
static unsigned long count;
static int dirty;

void panel_rx_push(unsigned char b)
{
    int i;

    if ((head + 1) % RING != tail) {
        ring[head] = b;
        head = (head + 1) % RING;
    }
    if (b != 0x22 && b != 0x00 && b != 0x79) {
        for (i = 0; i < 2; i++)
            fresh[i] = fresh[i + 1];
        fresh[2] = b;
    }
    if (b >= 0x80)
        button = b;
    count++;
    dirty = 1;
}

static void panel_at(unsigned char pos)
{
    panel_tx(PANEL_CURSOR);
    panel_tx(pos);
}

static void panel_putc(char c)
{
    panel_tx(PANEL_CHAR);
    panel_tx((unsigned char)c);
}

static void panel_puts(const char *s)
{
    for (; *s; s++)
        panel_putc(*s);
}

static void panel_hex(unsigned char b)
{
    static const char digits[] = "0123456789ABCDEF";

    panel_putc(digits[b >> 4]);
    panel_putc(digits[b & 0x0F]);
}

/* MIDI output state: each queued panel byte goes out as 6 SysEx bytes. */
static unsigned char msg[6];
static int msg_pos = 6;

static void midi_pump(void)
{
    if (msg_pos == 6) {
        unsigned char b;

        if (head == tail)
            return;
        b = ring[tail];
        tail = (tail + 1) % RING;
        msg[0] = 0xF0;
        msg[1] = 0x7D;
        msg[2] = 'P';
        msg[3] = b >> 4;
        msg[4] = b & 0x0F;
        msg[5] = 0xF7;
        msg_pos = 0;
    }
    if (DUART_SRA & DUART_TXRDY)
        DUART_THRA = msg[msg_pos++];
}

int main(void)
{
    unsigned long since = 0;
    int i;

    midi_setup();
    panel_tx(PANEL_INIT1);
    panel_tx(PANEL_INIT2);
    panel_tx(PANEL_CLEAR);
    panel_at(0);
    panel_puts("THE SYSTEM");
    panel_at(LINE2);
    panel_puts("TUNING KEYBOARD");

    panel_tx(PANEL_PRE_TUNE);
    panel_at(11);
    panel_puts("TUNE");
    for (i = 0; i < TUNE_TRIES; i++) {
        int answer;

        /* Each byte's echo is consumed by panel_tx; the answer to FD is
         * either its reply (if not an echo) or the next byte after it. */
        panel_tx(0x7F);
        delay(TUNE_GAP);
        panel_tx(0xFF);
        delay(TUNE_GAP);
        answer = panel_tx(0xFD);
        if (answer < 0 || answer == 0xFD)
            answer = panel_rx_wait(TUNE_WAIT);
        panel_putc(' ');
        if (answer < 0) {
            panel_puts("--");
        } else {
            panel_hex((unsigned char)answer);
            if (answer != TUNE_FAILED)
                break;
        }
    }

    panel_tx(PANEL_KEYBOARD);
    panel_at(LINE2);
    panel_puts("PLAY KEYS OR PRESS BUTTONS");

    for (;;) {
        if (DUART_SRB & DUART_RXRDY)
            panel_rx_push(DUART_RHRB);
        if (dirty && ++since > REDRAW) {
            panel_at(28);
            panel_puts("BYTES ");
            panel_hex((unsigned char)(count >> 8));
            panel_hex((unsigned char)count);
            panel_at(LINE2);
            panel_puts("NEW ");
            for (i = 0; i < 3; i++) {
                panel_hex(fresh[i]);
                panel_putc(' ');
            }
            panel_puts(" BTN ");
            panel_hex(button);
            panel_puts("          ");
            dirty = 0;
            since = 0;
        }
        midi_pump();
    }
}
