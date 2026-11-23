#ifndef USB2BOX_JOBS_H
#define USB2BOX_JOBS_H

#include "device.h"

enum { JOB_CAPACITY = 6, JOB_DATA = 256, JOB_PENDING = 0xffff };
typedef struct {
    uint32_t id, deadline, order;
    uint16_t status, length, complete;
    uint8_t lane, state, data[JOB_DATA];
} job;

typedef uint16_t (*job_step)(job *task, bool cancel);
typedef struct {
    job entries[JOB_CAPACITY];
    uint8_t next;
    uint32_t next_order;
} jobs;

uint16_t job_submit(jobs *queue, uint32_t id, uint8_t lane, const uint8_t *data,
                    uint16_t length, uint32_t now, uint32_t timeout);
void jobs_poll(jobs *queue, job_step step, uint32_t now);
uint16_t job_cancel(jobs *queue, uint32_t id, job_step step);
uint16_t job_collect(jobs *queue, uint32_t id, uint8_t *out, uint16_t *written);
bool job_lane_active(const jobs *queue, uint8_t lane);
void jobs_reset(jobs *queue, job_step step);

#endif
