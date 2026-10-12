/* Panel test.
 *
 * Starts the panel the way the boot ROM does (E7, then 71), then uses the
 * display commands found in the ROM: 66 clears the screen and 60 nn moves
 * the cursor to position nn.
 *
 *   Line 1: a title.
 *   Line 2: the last four bytes the panel sent, in hex, updated in place.
 *   MIDI Out: every byte the panel sends, as SysEx F0 7D 'P' hi lo F7
 *   (hi and lo are its two hex digits as numbers 0-15), so a MIDI monitor
 *   shows the whole stream, keyboard included if the panel sends it.
 */
#include "hw.h"

#define PANEL_INIT1   0xE7
#define PANEL_INIT2   0x71
#define PANEL_CLEAR   0x66
#define PANEL_CURSOR  0x60   /* followed by a position */
#define LINE2         40     /* assumed: second line starts at position 40 */

static void panel_puts(const char *s)
{
    for (; *s; s++)
        panel_tx((unsigned char)*s);
}

static void panel_at(unsigned char pos)
{
    panel_tx(PANEL_CURSOR);
    panel_tx(pos);
}

static void panel_hex(unsigned char b)
{
    static const char digits[] = "0123456789ABCDEF";

    panel_tx(digits[b >> 4]);
    panel_tx(digits[b & 0x0F]);
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
    panel_puts("THE SYSTEM   PANEL TEST");
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
                panel_tx(' ');
            }
            panel_puts("          ");
        }
    }
}
