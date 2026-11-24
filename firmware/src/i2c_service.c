#include "i2c_service.h"

typedef struct {
    resources *pool;
    uint32_t speed;
    uint8_t address, messages, offset;
    bool configured, slave, waiting;
    uint8_t read_data[JOB_DATA];
    uint32_t recovery_at;
    uint8_t recovery_phase, recovery_pulses;
} i2c_bus;

static i2c_bus buses[2];
static jobs queue;
static uint32_t poll_time;

enum {
    RECOVERY_IDLE, RECOVERY_WAIT_BUS, RECOVERY_CLOCK_LOW, RECOVERY_CLOCK_HIGH,
    RECOVERY_STOP_PREPARE, RECOVERY_STOP_LOW, RECOVERY_STOP_HIGH, RECOVERY_SETTLE,
};

typedef struct {
    uint32_t sequence, now;
    uint8_t kind, count, data[I2C_SLAVE_BYTES];
} slave_event;

static struct {
    slave_event events[I2C_EVENT_CAPACITY];
    uint32_t head, tail, sequence, received, errors, dropped;
} events[2];

void i2c_record_slave_event(unsigned bus, uint8_t kind, const uint8_t *data,
                             uint8_t count, uint32_t now)
{
    if (count > I2C_SLAVE_BYTES) return;
    const uint32_t sequence = events[bus].sequence++;
    if (kind == I2C_EVENT_RX) __atomic_add_fetch(&events[bus].received, count, __ATOMIC_RELAXED);
    if (kind == I2C_EVENT_ERROR) __atomic_add_fetch(&events[bus].errors, 1, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&events[bus].head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&events[bus].tail, __ATOMIC_ACQUIRE);
    if (head - tail == I2C_EVENT_CAPACITY) {
        __atomic_add_fetch(&events[bus].dropped, 1, __ATOMIC_RELAXED);
        return;
    }
    slave_event *event = &events[bus].events[head % I2C_EVENT_CAPACITY];
    event->sequence = sequence;
    event->now = now;
    event->kind = kind;
    event->count = count;
    if (kind == I2C_EVENT_RX)
        for (unsigned i = 0; i < count; ++i) event->data[i] = data[i];
    __atomic_store_n(&events[bus].head, head + 1, __ATOMIC_RELEASE);
}

static bool take_event(unsigned bus, slave_event *event)
{
    const uint32_t tail = __atomic_load_n(&events[bus].tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&events[bus].head, __ATOMIC_ACQUIRE)) return false;
    *event = events[bus].events[tail % I2C_EVENT_CAPACITY];
    __atomic_store_n(&events[bus].tail, tail + 1, __ATOMIC_RELEASE);
    return true;
}

static void reset_events(unsigned bus)
{
    /* The peripheral is disabled before reset and enabled after it. */
    events[bus].head = events[bus].tail = events[bus].sequence = 0;
    events[bus].received = events[bus].errors = events[bus].dropped = 0;
}

static uint16_t read_events(unsigned bus, bool legacy, uint8_t *out)
{
    slave_event event = {0};
    for (unsigned i = 0; i < I2C_EVENT_CAPACITY; ++i) {
        if (!take_event(bus, &event)) break;
        if (!legacy || event.kind == I2C_EVENT_RX) break;
    }
    const uint32_t received = __atomic_load_n(&events[bus].received, __ATOMIC_RELAXED);
    const uint32_t errors = __atomic_load_n(&events[bus].errors, __ATOMIC_RELAXED);
    uint16_t length;
    if (legacy) {
        wire_put32(out, received);
        wire_put32(out + 4, errors);
        length = 8;
    } else {
        wire_put32(out, event.sequence);
        wire_put32(out + 4, event.now);
        wire_put32(out + 8, __atomic_load_n(&events[bus].dropped, __ATOMIC_RELAXED));
        wire_put32(out + 12, received);
        wire_put32(out + 16, errors);
        out[20] = event.kind;
        out[21] = event.count;
        length = 22;
    }
    if (event.kind == I2C_EVENT_RX) {
        for (unsigned i = 0; i < event.count; ++i) out[length++] = event.data[i];
    }
    return length;
}

static bool init_bus(unsigned bus)
{
    i2c_bus *b = &buses[bus];
    if (i2c_driver_init(bus, b->speed, b->slave, b->address)) return true;
    b->configured = false;
    resource_release(b->pool, (uint8_t)(3 + bus));
    return false;
}

static void recovery_next(i2c_bus *bus, uint8_t phase)
{
    bus->recovery_phase = phase;
    bus->recovery_at = poll_time + 1u;
}

