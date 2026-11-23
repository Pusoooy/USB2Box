#include "app.h"
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static uint32_t now_ms(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u);
}

int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    device dev;
    app_init();
    device_init(&dev, app_command, app_stop);
    int next;
    uint32_t previous = now_ms() - 1u;
    while ((next = getchar()) != EOF) {
        const uint32_t now = now_ms();
        if (now != previous) {
            previous = now;
            device_tick(&dev, now);
            app_poll(now);
        }
        const uint8_t byte = (uint8_t)next;
        device_feed(&dev, &byte, 1, now);
        size_t length;
        const uint8_t *response = device_reply(&dev, &length);
        if (length) {
            if (fwrite(response, 1, length, stdout) != length || fflush(stdout) != 0) break;
            device_consume_reply(&dev, length);
        }
    }
    device_reset(&dev);
    return ferror(stdin) || ferror(stdout);
}
