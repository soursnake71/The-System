/* Panel test.
 *
 * Starts the panel the way the boot ROM does (E7, then 71), then uses the
 * display commands found in the ROM: 66 clears the screen and 60 nn moves
 * the cursor to position nn.
 *
 *   Line 1: "THE SYSTEM" written with 77 before each character, then "PLAIN"
 *           written without. The ROM sends 77+k before characters it shows
 *           normally and 7A+k before flagged ones; after E7/71 plain text
 *           blinked, so this shows which form is steady.
 *   Line 2: the last four bytes the panel sent, in hex, updated in place
 *           (77 form). Buttons send 80+n when pressed and n when released.
 *   MIDI Out: every byte the panel sends, as SysEx F0 7D 'P' hi lo F7
 *   (hi and lo are its two hex digits as numbers 0-15), so a MIDI monitor
 *   shows the whole stream, keyboard included if the panel sends it.
 */
#include "hw.h"

#define PANEL_INIT1   0xE7
#define PANEL_INIT2   0x71
#define PANEL_CLEAR   0x66
#define PANEL_CURSOR  0x60   /* followed by a position */
#define PANEL_CHAR    0x77   /* followed by a character: steady (to confirm) */
#define LINE2         40     /* assumed: second line starts at position 40 */

static void panel_puts_plain(const char *s)
{
    for (; *s; s++)
        panel_tx((unsigned char)*s);
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

static void panel_at(unsigned char pos)
{
    panel_tx(PANEL_CURSOR);
    panel_tx(pos);
}

static void panel_hex(unsigned char b)
{
    static const char digits[] = "0123456789ABCDEF";

    panel_putc(digits[b >> 4]);
    panel_putc(digits[b & 0x0F]);
}

static void midi_report(unsigned char b)
{
    midi_tx(0xF0);
    midi_tx(0x7D);
    midi_tx('P');
    midi_tx(b >> 4);
    midi_tx(b & 0x0F);
    midi_tx(0xF7);
}

int main(void)
{
    unsigned char last[4] = { 0, 0, 0, 0 };
    int i;

    midi_setup();
    panel_tx(PANEL_INIT1);
    panel_tx(PANEL_INIT2);
    panel_tx(PANEL_CLEAR);
    panel_at(0);
    panel_puts("THE SYSTEM");
    panel_at(20);
    panel_puts_plain("PLAIN");
    panel_at(LINE2);
    panel_puts("PRESS BUTTONS OR KEYS");

    for (;;) {
        if (DUART_SRB & DUART_RXRDY) {
            unsigned char b = DUART_RHRB;

            midi_report(b);
            for (i = 0; i < 3; i++)
                last[i] = last[i + 1];
            last[3] = b;
            panel_at(LINE2);
            panel_puts("LAST ");
            for (i = 0; i < 4; i++) {
                panel_hex(last[i]);
                panel_putc(' ');
            }
            panel_puts("          ");
        }
    }
}
