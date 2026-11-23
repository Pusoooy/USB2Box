#ifndef USB2BOX_RESOURCES_H
#define USB2BOX_RESOURCES_H

#include "device.h"

enum {
    RESOURCE_DMA_BASE = 48, RESOURCE_TIM_BASE = 64,
    RESOURCE_ADC = 76, RESOURCE_COUNT = 77, RESOURCE_PLAN_MAX = 16,
    RESOURCE_SYSTEM_OWNER = 255,
};
#define RESOURCE_PIN(port, pin) ((uint8_t)(((port) - 'A') * 16 + (pin)))
#define RESOURCE_STREAM(dma, stream) ((uint8_t)(RESOURCE_DMA_BASE + ((dma) - 1) * 8 + (stream)))
#define RESOURCE_TIMER(timer) ((uint8_t)(RESOURCE_TIM_BASE + (timer)))

typedef struct {
    uint8_t owner[RESOURCE_COUNT], last_conflict;
} resources;

typedef struct {
    uint8_t count, ids[RESOURCE_PLAN_MAX];
} resource_plan;

void resources_init(resources *pool);
bool resource_add(resource_plan *plan, uint8_t id);
bool resource_dma_add(resource_plan *plan, const resources *pool, uint8_t owner,
                      const char *request, unsigned dma, unsigned stream, unsigned channel);
uint16_t resource_reserve(resources *pool, const resource_plan *plan,
                          uint8_t owner, uint8_t *conflict);
void resource_release(resources *pool, uint8_t owner);

#endif
