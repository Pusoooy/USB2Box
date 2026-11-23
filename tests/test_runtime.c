#include "jobs.h"
#include "resources.h"
#include <assert.h>
#include <stdio.h>

static unsigned calls[16];
static uint16_t advance(job *task, bool cancel)
{
    ++calls[task->id];
    if (cancel) return STATUS_CANCELLED;
    task->data[task->complete++] = 42;
    return JOB_PENDING;
}

int main(void)
{
    resources pool;
    resources_init(&pool);
    resource_plan plan = {0};
    uint8_t conflict = 0;
    assert(resource_add(&plan, RESOURCE_PIN('B', 6)));
    assert(resource_add(&plan, RESOURCE_PIN('A', 13)));
    assert(resource_reserve(&pool, &plan, 1, &conflict) == STATUS_BUSY);
    assert(conflict == RESOURCE_PIN('A', 13) && pool.owner[RESOURCE_PIN('B', 6)] == 0);
    --plan.count;
    assert(!resource_dma_add(&plan, &pool, 1, "USART1_RX", 2, 2, 3));
    assert(resource_dma_add(&plan, &pool, 1, "USART1_RX", 2, 2, 4));
    assert(!resource_dma_add(&plan, &pool, 1, "SPI1_RX", 2, 2, 3));
    assert(resource_reserve(&pool, &plan, 1, &conflict) == STATUS_OK);
    resource_release(&pool, 1);
    assert(pool.owner[RESOURCE_STREAM(2, 2)] == 0);
    assert(pool.owner[RESOURCE_PIN('A', 13)] == RESOURCE_SYSTEM_OWNER);

    jobs queue = {0};
    const uint32_t now = UINT32_MAX - 2;
    assert(job_submit(&queue, 1, 0, NULL, 0, now, 5) == STATUS_OK);
    assert(job_submit(&queue, 2, 0, NULL, 0, now, 4) == STATUS_OK);
    assert(job_submit(&queue, 3, 1, NULL, 0, now, 7) == STATUS_OK);
    assert(job_submit(&queue, 1, 1, NULL, 0, now, 10) == STATUS_DUPLICATE);
    for (unsigned id = 4; id <= 6; ++id)
        assert(job_submit(&queue, id, 0, NULL, 0, now, 10) == STATUS_OK);
    assert(job_submit(&queue, 7, 0, NULL, 0, now, 10) == STATUS_FULL);
    jobs_poll(&queue, advance, now);
    assert(calls[1] == 1 && calls[2] == 0 && calls[3] == 1);
    assert(job_cancel(&queue, 3, advance) == STATUS_OK);
    assert(calls[3] == 2);
    jobs_poll(&queue, advance, now + 5);
    assert(calls[2] == 0);
    uint8_t out[DEVICE_FRAGMENT];
    uint16_t count;
    assert(job_collect(&queue, 2, out, &count) == STATUS_OK);
    assert(count == 4 && wire_u16(out) == STATUS_TIMEOUT);
    assert(job_collect(&queue, 3, out, &count) == STATUS_OK);
    assert(count == 5 && wire_u16(out) == STATUS_CANCELLED && out[4] == 42);
    jobs_reset(&queue, advance);
    puts("resource atomic reservation and bounded job deadlines, contention and cancellation passed");
}
