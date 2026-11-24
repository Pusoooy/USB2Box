"""USB2Box real-device checks with explicit fixture selection."""
from __future__ import annotations

from argparse import ArgumentParser
from datetime import datetime, timezone
from hashlib import sha256
import json
from pathlib import Path
import random
from time import monotonic, sleep

from usb2xxx import Busy, Deadline, I2CMessage, USB2XXX, list_devices


def wait_until(action, done, seconds=1.0):
    deadline = monotonic() + seconds
    while True:
        result = action()
        if done(result):
            return result
        if monotonic() >= deadline:
            raise TimeoutError("fixture response deadline")
        sleep(0.005)


def uart_loop(dev):
    rng = random.Random(17)
    total = 0
    for baud in (9600, 115200, 921600):
        for port in range(2):
            dev.uart_configure(port, baud)
        try:
            expected = [rng.randbytes(512), rng.randbytes(512)]
            for port, block in enumerate(expected):
                dev.uart_write(port, block)
            received = [bytearray(), bytearray()]

            def collect():
                for port in range(2):
                    received[port].extend(dev.uart_read(port))
                return received

            wait_until(collect, lambda blocks: all(len(value) >= 512 for value in blocks), 2)
            if received != expected[::-1]:
                raise ValueError(f"UART fixture payload mismatch at {baud} baud")
            total += 2 * 512
        finally:
            for port in range(2):
                dev.uart_stop(port)
    return {"bidirectional_bytes": total, "bauds": [9600, 115200, 921600]}


def spi_loop(dev):
    sizes = []
    for mode in range(4):
        for bits in (8, 16):
            data = [bytes(range(256)), b"\xa5\x5a" * 173]
            if dev.spi_transfer(data, mode=mode, bits=bits) != data:
                raise ValueError(f"SPI loopback mismatch in mode {mode}/{bits} bits")
            sizes.append(sum(map(len, data)))
    return {"modes": 4, "word_widths": [8, 16], "loopback_bytes": sum(sizes)}


def i2c_loop(dev):
    for bus in range(2):
        dev.i2c_configure(bus, 10000)
    try:
        for master in range(2):
            slave = 1 - master
            dev.i2c_role(master)
            dev.i2c_role(slave, True, 42)
            dev.i2c_slave_buffer(slave, b"data")
            rx = dev.i2c_transfer(master, 42, [I2CMessage(data=b"\xaa"), I2CMessage(read=4)], 200)
            if rx != b"data":
                raise ValueError("I²C fixture mismatch at 10 kHz; check rise times and weak pull-ups")
    finally:
        for bus in range(2):
            dev.i2c_release(bus)
    return {"speed_hz": 10000, "directions": 2}


def pwm_loop(dev, group, channel):
    expected = 1000 + 300 * group
    dev.pwm_configure(group, expected, 250000, channel=channel)
    dev.pwm_capture_start()
    try:
        dev.pwm_start(group, 750, channel=channel)

        def measure():
            try:
                return dev.pwm_capture_read()
            except Busy:
                return None

        first = wait_until(measure, bool, 0.2)
        if (abs(first["frequency_hz"] - expected) > expected * 0.02 or
                abs(first["duty_ppm"] - 250000) > 20000):
            raise ValueError(f"PWM fixture frequency or duty mismatch: {first}")

        frequency = expected + 400
        dev.pwm_update(group, frequency, 600000, channel=channel)
        updated = wait_until(measure, lambda reading: reading and
                             abs(reading["frequency_hz"] - frequency) <= frequency * 0.02 and
                             abs(reading["duty_ppm"] - 600000) <= 20000, 0.3)
        return {"group": group, "channel": channel, "initial": first, "updated": updated}
    finally:
        dev.pwm_stop(group, channel=channel)
        dev.pwm_capture_stop()


def adc_sample(dev, channel):
    values = [dev.adc_read_once(channel) for _ in range(16)]
    return {"channel": channel, "min": min(values), "max": max(values),
            "samples": len(values)}


