#ifndef USB2BOX_DEVICE_H
#define USB2BOX_DEVICE_H

#include "frame.h"

enum {
    DEVICE_FRAGMENT = 512, DEVICE_MESSAGE = 1024,
    DEVICE_LEASE_MS = 2000, DEVICE_FRAGMENT_MS = 500,
    FLAG_FIRST = 1, FLAG_LAST = 2, FLAG_ERROR = 8,
    STATUS_OK = 0, STATUS_FRAME = 1, STATUS_OPCODE = 2, STATUS_DUPLICATE = 3,
    STATUS_VALUE = 0x1001, STATUS_BUSY = 0x2001, STATUS_TIMEOUT = 0x3001,
    STATUS_IO = 0x4001, STATUS_FULL = 0x5001, STATUS_CANCELLED = 0x6001,
    STATUS_SESSION = 0x7001,
    SERVICE_SYS = 0, SERVICE_I2C = 1, SERVICE_UART = 2, SERVICE_PWM = 3,
    SERVICE_SPI = 4, SERVICE_ADC = 5, SERVICE_DIAG = 6,
    SYS_HELLO = 1, SYS_CAPABILITIES, SYS_VERSION, SYS_DEVICE_INFO,
    SYS_OPEN, SYS_HEARTBEAT, SYS_CLOSE, SYS_RESOURCES, SYS_CANCEL, SYS_STOP, SYS_COLLECT,
    SYS_FEATURES,
    FEATURE_BULK_STREAMS = 1u << 0,
    FEATURE_I2C_EVENTS = 1u << 1,
    FEATURE_PWM_UPDATE = 1u << 2,
    FEATURE_ADC_TIMING = 1u << 3,
    FEATURE_I2C_RECOVERY = 1u << 4,
    FEATURE_DIAGNOSTIC_LOG = 1u << 5,
    DEVICE_LOG_CAPACITY = 16,
};

typedef uint16_t (*device_command)(uint8_t service, uint8_t opcode,
                                  const uint8_t *input, uint16_t length,
                                  uint8_t *output, uint16_t *written, uint32_t now);
typedef void (*device_stop)(void);

typedef struct {
    uint32_t tick, request;
    uint16_t status;
    uint8_t level, kind, service, opcode;
} device_log_entry;

typedef struct {
    device_command command;
    device_stop stop;
    uint32_t lease_id, lease_at, partial_at, last_request;
    uint32_t requests, invalid_headers, errors, lease_expired, resets;
    uint32_t current_ms, overwritten;
    device_log_entry log[DEVICE_LOG_CAPACITY];
    uint8_t log_next, log_count;
    bool active, assembling;
    frame_header fragment, message;
    uint16_t header_used, payload_used, assembled, tx_size, tx_sent;
    uint8_t header[FRAME_HEADER_SIZE], payload[DEVICE_FRAGMENT], input[DEVICE_MESSAGE];
    uint8_t tx[FRAME_HEADER_SIZE + DEVICE_FRAGMENT];
} device;

void device_init(device *dev, device_command command, device_stop stop);
void device_reset(device *dev);
void device_suspend(device *dev);
void device_tick(device *dev, uint32_t now);
size_t device_feed(device *dev, const uint8_t *bytes, size_t length, uint32_t now);
const uint8_t *device_reply(const device *dev, size_t *length);
void device_consume_reply(device *dev, size_t length);

uint16_t wire_u16(const uint8_t *p);
uint32_t wire_u32(const uint8_t *p);
void wire_put16(uint8_t *p, uint16_t value);
void wire_put32(uint8_t *p, uint32_t value);
void wire_put64(uint8_t *p, uint64_t value);

#endif
