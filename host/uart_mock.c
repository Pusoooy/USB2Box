#include "uart_service.h"

static struct {
    uint8_t *rx;
    uint32_t count;
    uint16_t length;
} mock[2];

bool uart_driver_configure(unsigned port, uint32_t baud, uint8_t parity, uint8_t stop_bits)
{
    (void)baud;
    (void)parity;
    (void)stop_bits;
    mock[port].count = 0;
    return true;
}
bool uart_driver_start(unsigned port, uint8_t *buffer, uint16_t length)
{
    mock[port].rx = buffer;
    mock[port].length = length;
    return true;
}
void uart_driver_stop(unsigned port) { mock[port].rx = 0; }
uint32_t uart_driver_rx_count(unsigned port) { return mock[port].count; }
uint32_t uart_driver_errors(unsigned port) { (void)port; return 0; }
bool uart_driver_tx_idle(unsigned port) { (void)port; return true; }
bool uart_driver_send(unsigned port, uint8_t *bytes, uint16_t length)
{
    const unsigned receiver = 1u - port;
    if (mock[receiver].rx) {
        for (unsigned i = 0; i < length; ++i)
            mock[receiver].rx[mock[receiver].count++ % mock[receiver].length] = bytes[i];
    }
    return true;
}
