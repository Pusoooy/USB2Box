#include "app.h"
#include "uart_service.h"
#include "i2c_service.h"

static resources pool;

void app_init(void) { resources_init(&pool); }
void app_stop(void) { uart_stop_all(&pool); i2c_stop_all(&pool); }
void app_poll(uint32_t now) { uart_poll(); i2c_poll(now); }

uint16_t app_command(uint8_t service, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *output, uint16_t *written, uint32_t now)
{
    if (service == 1) return i2c_command(&pool, opcode, input, length, output, written, now);
    if (service == 0 && (opcode == SYS_CANCEL || opcode == SYS_COLLECT) && length == 4)
        return i2c_job_command(opcode, wire_u32(input), output, written);
    if (service == 3) return uart_command(&pool, opcode, input, length, output, written);
    if (service == 0 && opcode == SYS_RESOURCES && length == 0) {
        for (unsigned i = 0; i < RESOURCE_COUNT; ++i) output[i] = pool.owner[i];
        *written = RESOURCE_COUNT;
        return STATUS_OK;
    }
    return STATUS_OPCODE;
}
