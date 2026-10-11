/* Hardware layer for the ASR-10 main board (MC68302).
 *
 * Nothing here talks to real hardware yet. Which 68302 serial channel (or
 * the 2681 DUART) carries the panel link, at what baud rate, and where the
 * 68302 register block sits after the boot ROM has set it up all have to be
 * read out of the stock OS first. Until then panel_tx() is a stub, so the
 * image builds and links but shows nothing.
 */
#include "panel.h"

void panel_tx(unsigned char byte)
{
    (void)byte; /* TODO: write to the panel serial port */
}
