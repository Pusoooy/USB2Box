#include "i2c_service.h"

typedef struct {
    uint32_t speed;
    uint8_t address, messages, offset;
    bool configured, slave, waiting;
    uint8_t read_data[JOB_DATA];
} i2c_bus;

static i2c_bus buses[2];
static jobs queue;

static bool init_bus(unsigned bus)
{
    const i2c_bus *b = &buses[bus];
    return i2c_driver_init(bus, b->speed, b->slave, b->address);
}

static uint16_t transfer_step(job *task, bool cancel)
{
    const unsigned bus = task->lane;
    i2c_bus *b = &buses[bus];
    if (cancel) {
        i2c_driver_deinit(bus);
        init_bus(bus);
        b->waiting = false;
        for (unsigned i = 0; i < task->complete; ++i) task->data[i] = b->read_data[i];
        return STATUS_CANCELLED;
    }
    const uint8_t *input = task->data;
    if (!b->waiting) {
        b->messages = 0;
        b->offset = 2;
    } else {
        const uint16_t result = i2c_driver_result(bus);
        if (result == JOB_PENDING) return result;
        if (result != STATUS_OK) {
            transfer_step(task, true);
            return result;
        }
        const bool read = input[b->offset] != 0;
        const uint8_t size = input[b->offset + 1];
        if (read) task->complete += size;
        b->offset += (uint8_t)(2 + (read ? 0 : size));
        ++b->messages;
    }
    if (b->messages == input[1]) {
        for (unsigned i = 0; i < task->complete; ++i) task->data[i] = b->read_data[i];
        b->waiting = false;
        return STATUS_OK;
    }

    const bool read = input[b->offset] != 0;
    uint8_t *buffer = read ? b->read_data + task->complete : task->data + b->offset + 2;
    if (!i2c_driver_start(bus, input[0], read, buffer, input[b->offset + 1],
                          b->messages == 0, b->messages + 1 == input[1])) {
        transfer_step(task, true);
        return STATUS_IO;
    }
    b->waiting = true;
    return JOB_PENDING;
}

void i2c_poll(uint32_t now) { jobs_poll(&queue, transfer_step, now); }

static bool bus_busy(unsigned bus)
{
    return job_lane_active(&queue, (uint8_t)bus);
}

void i2c_stop_all(resources *pool)
{
    jobs_reset(&queue, transfer_step);
    for (unsigned bus = 0; bus < 2; ++bus) {
        if (buses[bus].configured) i2c_driver_deinit(bus);
        buses[bus].configured = buses[bus].waiting = false;
        resource_release(pool, (uint8_t)(3 + bus));
    }
}

static bool valid_transfer(const uint8_t *data, uint16_t length)
{
    if (length < 4 || length > 255 || data[0] < 8 || data[0] > 119 ||
        !data[1] || data[1] > 8) return false;
    unsigned offset = 2, reads = 0;
    for (unsigned i = 0; i < data[1]; ++i) {
        if (offset + 2 > length || data[offset] > 1 || !data[offset + 1]) return false;
        if (data[offset]) reads += data[offset + 1];
        offset += 2 + (data[offset] ? 0 : data[offset + 1]);
    }
    return offset == length && reads <= JOB_DATA;
}

uint16_t i2c_job_command(uint8_t opcode, uint32_t id, uint8_t *out, uint16_t *written)
{
    if (opcode == SYS_CANCEL) return job_cancel(&queue, id, transfer_step);
    return job_collect(&queue, id, out, written);
}

uint16_t i2c_command(resources *pool, uint8_t opcode, const uint8_t *input,
                     uint16_t length, uint8_t *out, uint16_t *written, uint32_t now)
{
    if (!length || input[0] > 1) return STATUS_VALUE;
    const unsigned bus = input[0];
    i2c_bus *b = &buses[bus];
    const uint8_t owner = (uint8_t)(3 + bus);

    switch (opcode) {
    case 1: {
        if (length != 5) return STATUS_VALUE;
        if (b->configured) return STATUS_BUSY;
        const uint32_t speed = wire_u32(input + 1);
        if (speed < 10000 || speed > 400000) return STATUS_VALUE;
        resource_plan plan = {0};
        resource_add(&plan, RESOURCE_PIN('B', bus == 0 ? 8 : 10));
        resource_add(&plan, RESOURCE_PIN('B', bus == 0 ? 9 : 3));
        uint8_t conflict;
        const uint16_t status = resource_reserve(pool, &plan, owner, &conflict);
        if (status != STATUS_OK) return status;
        b->speed = speed;
        b->slave = false;
        b->address = 0;
        b->configured = init_bus(bus);
        if (!b->configured) resource_release(pool, owner);
        return b->configured ? STATUS_OK : STATUS_IO;
    }
    case 2:
        if (length != 3 || input[1] > 1 ||
            (input[1] && (input[2] < 8 || input[2] > 119))) return STATUS_VALUE;
        if (!b->configured || bus_busy(bus)) return STATUS_BUSY;
        i2c_driver_deinit(bus);
        b->slave = input[1] != 0;
        b->address = input[2];
        b->configured = init_bus(bus);
        if (!b->configured) resource_release(pool, owner);
        return b->configured ? STATUS_OK : STATUS_IO;
    case 3:
        if (length < 13 || !valid_transfer(input + 9, length - 9)) return STATUS_VALUE;
        if (!b->configured || b->slave) return STATUS_BUSY;
        return job_submit(&queue, wire_u32(input + 1), (uint8_t)bus,
                          input + 9, length - 9, now, wire_u32(input + 5));
    case 4:
        if (length > I2C_SLAVE_BYTES + 1 || !b->configured || !b->slave)
            return STATUS_VALUE;
        i2c_driver_slave_tx(bus, input + 1, (uint8_t)(length - 1));
        return STATUS_OK;
    case 5: {
        if (length != 1 || !b->configured || !b->slave) return STATUS_VALUE;
        i2c_slave_event event;
        i2c_driver_slave_event(bus, &event);
        wire_put32(out, event.received);
        wire_put32(out + 4, event.errors);
        for (unsigned i = 0; i < event.count; ++i) out[8 + i] = event.bytes[i];
        *written = (uint16_t)(8 + event.count);
        return STATUS_OK;
    }
    case 6:
        if (length != 1 || bus_busy(bus)) return STATUS_BUSY;
        if (b->configured) i2c_driver_deinit(bus);
        b->configured = false;
        resource_release(pool, owner);
        return STATUS_OK;
    default:
        return STATUS_OPCODE;
    }
}
