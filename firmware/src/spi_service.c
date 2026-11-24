#include "spi_service.h"
#include "buffer_pool.h"

enum { SPI_OWNER = 7, SPI_DMA_RX = RESOURCE_STREAM(2, 0), SPI_DMA_TX = RESOURCE_STREAM(2, 3) };
static jobs spi_jobs;
static bool configured, selected, dma_reserved;
static job *active;
static uint8_t word_bytes;
static uint16_t setup_ms, hold_ms;
static uint32_t now_ms, ready_at, release_at;
static buffer_lease rx;

static void abort_transaction(void)
{
    if (selected) spi_driver_select(false);
    selected = false;
    if (active) {
        const uint16_t received = spi_driver_abort();
        active->complete = received > active->length ? active->length : received;
        const uint8_t *bytes = (const uint8_t *)rx.words;
        for (unsigned i = 0; i < active->complete; ++i) active->data[i] = bytes[i];
        active = 0;
    }
}

static uint16_t advance(job *task, bool cancel)
{
    if (cancel || !selected) {
        abort_transaction();
        return STATUS_CANCELLED;
    }
    if (!active) {
        if (now_ms - ready_at >= 0x80000000u) return JOB_PENDING;
        if (spi_driver_start(task->data, (uint8_t *)rx.words, task->length)) {
            active = task;
            return JOB_PENDING;
        }
        abort_transaction();
        return STATUS_IO;
    }
    const uint16_t result = spi_driver_result();
    if (result == JOB_PENDING) return result;
    if (result != STATUS_OK) {
        abort_transaction();
        return result;
    }
    active = 0;
    release_at = now_ms + hold_ms;
    task->complete = task->length;
    const uint8_t *bytes = (const uint8_t *)rx.words;
    for (unsigned i = 0; i < task->complete; ++i) task->data[i] = bytes[i];
    return STATUS_OK;
}

static void release_dma(resources *pool)
{
    if (!dma_reserved) return;
    spi_driver_disable_dma();
    resource_release_item(pool, SPI_OWNER, SPI_DMA_RX);
    resource_release_item(pool, SPI_OWNER, SPI_DMA_TX);
    dma_reserved = false;
}

void spi_poll(resources *pool, uint32_t now)
{
    now_ms = now;
    /* An expired segment invalidates the whole CS interval, even before it starts. */
    if (selected && job_lane_expired(&spi_jobs, 0, now)) abort_transaction();
    jobs_poll(&spi_jobs, advance, now);
    if (!selected) release_dma(pool);
}

void spi_stop_all(resources *pool)
{
    jobs_reset(&spi_jobs, advance);
    abort_transaction();
    release_dma(pool);
    if (configured) spi_driver_deinit();
    configured = false;
    buffer_release(&rx);
    resource_release(pool, SPI_OWNER);
}

static uint16_t configure(resources *pool, const uint8_t *input, uint8_t *out,
                          uint16_t *written)
{
    if (configured) return STATUS_BUSY;
    if (input[1] > 3 || (input[2] != 8 && input[2] != 16) || input[3] > 1 ||
        input[4] > 3 || input[5] > 1 || !wire_u32(input + 6)) return STATUS_VALUE;
    const spi_config config = {
        .mode = input[1], .bits = input[2], .lsb = input[3],
        .cs = input[4], .active_high = input[5], .hz = wire_u32(input + 6),
    };
    const uint8_t cs = config.cs == 0 ? RESOURCE_PIN('B', 12) :
                                       RESOURCE_PIN('C', config.cs - 1);
    resource_plan plan = {0};
    resource_add(&plan, RESOURCE_PIN('A', 5));
    resource_add(&plan, RESOURCE_PIN('A', 6));
    resource_add(&plan, RESOURCE_PIN('A', 7));
    resource_add(&plan, cs);
    uint8_t conflict;
    const uint16_t status = resource_reserve(pool, &plan, SPI_OWNER, &conflict);
    if (status != STATUS_OK) return status;
    if (!buffer_take(&rx, JOB_DATA / sizeof(uint16_t))) {
        resource_release(pool, SPI_OWNER);
        return STATUS_FULL;
    }
    const uint32_t actual_hz = spi_driver_init(&config);
    if (!actual_hz) {
        buffer_release(&rx);
        resource_release(pool, SPI_OWNER);
        return STATUS_IO;
    }
    setup_ms = (uint16_t)((wire_u16(input + 10) + 999u) / 1000u);
    hold_ms = (uint16_t)((wire_u16(input + 12) + 999u) / 1000u);
    configured = true;
    word_bytes = (uint8_t)(config.bits / 8);
    wire_put32(out, actual_hz);
    *written = 4;
    return STATUS_OK;
}

