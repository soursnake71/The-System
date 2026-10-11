/* Front panel display, 2 lines x 40 characters.
 *
 * Command bytes are from MAME's esq2x40_sq1 VFD (src/mame/ensoniq/esqvfd.cpp),
 * which MAME's ASR-10 skeleton uses. They are not yet confirmed on a real
 * ASR-10: check them against the stock OS before trusting them.
 *   0x20-0x7f  printable character at the cursor, cursor advances
 *   0x83       clear display
 *   0x87 rc    move cursor: row in the high nibble, column in the low nibble
 */
#include "panel.h"

#define VFD_CLEAR 0x83
#define VFD_GOTO  0x87

void panel_clear(void)
{
    panel_tx(VFD_CLEAR);
}

void panel_goto(unsigned row, unsigned col)
{
    panel_tx(VFD_GOTO);
    panel_tx((unsigned char)(((row & 0xf) << 4) | (col & 0xf)));
}

void panel_puts(const char *s)
{
    for (; *s; s++)
        if (*s >= 0x20 && *s < 0x7f)
            panel_tx((unsigned char)*s);
}
