#include <stddef.h>

/* ARM GCC emits this for aggregate initialization in a freestanding build. */
void *memset(void *destination, int value, size_t length)
{
    /* Volatile stores prevent this loop from being lowered to a call to itself. */
    volatile unsigned char *bytes = destination;
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = (unsigned char)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length)
{
    volatile unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
    return destination;
}

int memcmp(const void *first, const void *second, size_t length)
{
    const unsigned char *a = first;
    const unsigned char *b = second;
    for (size_t i = 0; i < length; ++i) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}
