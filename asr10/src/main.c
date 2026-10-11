/* First boot test.
 *
 * Shows the OS is running in two ways that don't depend on anything we
 * haven't confirmed:
 *   - prints plain uppercase text on the display (every Ensoniq display
 *     model in MAME prints 0x20-0x5F at the cursor without commands);
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
    panel_puts(" THE SYSTEM ");
    for (;;) {
        midi_note(1);
        delay(150000);
        midi_note(0);
        delay(450000);
    }
}
