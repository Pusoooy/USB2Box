#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "app.h"
#include "mock_clock.h"
#include "stream.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#endif

static uint32_t now_ms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u);
#endif
}

/* Return -1 on EOF, -2 on error, or zero when the next device tick is due. */
static int read_input(uint8_t *buffer, unsigned capacity)
{
#ifdef _WIN32
    DWORD available;
    if (!PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE), NULL, 0, NULL, &available, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) return -1;
        /* A redirected regular file (legacy framing) is readable without a peek. */
        available = capacity;
    }
    if (!available) {
        Sleep(1);
        return 0;
    }
    const int count = _read(_fileno(stdin), buffer, available < capacity ? available : capacity);
#else
    struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
    const int ready = poll(&input, 1, 1);
    if (!ready || (ready < 0 && errno == EINTR)) return 0;
    if (ready < 0) return -2;
    const int count = (int)read(STDIN_FILENO, buffer, capacity);
#endif
    return count ? (count > 0 ? count : -2) : -1;
}

static bool send_record(bool bulk, uint8_t endpoint, const uint8_t *bytes, uint16_t length)
{
    if (bulk) {
        uint8_t prefix[3] = {endpoint};
        wire_put16(prefix + 1, length);
        if (fwrite(prefix, 1, sizeof prefix, stdout) != sizeof prefix) return false;
    }
    return fwrite(bytes, 1, length, stdout) == length && fflush(stdout) == 0;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const bool bulk = argc == 2 && strcmp(argv[1], "--bulk") == 0;
    device dev;
    stream_mux mux = {0};
    app_init();
    device_init(&dev, app_command, app_stop);
    uint8_t input[64];
    int result = 0;
    uint32_t previous = now_ms() - 1u;
    for (;;) {
        const int received = read_input(input, sizeof input);
        if (received < 0) {
            result = received == -2;
            break;
        }
        const uint32_t now = now_ms();
        if (now != previous) {
            previous = now;
            device_tick(&dev, now);
            pwm_mock_tick(now);
            app_poll(now);
        }
        size_t consumed = 0;
        while (consumed < (size_t)received) {
            consumed += device_feed(&dev, input + consumed, (size_t)received - consumed, now);
            size_t length;
            const uint8_t *reply = device_reply(&dev, &length);
            if (!length) continue;
            if (!send_record(bulk, 0x81, reply, (uint16_t)length)) {
                result = 1;
                goto finish;
            }
            device_consume_reply(&dev, length);
        }
        if (bulk) {
            uint16_t length;
            const uint8_t *frame = stream_peek(&mux, dev.active, now, &length);
            if (!length) continue;
            if (!send_record(true, 0x82, frame, length)) {
                result = 1;
                break;
            }
            stream_consume(&mux, length);
        }
    }
finish:
    device_reset(&dev);
    return result;
}
