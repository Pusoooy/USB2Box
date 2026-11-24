#ifndef USB2BOX_BUFFER_POOL_H
#define USB2BOX_BUFFER_POOL_H

#include <stdbool.h>
#include <stdint.h>

enum { BUFFER_BLOCK_WORDS = 128, BUFFER_BLOCKS = 33 };

typedef struct {
    uint16_t *words;
    uint8_t first, count;
} buffer_lease;

/* Lease and release from the main loop, after the owning driver is idle. */
bool buffer_take(buffer_lease *lease, unsigned words);
void buffer_release(buffer_lease *lease);

#endif
