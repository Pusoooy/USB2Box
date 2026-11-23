#include "spi_service.h"

static bool selected;
static uint16_t result;

uint32_t spi_driver_init(const spi_config *config) { return config->hz; }
void spi_driver_deinit(void) { selected = false; }
void spi_driver_select(bool active) { selected = active; }
bool spi_driver_start(uint8_t *tx, uint8_t *rx, uint16_t bytes)
{
    if (!selected) return false;
    for (unsigned i = 0; i < bytes; ++i) rx[i] = tx[i];
    result = STATUS_OK;
    return true;
}
uint16_t spi_driver_result(void) { return result; }
void spi_driver_abort(void) { result = STATUS_CANCELLED; }
