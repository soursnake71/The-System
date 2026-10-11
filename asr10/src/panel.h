#ifndef PANEL_H
#define PANEL_H

void panel_clear(void);
void panel_goto(unsigned row, unsigned col);
void panel_puts(const char *s);

/* Sends one byte to the front panel. Provided by the hardware layer. */
void panel_tx(unsigned char byte);

#endif