def adc_stream(dev, channel, seconds):
    target = 100000
    dev.adc_configure([(channel, 2)], resolution=0)
    samples = blocks = expected_offset = discontinuities = dropped = timeouts = 0
    rate = resolution = None
    command_ms = []
    stopped_early = False
    try:
        dev.adc_start(target)
        start = monotonic()
        next_probe = start + 0.25
        while monotonic() - start < seconds:
            try:
                block = dev.adc_next_stream(timeout=0.3)
            except Deadline:
                timeouts += 1
                continue
            dropped = block.dropped
            if block.samples:
                discontinuities += block.offset != expected_offset
                expected_offset = block.offset + len(block.samples)
                samples += len(block.samples)
                blocks += 1
                rate, resolution = block.rate, block.resolution
            if not block.running and not block.more_available:
                stopped_early = True
                break
            now = monotonic()
            if now >= next_probe:
                dev.resources()
                command_ms.append((monotonic() - now) * 1000)
                next_probe = now + 0.25
        elapsed = monotonic() - start
    finally:
        dev.adc_stop()
    return {"channel": channel, "target_sps": target, "reported_sps": rate,
            "resolution_bits": resolution, "duration_s": elapsed, "samples": samples,
            "blocks": blocks, "host_received_sps": samples / elapsed,
            "host_sample_payload_bytes_per_s": 2 * samples / elapsed,
            "discontinuous_blocks": discontinuities, "device_dropped_samples": dropped,
            "read_timeouts": timeouts, "stopped_early": stopped_early,
            "control_probes": len(command_ms), "max_control_round_trip_ms": max(command_ms, default=None)}


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--vid", type=lambda item: int(item, 0))
    parser.add_argument("--pid", type=lambda item: int(item, 0))
    parser.add_argument("--serial")
    parser.add_argument("--fixture", choices=("usb", "uart", "i2c", "pwm", "spi", "adc", "adc-stream"),
                        default="usb")
    parser.add_argument("--board-rev")
    parser.add_argument("--wiring")
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--channel", default=10, type=int)
    parser.add_argument("--group", choices=(0, 1), default=0, type=int)
    parser.add_argument("--pwm-channel", choices=(1, 2), default=1, type=int)
    parser.add_argument("--seconds", default=10, type=int, help="ADC stream measurement window, 1–60 seconds")
    args = parser.parse_args()
    if not args.vid or not args.pid:
        print("NOT_RUN: assigned USB2Box VID/PID and a physical board are required.")
        return 2
    if args.fixture != "usb" and (not args.board_rev or not args.wiring or not args.firmware or not args.log):
        parser.error("physical fixture checks require board revision, wiring, firmware image and log path")
    if args.fixture == "adc-stream" and not 1 <= args.seconds <= 60:
        parser.error("ADC stream measurement window must be 1–60 seconds")
    report = {"fixture": args.fixture, "board_revision": args.board_rev, "wiring": args.wiring,
              "timestamp_utc": datetime.now(timezone.utc).isoformat(), "simulated": False,
              "firmware_image_sha256": sha256(args.firmware.read_bytes()).hexdigest() if args.firmware else None,
              "verdict": "NOT_RUN"}
    try:
        devices = list_devices(mock=False, vid=args.vid, pid=args.pid)
        if not devices:
            report["detail"] = "No matching physical USB device"
            return 2
        with USB2XXX.open(vid=args.vid, pid=args.pid, serial=args.serial) as dev:
            report["metrics"] = {
                "usb": lambda: dev.capabilities(), "uart": lambda: uart_loop(dev),
                "spi": lambda: spi_loop(dev), "i2c": lambda: i2c_loop(dev),
                "pwm": lambda: pwm_loop(dev, args.group, args.pwm_channel),
                "adc": lambda: adc_sample(dev, args.channel),
                "adc-stream": lambda: adc_stream(dev, args.channel, args.seconds),
            }[args.fixture]()
        if args.fixture == "adc-stream":
            metrics = report["metrics"]
            if (not metrics["samples"] or metrics["discontinuous_blocks"] or
                    metrics["device_dropped_samples"] or metrics["read_timeouts"] or metrics["stopped_early"]):
                report["detail"] = "ADC stream ended early or was incomplete"
                report["verdict"] = "FAILED"
                return 1
        report["verdict"] = "MEASURED" if args.fixture in ("adc", "adc-stream") else "PASS"
        return 0
    except Exception as error:
        report["detail"] = str(error)
        report["verdict"] = "FAILED"
        return 1
    finally:
        raw = json.dumps(report, ensure_ascii=False, indent=2)
        if args.log:
            args.log.write_text(raw + "\n", encoding="utf-8")
        print(raw)


if __name__ == "__main__":
    raise SystemExit(main())
