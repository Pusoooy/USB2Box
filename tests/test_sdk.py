import asyncio
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
import unittest

from usb2xxx import Busy, Deadline, I2CMessage, LeaseExpired, RemoteError, USB2XXX
from usb2xxx.client import FEATURE_PWM_CHANNELS
from usb2xxx.protocol import HEADER, FrameReader
from usb2xxx.transport import acquire_device_lock


class SDKTests(unittest.TestCase):
    def test_shared_device_and_lease_safety(self):
        with USB2XXX.open(mock=True) as dev:
            self.assertEqual(dev.capabilities()["message"], 1024)
            self.assertTrue(dev.simulated)
            for port in range(2):
                dev.uart_configure(port)
            pattern = bytes(range(256)) * 3
            self.assertEqual(dev.uart_write(0, pattern), len(pattern))
            self.assertEqual(dev.uart_write(1, b"peer"), 4)
            results = [bytearray(), bytearray()]
            end = time.monotonic() + 0.5
            while time.monotonic() < end and len(results[1]) < len(pattern):
                for port in range(2):
                    results[port].extend(dev.uart_read(port))
                time.sleep(0.002)
            self.assertEqual(results, [b"peer", pattern])
            for bus in range(2):
                dev.i2c_configure(bus)
            dev.i2c_role(1, True, 42)
            dev.i2c_slave_buffer(1, b"bytes")
            data = dev.i2c_transfer(0, 42, [I2CMessage(data=b"offset"), I2CMessage(read=5)])
            self.assertEqual(data, b"bytes")
            self.assertEqual(dev.i2c_slave_event(1), (6, 0, b"offset"))
            event = dev.i2c_next_slave_event(1)
            self.assertEqual((event.kind, event.count, event.sequence, event.dropped), ("tx", 5, 1, 0))
            with self.assertRaises(RemoteError) as error:
                dev.i2c_transfer(0, 41, [I2CMessage(read=2)])
            self.assertEqual(error.exception.status, 0x4001)
            self.assertEqual(dev.i2c_recover(0), {"pulses": 0, "scl_high": True, "sda_high": True})

            for group in range(2):
                dev.pwm_configure(group, 1000 + group * 325, 250000)
                self.assertTrue(dev.pwm_start(group, 1000)["running"])
            self.assertTrue(dev.features() & FEATURE_PWM_CHANNELS)
            state = dev.pwm_configure(0, 1000, 750000, channel=2)
            self.assertEqual((state["timer"], state["channel"], state["duty_ppm"]),
                             ("TIM5", 2, 750000))
            self.assertAlmostEqual(state["duty_step_ppm"], 1_000_000 / 84000)
            self.assertTrue(dev.pwm_start(0, 1000, channel=2)["running"])
            with self.assertRaises(Busy) as conflict:
                dev.adc_configure([(1, 7)])
            self.assertEqual(conflict.exception.detail["resource"], 1)
            dev.pwm_stop(0, channel=2)
            self.assertTrue(dev.pwm_status(0)["running"])
            with self.assertRaises(Busy) as conflict:
                dev.adc_configure([(0, 7)])
            self.assertEqual(conflict.exception.detail["resource"], 0)
            self.assertEqual(dev.spi_transfer([pattern], bits=16, mode=3, cs=1), [pattern])
            self.assertEqual(dev.adc_read_once(11), 341)
            self.assertEqual(dev.adc_estimate_reference(), 3.3)
            dev.adc_configure([(10, 2), (11, 2)])
            dev.adc_start(50000, 256)
            with self.assertRaises(Busy) as conflict:
                dev.spi_transfer([b"collision"], cs=1)
            deadline = time.monotonic() + 0.3
            samples = []
            while time.monotonic() < deadline:
                block = dev.adc_read(128)
                samples.extend(block.samples)
                if not block.running and not block.more_available:
                    break
                time.sleep(0.004)
            self.assertEqual(len(samples), 256)
            self.assertTrue(dev.diagnostic_log())
            with TemporaryDirectory() as directory:
                filename = Path(directory) / "capture.csv"
                block.save(filename)
                self.assertTrue(filename.read_text().startswith("sample_index,raw"))
            dev.call(0, 10)
            self.assertFalse(any(owner not in (0, 255) for owner in dev.resources()))
            with self.assertRaises(LeaseExpired):
                dev.pwm_status(0)

    def test_async_stream_and_cli(self):
        async def run():
            async with await USB2XXX.open_async(mock=True) as dev:
                self.assertEqual((await dev.capabilities())["version"], (1, 0))
                await dev.run(USB2XXX.adc_configure, [(10, 2)])
                await dev.call(5, 3, (100000).to_bytes(4, "little") + bytes([8, 0, 0, 0, 0, 0]))
                blocks = [block async for block in dev.capture_stream()]
                self.assertEqual(sum(len(block.samples) for block in blocks), 8)
                self.assertEqual(blocks[0].scan_channels, (10,))
                self.assertEqual(blocks[0].scan_period_ns, 10000)
                self.assertEqual(blocks[0].sample_position(1)[1] - blocks[0].sample_position(0)[1], 10)
        asyncio.run(run())
        result = subprocess.check_output([sys.executable, "-m", "usb2xxx.cli", "--mock", "adc", "10"])
        self.assertEqual(json.loads(result), {"simulated": True, "result": {"sample": 310}})

    def test_three_concurrent_streams_and_timeout_credit(self):
        with USB2XXX.open(mock=True) as dev:
            for port in range(2):
                dev.uart_configure(port)

            with dev.uart_subscribe(0) as first, dev.uart_subscribe(1) as second:
                for _ in range(3):
                    with self.assertRaises(Deadline):
                        first.read(0.02)
                with self.assertRaises(Busy):
                    dev.uart_read(0)
                expected = [b"peer across the other port", bytes(range(250)) * 3]

                def collect_uart(subscription, count):
                    result = bytearray()
                    while len(result) < count:
                        block = subscription.read()
                        self.assertEqual((block.port, block.offset), (subscription.port, len(result)))
                        self.assertEqual((block.dropped, block.errors), (0, 0))
                        result.extend(block.data)
                    return bytes(result)

                def collect_adc():
                    result = []
                    while True:
                        block = dev.adc_next_stream()
                        self.assertEqual(block.offset, len(result))
                        result.extend(block.samples)
                        if not block.running and not block.more_available:
                            return result

                dev.adc_configure([(10, 2)])
                dev.adc_start(50000, 513)
                with ThreadPoolExecutor(max_workers=3) as executor:
                    rx0 = executor.submit(collect_uart, first, len(expected[0]))
                    rx1 = executor.submit(collect_uart, second, len(expected[1]))
                    adc = executor.submit(collect_adc)
                    self.assertEqual(dev.uart_write(0, expected[1]), len(expected[1]))
                    self.assertEqual(dev.uart_write(1, expected[0]), len(expected[0]))
                    self.assertEqual(rx0.result(timeout=2), expected[0])
                    self.assertEqual(rx1.result(timeout=2), expected[1])
                    self.assertEqual(len(adc.result(timeout=2)), 513)
                with self.assertRaises(Deadline):
                    first.read(0.02)

            with dev.uart_subscribe(0) as resumed:
                dev.uart_write(1, b"new")
                block = resumed.read()
                self.assertEqual((block.offset, block.data), (len(expected[0]), b"new"))

    def test_frame_reader_retains_header_after_payload_timeout(self):
        header = HEADER.pack(b"U2SX", 1, 0, HEADER.size, 4, 3, 2, 8,
                             0, 0, 0, 4, 4, 0, 2, 65535)
        reads = iter([header, Deadline("delayed USB payload"), b"data"])

        class Transport:
            def read_exact(self, endpoint, count, timeout):
                result = next(reads)
                if isinstance(result, Exception):
                    raise result
                self_count = len(result)
                if self_count != count:
                    raise AssertionError(f"read {self_count}, expected {count}")
                return result

        reader = FrameReader(Transport(), 0x82)
        with self.assertRaises(Deadline):
            reader.read(0.1)
        frame = reader.read(0.1)
        self.assertEqual((frame.stream, frame.sequence, frame.payload), (2, 65535, b"data"))

    def test_cross_process_device_lock_released_on_close(self):
        child = """
import sys
from pathlib import Path
from usb2xxx import Busy
from usb2xxx.transport import acquire_device_lock
try:
    with acquire_device_lock(Path(sys.argv[1])):
        sys.exit(0)
except Busy:
    sys.exit(2)
"""
        with TemporaryDirectory() as scratch:
            path = Path(scratch) / "device.lock"
            argv = [sys.executable, "-c", child, str(path)]
            with acquire_device_lock(path):
                self.assertEqual(subprocess.run(argv, check=False, timeout=5).returncode, 2)
            self.assertEqual(subprocess.run(argv, check=False, timeout=5).returncode, 0)


if __name__ == "__main__":
    unittest.main()
