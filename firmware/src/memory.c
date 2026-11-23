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
