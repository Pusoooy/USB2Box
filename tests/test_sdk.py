import asyncio
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
import unittest

from usb2xxx import Busy, I2CMessage, LeaseExpired, RemoteError, USB2XXX


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
            with self.assertRaises(RemoteError) as error:
                dev.i2c_transfer(0, 41, [I2CMessage(read=2)])
            self.assertEqual(error.exception.status, 0x4001)

            for group in range(2):
                dev.pwm_configure(group, 1000 + group * 325, 250000)
                self.assertTrue(dev.pwm_start(group, 1000)["running"])
            with self.assertRaises(Busy) as conflict:
                dev.adc_configure([(0, 7)])
            self.assertEqual(conflict.exception.detail["resource"], 0)
            self.assertEqual(dev.spi_transfer([pattern], bits=16, mode=3, cs=1), [pattern])
            self.assertEqual(dev.adc_read_once(11), 341)
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
        asyncio.run(run())
        result = subprocess.check_output([sys.executable, "-m", "usb2xxx.cli", "--mock", "adc", "10"])
        self.assertEqual(json.loads(result), {"simulated": True, "result": {"sample": 310}})


if __name__ == "__main__":
    unittest.main()
