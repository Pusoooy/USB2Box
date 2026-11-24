#ifndef USB2BOX_SPI_SERVICE_H
#define USB2BOX_SPI_SERVICE_H

#include "jobs.h"
#include "resources.h"

typedef struct {
    uint8_t mode, bits, lsb, cs, active_high;
    uint32_t hz;
} spi_config;

uint16_t spi_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now);
void spi_poll(uint32_t now);
void spi_stop_all(resources *pool);

uint32_t spi_driver_init(const spi_config *config);
void spi_driver_deinit(void);
void spi_driver_select(bool active);
bool spi_driver_start(uint8_t *tx, uint8_t *rx, uint16_t bytes);
uint16_t spi_driver_result(void);
uint16_t spi_driver_received(void);
void spi_driver_abort(void);

#endif
