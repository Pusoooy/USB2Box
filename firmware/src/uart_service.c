#include "uart_service.h"

typedef struct {
    uint8_t rx[UART_RX_SIZE], tx[UART_TX_SIZE];
    uint32_t rx_read, lost, tx_head, tx_tail;
    uint16_t tx_in_flight;
    bool configured, receiving;
} uart_state;

static uart_state ports[2];

static void halt(resources *pool, unsigned port)
{
    uart_state *state = &ports[port];
    if (state->configured) uart_driver_stop(port);
    state->configured = state->receiving = false;
    state->tx_head = state->tx_tail = 0;
    state->tx_in_flight = 0;
    resource_release(pool, (uint8_t)(port + 1));
}

void uart_stop_all(resources *pool)
{
    for (unsigned port = 0; port < 2; ++port) halt(pool, port);
}

static uint16_t configure(resources *pool, unsigned port, uint32_t baud,
                          uint8_t parity, uint8_t stop_bits)
{
    if (baud < 1200 || baud > 3000000 || parity > 2 ||
        (stop_bits != 1 && stop_bits != 2)) return STATUS_VALUE;
    uart_state *state = &ports[port];
    if (state->configured) return STATUS_BUSY;

    const uint8_t owner = (uint8_t)(port + 1);
    resource_plan plan = {0};
    bool valid;
    if (port == 0) {
        resource_add(&plan, RESOURCE_PIN('B', 6));
        resource_add(&plan, RESOURCE_PIN('B', 7));
        valid = resource_dma_add(&plan, pool, owner, "USART1_RX", 2, 2, 4) &&
                resource_dma_add(&plan, pool, owner, "USART1_TX", 2, 7, 4);
    } else {
        resource_add(&plan, RESOURCE_PIN('A', 2));
        resource_add(&plan, RESOURCE_PIN('A', 3));
        valid = resource_dma_add(&plan, pool, owner, "USART2_RX", 1, 5, 4) &&
                resource_dma_add(&plan, pool, owner, "USART2_TX", 1, 6, 4);
    }
    if (!valid) return STATUS_BUSY;
    uint8_t conflict;
    const uint16_t status = resource_reserve(pool, &plan, owner, &conflict);
    if (status != STATUS_OK) return status;
    if (!uart_driver_configure(port, baud, parity, stop_bits)) {
        uart_driver_stop(port);
        resource_release(pool, owner);
        return STATUS_IO;
    }
    state->configured = true;
    state->rx_read = state->lost = 0;
    return STATUS_OK;
}

static uint32_t update_read_position(uart_state *state, uint32_t produced)
{
    const uint32_t available = produced - state->rx_read;
    if (available <= UART_RX_SIZE) return available;
    state->lost += available - UART_RX_SIZE;
    state->rx_read = produced - UART_RX_SIZE;
    return UART_RX_SIZE;
}

uint16_t uart_command(resources *pool, uint8_t opcode, const uint8_t *input,
                      uint16_t length, uint8_t *out, uint16_t *written)
{
    if (!length || input[0] >= 2) return STATUS_VALUE;
    const unsigned port = input[0];
    uart_state *state = &ports[port];
    if (opcode == 1)
        return length == 7 ? configure(pool, port, wire_u32(input + 1),
                                       input[5], input[6]) : STATUS_VALUE;
    if (opcode == 5 && length == 1) {
        halt(pool, port);
        return STATUS_OK;
    }
    if (!state->configured) return STATUS_SESSION;
    switch (opcode) {
    case 2:
        if (length != 1) return STATUS_VALUE;
        if (state->receiving) return STATUS_BUSY;
        state->receiving = uart_driver_start(port, state->rx, sizeof state->rx);
        return state->receiving ? STATUS_OK : STATUS_IO;
    case 3: {
        if (length != 3 || !state->receiving) return STATUS_VALUE;
        uint16_t count = wire_u16(input + 1);
        if (count > DEVICE_FRAGMENT) return STATUS_VALUE;
        const uint32_t available = update_read_position(state, uart_driver_rx_count(port));
        if (count > available) count = (uint16_t)available;
        const uint32_t start = state->rx_read;
        for (unsigned i = 0; i < count; ++i) out[i] = state->rx[(start + i) % UART_RX_SIZE];
        update_read_position(state, uart_driver_rx_count(port));
        if (state->rx_read != start)
            return STATUS_FULL;
        state->rx_read += count;
        *written = count;
        return STATUS_OK;
    }
    case 4: {
        const uint16_t count = (uint16_t)(length - 1);
        if (count > UART_TX_SIZE - (state->tx_head - state->tx_tail)) return STATUS_FULL;
        for (unsigned i = 0; i < count; ++i)
            state->tx[state->tx_head++ % UART_TX_SIZE] = input[i + 1];
        wire_put16(out, count);
        *written = 2;
        return STATUS_OK;
    }
    case 6:
        if (length != 1) return STATUS_VALUE;
        if (state->receiving) update_read_position(state, uart_driver_rx_count(port));
        wire_put32(out, state->lost);
        wire_put32(out + 4, uart_driver_errors(port));
        wire_put32(out + 8, state->tx_head - state->tx_tail);
        *written = 12;
        return STATUS_OK;
    default:
        return STATUS_OPCODE;
    }
}

void uart_poll(void)
{
    for (unsigned port = 0; port < 2; ++port) {
        uart_state *state = &ports[port];
        if (!state->configured || !uart_driver_tx_idle(port)) continue;
        state->tx_tail += state->tx_in_flight;
        state->tx_in_flight = 0;
        const unsigned offset = state->tx_tail % UART_TX_SIZE;
        uint32_t count = state->tx_head - state->tx_tail;
        if (count > UART_TX_SIZE - offset) count = UART_TX_SIZE - offset;
        if (count && uart_driver_send(port, state->tx + offset, (uint16_t)count))
            state->tx_in_flight = (uint16_t)count;
    }
}
