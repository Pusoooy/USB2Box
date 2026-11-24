#include "jobs.h"
#include "invariant.h"

enum { UNUSED, QUEUED, RUNNING, DONE };

static job *find(jobs *queue, uint32_t id)
{
    for (unsigned i = 0; i < JOB_CAPACITY; ++i)
        if (queue->entries[i].state != UNUSED && queue->entries[i].id == id)
            return &queue->entries[i];
    return 0;
}

uint16_t job_submit(jobs *queue, uint32_t id, uint8_t lane, const uint8_t *data,
                    uint16_t length, uint32_t now, uint32_t timeout)
{
    if (!id || length > JOB_DATA || !timeout || timeout > 60000) return STATUS_VALUE;
    if (find(queue, id)) return STATUS_DUPLICATE;
    for (unsigned i = 0; i < JOB_CAPACITY; ++i) {
        job *task = &queue->entries[i];
        if (task->state != UNUSED) continue;
        task->id = id;
        task->lane = lane;
        task->deadline = now + timeout;
        task->order = queue->next_order++;
        task->length = length;
        task->complete = 0;
        task->state = QUEUED;
        for (unsigned j = 0; j < length; ++j) task->data[j] = data[j];
        return STATUS_OK;
    }
    return STATUS_FULL;
}

static bool lane_blocked(const jobs *queue, const job *candidate)
{
    for (unsigned i = 0; i < JOB_CAPACITY; ++i) {
        const job *other = &queue->entries[i];
        if (other == candidate || other->lane != candidate->lane) continue;
        if (other->state == RUNNING ||
            (other->state == QUEUED && candidate->order - other->order < 0x80000000u))
            return true;
    }
    return false;
}

void jobs_poll(jobs *queue, job_step step, uint32_t now)
{
    for (unsigned n = 0; n < JOB_CAPACITY; ++n) {
        job *task = &queue->entries[(queue->next + n) % JOB_CAPACITY];
        if (task->state != QUEUED && task->state != RUNNING) continue;
        if ((uint32_t)(now - task->deadline) < 0x80000000u) {
            if (task->state == RUNNING) step(task, true);
            task->status = STATUS_TIMEOUT;
        } else {
            if (task->state == QUEUED && lane_blocked(queue, task)) continue;
            task->state = RUNNING;
            task->status = step(task, false);
        }
        if (task->status != JOB_PENDING) task->state = DONE;
    }
    queue->next = (uint8_t)((queue->next + 1u) % JOB_CAPACITY);
}

uint16_t job_cancel(jobs *queue, uint32_t id, job_step step)
{
    job *task = find(queue, id);
    if (!task) return STATUS_VALUE;
    if (task->state == DONE) return STATUS_BUSY;
    if (task->state == RUNNING) step(task, true);
    task->state = DONE;
    task->status = STATUS_CANCELLED;
    return STATUS_OK;
}

uint16_t job_collect(jobs *queue, uint32_t id, uint8_t *out, uint16_t *written)
{
    job *task = find(queue, id);
    if (!task) return STATUS_VALUE;
    if (task->state != DONE) return STATUS_BUSY;
    if (task->complete > JOB_DATA) {
        USB2_ASSERT(task->complete <= JOB_DATA);
        task->state = UNUSED;
        return STATUS_IO;
    }
    wire_put16(out, task->status);
    wire_put16(out + 2, task->complete);
    for (unsigned i = 0; i < task->complete; ++i) out[4 + i] = task->data[i];
    *written = (uint16_t)(4u + task->complete);
    task->state = UNUSED;
    return STATUS_OK;
}

void jobs_reset(jobs *queue, job_step step)
{
    for (unsigned i = 0; i < JOB_CAPACITY; ++i) {
        if (queue->entries[i].state == RUNNING) step(&queue->entries[i], true);
        queue->entries[i].state = UNUSED;
    }
    queue->next = 0;
    queue->next_order = 0;
}

bool job_lane_active(const jobs *queue, uint8_t lane)
{
    for (unsigned i = 0; i < JOB_CAPACITY; ++i) {
        const job *task = &queue->entries[i];
        if (task->lane == lane && (task->state == QUEUED || task->state == RUNNING))
            return true;
    }
    return false;
}
