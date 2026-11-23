#include "app.h"
#include "uart_service.h"

static resources pool;

void app_init(void) { resources_init(&pool); }
void app_stop(void) { uart_stop_all(&pool); }
void app_poll(uint32_t now) { (void)now; uart_poll(); }

uint16_t app_command(uint8_t service, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *output, uint16_t *written, uint32_t now)
{
    (void)now;
    if (service == 3) return uart_command(&pool, opcode, input, length, output, written);
    if (service == 0 && opcode == SYS_RESOURCES && length == 0) {
        for (unsigned i = 0; i < RESOURCE_COUNT; ++i) output[i] = pool.owner[i];
        *written = RESOURCE_COUNT;
        return STATUS_OK;
    }
    return STATUS_OPCODE;
}
