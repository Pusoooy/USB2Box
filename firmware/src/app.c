#include "app.h"
#include "uart_service.h"
#include "i2c_service.h"
#include "pwm_service.h"
#include "spi_service.h"
#include "adc_service.h"

static resources pool;

void app_init(void) { resources_init(&pool); }
void app_stop(void)
{
    pwm_stop_all(&pool);
    adc_stop_all(&pool);
    spi_stop_all(&pool);
    uart_stop_all(&pool);
    i2c_stop_all(&pool);
}

void app_poll(uint32_t now)
{
    pwm_poll(now);
    adc_poll();
    uart_poll();
    i2c_poll(now);
    spi_poll(now);
}

uint16_t app_command(uint8_t service, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *output, uint16_t *written, uint32_t now)
{
    uint16_t status;
    pool.last_conflict = 0xff;
    switch (service) {
    case SERVICE_I2C:
        status = i2c_command(&pool, opcode, input, length, output, written, now);
        break;
    case SERVICE_UART:
        status = uart_command(&pool, opcode, input, length, output, written);
        break;
    case SERVICE_PWM:
        status = pwm_command(&pool, opcode, input, length, output, written, now);
        break;
    case SERVICE_SPI:
        status = spi_command(&pool, opcode, input, length, output, written, now);
        break;
    case SERVICE_ADC:
        status = adc_command(&pool, opcode, input, length, output, written);
        break;
    case SERVICE_SYS:
        if ((opcode == SYS_CANCEL || opcode == SYS_COLLECT) && length == 4)
            return i2c_job_command(opcode, wire_u32(input), output, written);
        if (opcode == SYS_RESOURCES && length == 0) {
            for (unsigned i = 0; i < RESOURCE_COUNT; ++i) output[i] = pool.owner[i];
            *written = RESOURCE_COUNT;
            return STATUS_OK;
        }
        return STATUS_OPCODE;
    default:
        return STATUS_OPCODE;
    }
    if (status == STATUS_BUSY && pool.last_conflict != 0xff) {
        output[0] = pool.last_conflict;
        *written = 1;
    }
    return status;
}
