/* Panel and keyboard test.
 *
 * Starts the panel the way the ROM does (E7, 71), then sends D5, which OS
 * 3.53 sends after keyboard tuning (D0 + its keyboard setting, 5 on this
 * disk) and which is the likely "keyboard on" command.
 *
 *   Line 1: "THE SYSTEM" and a count of bytes received from the panel.
 *   Line 2: the last four bytes, in hex, redrawn when the panel goes quiet.
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
#define LINE2           40

#define RING            256    /* bytes waiting to go out on MIDI */
#define QUIET           20000UL /* idle polls before the display is redrawn */

static unsigned char ring[RING];
static unsigned int head, tail;

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
    unsigned char last[4] = { 0, 0, 0, 0 };
    unsigned long count = 0, quiet = 0;
    int dirty = 0, i;

    midi_setup();
    panel_tx(PANEL_INIT1);
    panel_tx(PANEL_INIT2);
    panel_tx(PANEL_CLEAR);
    panel_at(0);
    panel_puts("THE SYSTEM");
    panel_at(LINE2);
    panel_puts("PLAY KEYS OR PRESS BUTTONS");
    panel_tx(PANEL_KEYBOARD);

    for (;;) {
        if (DUART_SRB & DUART_RXRDY) {
            unsigned char b = DUART_RHRB;

            if ((head + 1) % RING != tail) {
                ring[head] = b;
                head = (head + 1) % RING;
            }
            for (i = 0; i < 3; i++)
                last[i] = last[i + 1];
            last[3] = b;
            count++;
            dirty = 1;
            quiet = 0;
        } else if (dirty && ++quiet > QUIET) {
            panel_at(20);
            panel_puts("BYTES ");
            panel_hex((unsigned char)(count >> 8));
            panel_hex((unsigned char)count);
            panel_at(LINE2);
            panel_puts("LAST ");
            for (i = 0; i < 4; i++) {
                panel_hex(last[i]);
                panel_putc(' ');
            }
            panel_puts("             ");
            dirty = 0;
        }
        midi_pump();
    }
}
