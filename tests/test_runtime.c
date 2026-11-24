#include "jobs.h"
#include "resources.h"
#include "adc_service.h"
#include "buffer_pool.h"
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

static uint16_t wrong_count(job *task, bool cancel)
{
    (void)cancel;
    task->complete = JOB_DATA + 1;
    return STATUS_OK;
}

static void check_buffer_pool(void)
{
    buffer_lease spi = {0}, adc = {0}, extra = {0};
    assert(buffer_take(&spi, JOB_DATA / 2));
    assert(buffer_take(&adc, ADC_CACHE));
    assert(!buffer_take(&extra, 1));
    spi.words[0] = 0xabcd;
    adc.words[0] = 0x4567;
    adc.words[ADC_CACHE - 1] = 0x1234;
    assert(spi.words[0] == 0xabcd);
    buffer_release(&spi);
    assert(buffer_take(&extra, 1));
    extra.words[0] = 42;
    assert(adc.words[0] == 0x4567 && adc.words[ADC_CACHE - 1] == 0x1234);
    buffer_release(&extra);
    buffer_release(&extra);
    buffer_release(&adc);
    assert(buffer_take(&adc, ADC_CACHE));
    assert(buffer_take(&spi, JOB_DATA / 2));
    buffer_release(&adc);
    buffer_release(&spi);
    assert(buffer_take(&extra, BUFFER_BLOCKS * BUFFER_BLOCK_WORDS));
    buffer_release(&extra);
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
    assert(job_submit(&queue, 1, 0, NULL, 0, now, 5) == STATUS_OK);
    jobs_poll(&queue, wrong_count, now);
    assert(job_collect(&queue, 1, out, &count) == STATUS_IO);
    assert(job_submit(&queue, 1, 0, NULL, 0, now, 5) == STATUS_OK);
    check_buffer_pool();
    puts("resource reservation, bounded jobs and shared buffer ownership/recovery passed");
}
