"""USB2Box real-device checks with explicit fixture selection."""
from __future__ import annotations

from argparse import ArgumentParser
from datetime import datetime, timezone
from hashlib import sha256
import json
from pathlib import Path
import random
from time import monotonic, sleep

from usb2xxx import Busy, I2CMessage, USB2XXX, list_devices


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


def pwm_loop(dev, group):
    expected = 1000 + 300 * group
    dev.pwm_configure(group, expected, 250000)
    dev.pwm_capture_start()
    try:
        dev.pwm_start(group, 250)

        def measure():
            try:
                return dev.pwm_capture_read()
            except Busy:
                return None

        actual = wait_until(measure, bool, 0.2)
        if abs(actual["frequency_hz"] - expected) > expected * 0.02:
            raise ValueError(f"PWM fixture frequency mismatch: {actual}")
        return actual
    finally:
        dev.pwm_stop(group)
        dev.pwm_capture_stop()


def adc_sample(dev, channel):
    values = [dev.adc_read_once(channel) for _ in range(16)]
    return {"channel": channel, "min": min(values), "max": max(values),
            "samples": len(values)}


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--vid", type=lambda item: int(item, 0))
    parser.add_argument("--pid", type=lambda item: int(item, 0))
    parser.add_argument("--serial")
    parser.add_argument("--fixture", choices=("usb", "uart", "i2c", "pwm", "spi", "adc"),
                        default="usb")
    parser.add_argument("--board-rev")
    parser.add_argument("--wiring")
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--channel", default=10, type=int)
    parser.add_argument("--group", choices=(0, 1), default=0, type=int)
    args = parser.parse_args()
    if not args.vid or not args.pid:
        print("NOT_RUN: assigned USB2Box VID/PID and a physical board are required.")
        return 2
    if args.fixture != "usb" and (not args.board_rev or not args.wiring or not args.firmware or not args.log):
        parser.error("physical fixture checks require board revision, wiring, firmware image and log path")
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
                "pwm": lambda: pwm_loop(dev, args.group),
                "adc": lambda: adc_sample(dev, args.channel),
            }[args.fixture]()
        report["verdict"] = "MEASURED" if args.fixture == "adc" else "PASS"
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
