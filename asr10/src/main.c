#include "panel.h"

int main(void)
{
    panel_clear();
    panel_goto(0, 0);
    panel_puts("THE SYSTEM");
    panel_goto(1, 0);
    panel_puts("ASR-10 OS 0.0");
    for (;;)
        ;
}
