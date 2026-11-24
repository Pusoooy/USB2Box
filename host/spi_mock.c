#include "spi_service.h"

static bool selected;
static uint16_t result, received;
static uint16_t fault_count, fault_status;

void spi_mock_fault(uint16_t count, uint16_t status)
{
    fault_count = count;
    fault_status = status;
}

uint32_t spi_driver_init(const spi_config *config) { return config->hz; }
void spi_driver_deinit(void) { selected = false; }
void spi_driver_select(bool active) { selected = active; }
bool spi_driver_start(uint8_t *tx, uint8_t *rx, uint16_t bytes)
{
    if (!selected) return false;
    received = fault_status && fault_count < bytes ? fault_count : bytes;
    for (unsigned i = 0; i < received; ++i) rx[i] = tx[i];
    result = fault_status;
    fault_status = STATUS_OK;
    return true;
}
uint16_t spi_driver_result(void) { return result; }
uint16_t spi_driver_received(void) { return received; }
void spi_driver_abort(void) { result = STATUS_CANCELLED; }
