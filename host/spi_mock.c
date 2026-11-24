#include "spi_service.h"

static bool selected, dma;
static uint16_t result, received;
static uint16_t fault_count, fault_status;

bool spi_mock_selected(void) { return selected; }

void spi_mock_fault(uint16_t count, uint16_t status)
{
    fault_count = count;
    fault_status = status;
}

uint32_t spi_driver_init(const spi_config *config) { return config->hz; }
void spi_driver_deinit(void) { selected = false; }
bool spi_driver_enable_dma(void) { dma = true; return true; }
void spi_driver_disable_dma(void) { dma = false; }
void spi_driver_select(bool active) { selected = active; }
bool spi_driver_start(const uint8_t *tx, uint8_t *rx, uint16_t bytes)
{
    if (!selected || (bytes > SPI_IRQ_MAX_BYTES && !dma)) return false;
    received = fault_status && fault_count < bytes ? fault_count : bytes;
    for (unsigned i = 0; i < received; ++i) rx[i] = tx[i];
    result = fault_status;
    fault_status = STATUS_OK;
    return true;
}
uint16_t spi_driver_result(void) { return result; }
uint16_t spi_driver_abort(void) { result = STATUS_CANCELLED; return received; }