static uint16_t reserve_dma(resources *pool)
{
    resource_plan plan = {0};
    if (!resource_dma_add(&plan, pool, SPI_OWNER, "SPI1_RX", 2, 0, 3)) {
        pool->last_conflict = SPI_DMA_RX;
        return STATUS_BUSY;
    }
    if (!resource_dma_add(&plan, pool, SPI_OWNER, "SPI1_TX", 2, 3, 3)) {
        pool->last_conflict = SPI_DMA_TX;
        return STATUS_BUSY;
    }
    uint8_t conflict;
    const uint16_t status = resource_reserve(pool, &plan, SPI_OWNER, &conflict);
    if (status != STATUS_OK) return status;
    if (!spi_driver_enable_dma()) {
        resource_release_item(pool, SPI_OWNER, SPI_DMA_RX);
        resource_release_item(pool, SPI_OWNER, SPI_DMA_TX);
        return STATUS_IO;
    }
    dma_reserved = true;
    return STATUS_OK;
}

uint16_t spi_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now)
{
    if (!length || input[0] != 0) return STATUS_VALUE;
    if (opcode == 1)
        return length == 14 ? configure(pool, input, out, written) : STATUS_VALUE;
    if (!configured) return STATUS_SESSION;
    switch (opcode) {
    case 2:
        if (length != 1 || selected || job_lane_active(&spi_jobs, 0)) return STATUS_BUSY;
        spi_driver_select(true);
        selected = true;
        ready_at = now + setup_ms;
        release_at = ready_at + hold_ms;
        return STATUS_OK;
    case 3: {
        if (length <= 9 || length > 9 + JOB_DATA || (length - 9) % word_bytes) return STATUS_VALUE;
        if (!selected) return STATUS_SESSION;
        const bool acquire_dma = length - 9 > SPI_IRQ_MAX_BYTES && !dma_reserved;
        if (acquire_dma) {
            const uint16_t status = reserve_dma(pool);
            if (status != STATUS_OK) return status;
        }
        const uint16_t status = job_submit(&spi_jobs, wire_u32(input + 1), 0, input + 9,
                                           length - 9, now, wire_u32(input + 5));
        if (status != STATUS_OK && acquire_dma) release_dma(pool);
        return status;
    }
    case 4:
        if (length != 1 || job_lane_active(&spi_jobs, 0) ||
            (selected && now - release_at >= 0x80000000u)) return STATUS_BUSY;
        abort_transaction();
        release_dma(pool);
        return STATUS_OK;
    case 5:
        if (length != 1) return STATUS_VALUE;
        spi_stop_all(pool);
        return STATUS_OK;
    case 7:
        if (length != 5) return STATUS_VALUE;
        return job_collect(&spi_jobs, wire_u32(input + 1), out, written);
    case 8: {
        if (length != 5) return STATUS_VALUE;
        const uint16_t status = job_cancel(&spi_jobs, wire_u32(input + 1), advance);
        if (status == STATUS_OK) {
            abort_transaction();
            release_dma(pool);
        }
        return status;
    }
    default:
        return STATUS_OPCODE;
    }
}
