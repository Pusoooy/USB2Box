#ifndef USB2BOX_UART_SERVICE_H
#define USB2BOX_UART_SERVICE_H

#include "resources.h"

enum {
    UART_RX_SIZE = 4096, UART_TX_SIZE = 2048, UART_STREAM_META = 20,
    UART_STREAM_CONFIG = 7, UART_STREAM_CREDIT = 8,
};

uint16_t uart_command(resources *pool, uint8_t opcode, const uint8_t *input,
                      uint16_t length, uint8_t *out, uint16_t *written);
void uart_stop_all(resources *pool);
void uart_poll(void);
uint16_t uart_stream_payload(unsigned port, uint8_t *out, uint32_t now);

bool uart_driver_configure(unsigned port, uint32_t baud, uint8_t parity, uint8_t stop_bits);
bool uart_driver_start(unsigned port, uint8_t *buffer, uint16_t length);
void uart_driver_stop(unsigned port);
uint32_t uart_driver_rx_count(unsigned port);
uint32_t uart_driver_errors(unsigned port);
bool uart_driver_tx_idle(unsigned port);
bool uart_driver_send(unsigned port, uint8_t *bytes, uint16_t length);

#endif
