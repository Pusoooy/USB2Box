int main(void)
{
    /* M0 reset image uses the reset clock and leaves peripherals disabled. */
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
