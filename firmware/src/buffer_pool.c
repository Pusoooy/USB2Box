#include "buffer_pool.h"
#include "invariant.h"

_Static_assert(BUFFER_BLOCKS < 64, "buffer lease masks must fit in one word");

static uint16_t blocks[BUFFER_BLOCKS * BUFFER_BLOCK_WORDS];
static uint64_t used;

static uint64_t block_mask(unsigned first, unsigned count)
{
    return ((UINT64_C(1) << count) - 1u) << first;
}

bool buffer_take(buffer_lease *lease, unsigned words)
{
    if (lease->words || !words || words > BUFFER_BLOCKS * BUFFER_BLOCK_WORDS) return false;
    const unsigned count = (words + BUFFER_BLOCK_WORDS - 1u) / BUFFER_BLOCK_WORDS;
    for (unsigned first = 0; first + count <= BUFFER_BLOCKS; ++first) {
        const uint64_t mask = block_mask(first, count);
        if (used & mask) continue;
        used |= mask;
        *lease = (buffer_lease){blocks + first * BUFFER_BLOCK_WORDS, (uint8_t)first, (uint8_t)count};
        return true;
    }
    return false;
}

void buffer_release(buffer_lease *lease)
{
    if (!lease->words) return;
    const bool valid = lease->count && (unsigned)lease->first + lease->count <= BUFFER_BLOCKS;
    USB2_ASSERT(valid);
    if (valid) used &= ~block_mask(lease->first, lease->count);
    *lease = (buffer_lease){0};
}
