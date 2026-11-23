#include "resources.h"

static const struct {
    const char *request;
    uint8_t dma, stream, channel;
} routes[] = {
#include "dma_routes.inc"
};

static bool matches(const char *left, const char *right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

void resources_init(resources *pool)
{
    pool->last_conflict = 0xff;
    for (unsigned i = 0; i < RESOURCE_COUNT; ++i) pool->owner[i] = 0;
    for (unsigned pin = 11; pin <= 14; ++pin)
        pool->owner[RESOURCE_PIN('A', pin)] = RESOURCE_SYSTEM_OWNER;
}

bool resource_add(resource_plan *plan, uint8_t id)
{
    if (id >= RESOURCE_COUNT || plan->count >= RESOURCE_PLAN_MAX) return false;
    for (unsigned i = 0; i < plan->count; ++i)
        if (plan->ids[i] == id) return false;
    plan->ids[plan->count++] = id;
    return true;
}

bool resource_dma_add(resource_plan *plan, const resources *pool, uint8_t owner,
                      const char *request, unsigned dma, unsigned stream, unsigned channel)
{
    if (dma < 1 || dma > 2 || stream > 7 || channel > 7) return false;
    const uint8_t id = RESOURCE_STREAM(dma, stream);
    if (pool->owner[id] && pool->owner[id] != owner) return false;
    for (unsigned i = 0; i < sizeof routes / sizeof routes[0]; ++i) {
        if (routes[i].dma == dma && routes[i].stream == stream &&
            routes[i].channel == channel && matches(routes[i].request, request))
            return resource_add(plan, id);
    }
    return false;
}

uint16_t resource_reserve(resources *pool, const resource_plan *plan,
                          uint8_t owner, uint8_t *conflict)
{
    if (!owner || owner == RESOURCE_SYSTEM_OWNER || !plan->count ||
        plan->count > RESOURCE_PLAN_MAX) return STATUS_VALUE;
    for (unsigned i = 0; i < plan->count; ++i) {
        const uint8_t id = plan->ids[i];
        if (id >= RESOURCE_COUNT) return STATUS_VALUE;
        if (pool->owner[id] && pool->owner[id] != owner) {
            pool->last_conflict = *conflict = id;
            return STATUS_BUSY;
        }
    }
    for (unsigned i = 0; i < plan->count; ++i) pool->owner[plan->ids[i]] = owner;
    return STATUS_OK;
}

void resource_release(resources *pool, uint8_t owner)
{
    if (!owner || owner == RESOURCE_SYSTEM_OWNER) return;
    for (unsigned i = 0; i < RESOURCE_COUNT; ++i)
        if (pool->owner[i] == owner) pool->owner[i] = 0;
}
