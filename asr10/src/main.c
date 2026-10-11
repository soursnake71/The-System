/* First boot test.
 *
 * Shows the OS is running in two ways that don't depend on anything we
 * haven't confirmed:
 *   - clears the display and prints plain uppercase text. 0xD6 is clear on
 *     MAME's VFX display and 0x83 on its SQ-1 one; on each, the other code
 *     is ignored or only moves the cursor. Text 0x20-0x5F prints on both;
 *   - plays middle C on MIDI channel 1, once a second, on MIDI Out.
 */
#include "hw.h"

static void panel_puts(const char *s)
{
    for (; *s; s++)
        panel_tx((unsigned char)*s);
}

static void midi_note(unsigned char on)
{
    midi_tx(on ? 0x90 : 0x80);
    midi_tx(60);
    midi_tx(on ? 100 : 0);
}

int main(void)
{
    panel_tx(0xD6);
    panel_tx(0x83);
    panel_puts(" THE SYSTEM ");
    for (;;) {
        midi_note(1);
        delay(150000);
        midi_note(0);
        delay(450000);
    }
}
