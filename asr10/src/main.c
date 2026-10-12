/* Panel explorer and ROM dump.
 *
 * Read-only towards the panel: it prints plain uppercase text and never
 * sends control codes, since unknown codes may mean something to the panel
 * controller.
 *
 *   - Display, first a short MIDI port report: "A" then channel A's status
 *     (hex; bit 2 = ready to send) as the ROM left it, after switching the
 *     transmitter on, and after setting the port up from scratch; then
 *     "TX" once the first ROM message has gone out, or "NOTX" if the port
 *     never became ready. Kept short: on the ASR-10 a longer report wrapped
 *     and overwrote itself.
 *     Then "KEYS" and, for every byte the panel sends (button presses), its
 *     value in hex.
 *   - MIDI Out: the boot ROM, 0xF80000-0xFBFFFF, as a loop of SysEx
 *     messages (format in tools/syx2rom.py). One pass takes about three
 *     minutes, then it starts again.
 */
#include "hw.h"

#define ROM_START 0xF80000UL
#define ROM_END   0xFC0000UL
#define CHUNK     256
#define FRAME     (5 + 4 + 2 * CHUNK + 2)  /* header, address, nibbles, sum, F7 */

static unsigned char frame[FRAME];

static void build_frame(unsigned long addr)
{
    const volatile unsigned char *rom = (const volatile unsigned char *)addr;
    unsigned char sum = 0;
    int i, n = 0;

    frame[n++] = 0xF0;
    frame[n++] = 0x7D;  /* non-commercial manufacturer ID */
    frame[n++] = 'A';
    frame[n++] = 'S';
    frame[n++] = 'R';
    frame[n++] = (addr >> 21) & 0x7F;
    frame[n++] = (addr >> 14) & 0x7F;
    frame[n++] = (addr >> 7) & 0x7F;
    frame[n++] = addr & 0x7F;
    for (i = 0; i < CHUNK; i++) {
        unsigned char b = rom[i];
        frame[n++] = b >> 4;
        frame[n++] = b & 0x0F;
        sum += (b >> 4) + (b & 0x0F);
    }
    frame[n++] = sum & 0x7F;
    frame[n++] = 0xF7;
}

static void panel_puts(const char *s)
{
    for (; *s; s++)
        panel_tx((unsigned char)*s);
}

static void panel_hex(unsigned char b)
{
    static const char digits[] = "0123456789ABCDEF";

    panel_tx(' ');
    panel_tx(digits[b >> 4]);
    panel_tx(digits[b & 0x0F]);
}

/* How long to wait for the first ROM message to go out before reporting
 * NOTX, in loop passes: a message takes about 170 ms at MIDI speed. */
#define FIRST_FRAME_TIMEOUT 2000000UL

int main(void)
{
    unsigned long addr = ROM_START, spins = 0;
    int pos = 0, reported = 0;

    panel_puts(" A");
    panel_hex(DUART_SRA);
    midi_init();
    panel_hex(DUART_SRA);
    midi_setup();
    panel_hex(DUART_SRA);
    build_frame(addr);
    for (;;) {
        if (!reported && ++spins == FIRST_FRAME_TIMEOUT) {
            panel_puts(" NOTX KEYS");
            reported = 1;
        }
        if (DUART_SRA & DUART_TXRDY) {
            DUART_THRA = frame[pos++];
            if (pos == FRAME) {
                if (!reported) {
                    panel_puts(" TX KEYS");
                    reported = 1;
                }
                addr += CHUNK;
                if (addr >= ROM_END)
                    addr = ROM_START;
                build_frame(addr);
                pos = 0;
            }
        }
        if (DUART_SRB & DUART_RXRDY)
            panel_hex(DUART_RHRB);
    }
}
