#include "bsp.h"

int main(void)
{
    bsp_init();
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