static uint16_t finish_recovery(job *task, uint16_t status)
{
    const unsigned bus = task->lane;
    i2c_bus *b = &buses[bus];
    i2c_driver_recovery_drive(bus, true, true);
    task->data[0] = b->recovery_pulses;
    task->data[1] = i2c_driver_lines(bus);
    task->complete = 2;
    b->recovery_phase = RECOVERY_IDLE;
    if (!init_bus(bus)) return STATUS_IO;
    return status;
}

static uint16_t recovery_step(job *task, bool cancel)
{
    const unsigned bus = task->lane;
    i2c_bus *b = &buses[bus];
    if (cancel) return finish_recovery(task, STATUS_CANCELLED);
    if (b->recovery_phase == RECOVERY_IDLE) {
        b->recovery_pulses = 0;
        i2c_driver_recovery_begin(bus);
        recovery_next(b, RECOVERY_WAIT_BUS);
        return JOB_PENDING;
    }
    if (poll_time - b->recovery_at >= 0x80000000u) return JOB_PENDING;

    const uint8_t levels = i2c_driver_lines(bus);
    switch (b->recovery_phase) {
    case RECOVERY_WAIT_BUS:
        if (!(levels & I2C_LINE_SCL)) break;
        if (levels & I2C_LINE_SDA) return finish_recovery(task, STATUS_OK);
        i2c_driver_recovery_drive(bus, false, true);
        recovery_next(b, RECOVERY_CLOCK_LOW);
        break;
    case RECOVERY_CLOCK_LOW:
        i2c_driver_recovery_drive(bus, true, true);
        recovery_next(b, RECOVERY_CLOCK_HIGH);
        break;
    case RECOVERY_CLOCK_HIGH:
        if (!(levels & I2C_LINE_SCL)) break; /* Clock stretching. */
        ++b->recovery_pulses;
        if (levels & I2C_LINE_SDA) {
            i2c_driver_recovery_drive(bus, false, true);
            recovery_next(b, RECOVERY_STOP_PREPARE);
        } else if (b->recovery_pulses == 9) {
            return finish_recovery(task, STATUS_IO);
        } else {
            i2c_driver_recovery_drive(bus, false, true);
            recovery_next(b, RECOVERY_CLOCK_LOW);
        }
        break;
    case RECOVERY_STOP_PREPARE:
        i2c_driver_recovery_drive(bus, false, false);
        recovery_next(b, RECOVERY_STOP_LOW);
        break;
    case RECOVERY_STOP_LOW:
        i2c_driver_recovery_drive(bus, true, false);
        recovery_next(b, RECOVERY_STOP_HIGH);
        break;
    case RECOVERY_STOP_HIGH:
        if (!(levels & I2C_LINE_SCL)) break;
        i2c_driver_recovery_drive(bus, true, true);
        recovery_next(b, RECOVERY_SETTLE);
        break;
    case RECOVERY_SETTLE:
        return finish_recovery(task, levels == (I2C_LINE_SCL | I2C_LINE_SDA) ? STATUS_OK : STATUS_IO);
    default:
        return finish_recovery(task, STATUS_IO);
    }
    return JOB_PENDING;
}

static uint16_t transfer_step(job *task, bool cancel)
{
    if (!task->length) return recovery_step(task, cancel);
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

void i2c_poll(uint32_t now)
{
    poll_time = now;
    for (unsigned bus = 0; bus < 2; ++bus)
        if (buses[bus].configured && buses[bus].slave) i2c_driver_service(bus);
    jobs_poll(&queue, transfer_step, now);
}

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
        reset_events(bus);
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
        b->pool = pool;
        b->slave = false;
        b->address = 0;
        reset_events(bus);
        b->configured = init_bus(bus);
        return b->configured ? STATUS_OK : STATUS_IO;
    }
    case 2:
        if (length != 3 || input[1] > 1 ||
            (input[1] && (input[2] < 8 || input[2] > 119))) return STATUS_VALUE;
        if (!b->configured || bus_busy(bus)) return STATUS_BUSY;
        i2c_driver_deinit(bus);
        reset_events(bus);
        b->slave = input[1] != 0;
        b->address = input[2];
        b->configured = init_bus(bus);
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
    case 5:
    case 7:
        if (length != 1 || !b->configured || !b->slave) return STATUS_VALUE;
        *written = read_events(bus, opcode == 5, out);
        return STATUS_OK;
    case 6:
        if (length != 1 || bus_busy(bus)) return STATUS_BUSY;
        if (b->configured) i2c_driver_deinit(bus);
        b->configured = false;
        reset_events(bus);
        resource_release(pool, owner);
        return STATUS_OK;
    case 8:
        if (length != 9) return STATUS_VALUE;
        if (!b->configured || b->slave || bus_busy(bus)) return STATUS_BUSY;
        return job_submit(&queue, wire_u32(input + 1), (uint8_t)bus,
                          0, 0, now, wire_u32(input + 5));
    default:
        return STATUS_OPCODE;
    }
}
