#ifndef HW_H
#define HW_H

/* 2681 DUART, on odd bytes from 0xFC4800 (register n at 0xFC4801 + 2n).
 * Found in OS 1.61: channel A sends MIDI (it watches for SysEx F0/F7),
 * channel B receives front panel button codes. The boot ROM has set both
 * up by the time our code runs. */
#define DUART_BASE  0xFC4801UL
#define DUART_REG(n) (*(volatile unsigned char *)(DUART_BASE + 2 * (n)))
#define DUART_MRA   DUART_REG(0x0)  /* mode 1 then mode 2, channel A */
#define DUART_SRA   DUART_REG(0x1)  /* status, channel A (read) */
#define DUART_CSRA  DUART_REG(0x1)  /* clock select, channel A (write) */
#define DUART_CRA   DUART_REG(0x2)  /* command, channel A */
#define DUART_THRA  DUART_REG(0x3)  /* transmit, channel A (MIDI out) */
#define DUART_IP    DUART_REG(0xD)  /* input port pins (read) */
#define DUART_SRB   DUART_REG(0x9)  /* status, channel B */
#define DUART_THRB  DUART_REG(0xB)  /* transmit, channel B (panel) */
#define DUART_RHRB  DUART_REG(0xB)  /* receive, channel B (same address, read) */
#define DUART_RXRDY 0x01
#define DUART_TXRDY 0x04
#define DUART_CR_TX_ON  0x04       /* command: enable transmitter */
#define DUART_CR_MR_PTR 0x10       /* command: point at mode register 1 */
#define DUART_CR_RX_RST 0x20       /* command: reset receiver */
#define DUART_CR_TX_RST 0x30       /* command: reset transmitter */
#define DUART_CR_ERR_RST 0x40      /* command: reset error status */

/* OS 3.53 turns the MIDI transmitter on (CRA = 04) only while it has data
 * to send and off (08) otherwise; the ROM sets baud rate and format. */

/* MC68302 register block. OS 1.61 puts it at 0xFC6000: internal RAM at
 * +0x000, registers from +0x800 (it writes IMR at +0x816, ISR at +0x818). */
#define M68302_BASE 0xFC6000UL

void midi_init(void);
void midi_setup(void);
int midi_tx(unsigned char byte);
int panel_tx(unsigned char byte);
void delay(unsigned long loops);

#endif
