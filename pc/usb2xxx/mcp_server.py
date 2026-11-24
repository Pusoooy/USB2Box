"""Local stdio MCP adapter. Every tool uses the shared USB2XXX SDK."""
from __future__ import annotations

from argparse import ArgumentParser
from collections import OrderedDict
from dataclasses import asdict
import logging
from threading import RLock
from time import monotonic, sleep
from typing import Annotated

from mcp.server.fastmcp import FastMCP
from pydantic import Field

from . import ADCBlock, Busy, Deadline, DeviceError, I2CMessage, RemoteError, USB2XXX, list_devices
from .cli import connection_args, settings

log = logging.getLogger("usb2xxx.mcp")
Port = Annotated[int, Field(ge=0, le=1)]
Channel = Annotated[int, Field(ge=0, le=18)]
ReadLength = Annotated[int, Field(ge=1, le=248)]


class Adapter:
    def __init__(self, options: dict, allow_writes: bool):
        self.options = options
        self.simulated = options["mock"]
        self.allow_writes = allow_writes
        self.device: USB2XXX | None = None
        self.device_id = "mock"
        self.lock = RLock()
        self.captures: OrderedDict[int, ADCBlock] = OrderedDict()
        self.next_capture = 0

    def devices(self) -> list[dict]:
        if self.simulated:
            return list_devices(mock=True)
        from hashlib import sha256
        from .transport import discover
        serial = self.options["serial"]
        return [{"device_id": sha256(serial.encode()).hexdigest()[:12], "simulated": False}
                for candidate in discover(self.options["vid"], self.options["pid"])
                if candidate.serial_number == serial][:1]

    def result(self, status: str, actual=None, *, warnings=(), conflicts=()) -> dict:
        return {"device_id": self.device_id, "simulated": self.simulated,
                "request_id": self.device.last_request_id if self.device else 0,
                "status": status, "actual_values": {} if actual is None else actual,
                "warnings": list(warnings), "resource_conflicts": list(conflicts)}

    def use(self, action, *, mutating=False, confirm=False, dry_run=False, warnings=()) -> dict:
        with self.lock:
            try:
                if dry_run:
                    return self.result("dry_run", {"can_write": self.allow_writes,
                                                   "requires_confirmation": bool(mutating)},
                                       warnings=warnings)
                if mutating and not (self.allow_writes and confirm):
                    return self.result("permission_denied", warnings=(
                        "Enable writes when launching the server and pass confirm=true.",))
                if self.device is None:
                    if not self.simulated:
                        devices = self.devices()
                        if len(devices) != 1:
                            return self.result("unavailable", warnings=("Authorized device unavailable.",))
                        self.device_id = devices[0]["device_id"]
                    self.device = USB2XXX.open(**self.options)
                values = action(self.device)
                if mutating:
                    log.info("Authorized device action completed for %s", self.device_id)
                return self.result("ok", values, warnings=warnings)
            except Busy as error:
                return self.result("busy", {"status_code": error.status},
                                   conflicts=(error.detail.get("resource", "occupied"),))
            except RemoteError as error:
                return self.result("device_error", {"status_code": error.status,
                                                     "partial_bytes": len(error.partial)})
            except (DeviceError, ValueError, OSError) as error:
                return self.result("error", warnings=(str(error),))

    def close(self) -> None:
        with self.lock:
            if self.device is not None:
                dev, self.device = self.device, None
                dev.close()


def create_server(adapter: Adapter) -> FastMCP:
    server = FastMCP("USB2Box", instructions="Local USB2XXX device control and diagnostics.")

    @server.tool()
    def usb2xxx_list_devices() -> dict:
        """List the configured local USB2Box device or simulator."""
        return adapter.result("ok", adapter.devices())

    @server.tool()
    def usb2xxx_get_capabilities() -> dict:
        """Read protocol version, transfer limits and lease duration."""
        return adapter.use(lambda dev: dev.capabilities())

    @server.tool()
    def usb2xxx_get_status() -> dict:
        """Read session limits and resource ownership."""
        return adapter.use(lambda dev: {"capabilities": dev.capabilities(),
                                        "owners": dev.resources()})

    @server.tool()
    def usb2xxx_i2c_transfer(
        bus_id: Port, address: Annotated[int, Field(ge=8, le=119)],
        messages: Annotated[list[dict], Field(min_length=1, max_length=8)],
        confirm: bool = False, dry_run: bool = False,
    ) -> dict:
        """Perform a bounded master transfer with repeated START between messages."""
        def transfer(dev):
            decoded = []
            for msg in messages:
                if set(msg) == {"read"} and type(msg["read"]) is int and 1 <= msg["read"] <= 248:
                    decoded.append(I2CMessage(read=msg["read"]))
                elif set(msg) == {"hex"} and isinstance(msg["hex"], str) and len(msg["hex"]) <= 512:
                    decoded.append(I2CMessage(data=bytes.fromhex(msg["hex"])))
                else:
                    raise ValueError("each message requires exactly one bounded hex or read field")
            if sum(msg.read + len(msg.data) for msg in decoded) > 192:
                raise ValueError("I²C request exceeds 192 bytes")
            dev.i2c_configure(bus_id, 10000)
            try:
                return {"hex": dev.i2c_transfer(bus_id, address, decoded, 150).hex()}
            finally:
                dev.i2c_release(bus_id)

        return adapter.use(transfer, mutating=True, confirm=confirm, dry_run=dry_run,
                           warnings=("Weak internal pull-ups may prevent physical communication.",))

    @server.tool()
    def usb2xxx_i2c_slave_configure(bus_id: Port, address: Annotated[int, Field(ge=8, le=119)],
                                    reply_hex: Annotated[str, Field(max_length=128)] = "",
                                    confirm: bool = False) -> dict:
        """Configure a 7-bit slave and up to 64 response bytes."""
        def configure(dev):
            reply = bytes.fromhex(reply_hex)
            dev.i2c_configure(bus_id, 10000)
            try:
                dev.i2c_role(bus_id, True, address)
                dev.i2c_slave_buffer(bus_id, reply)
            except BaseException:
                dev.i2c_release(bus_id)
                raise
            return {"address": address, "reply_bytes": len(reply)}
        return adapter.use(configure, mutating=True, confirm=confirm,
                           warnings=("Weak internal pull-ups may prevent physical communication.",))

    @server.tool()
    def usb2xxx_i2c_slave_events(bus_id: Port,
                                limit: Annotated[int, Field(ge=1, le=8)] = 4) -> dict:
        """Pop bounded slave events in FIFO order and report overflow."""
        def read(dev):
            records = []
            for _ in range(limit):
                event = dev.i2c_next_slave_event(bus_id)
                if event.kind is None:
                    break
                records.append({**asdict(event), "data": event.data.hex()})
            return {"events": records, "dropped": event.dropped}
        return adapter.use(read)

    @server.tool()
    def usb2xxx_i2c_release(bus_id: Port) -> dict:
        """Stop an idle I2C bus and release its pins."""
        return adapter.use(lambda dev: dev.i2c_release(bus_id))

    @server.tool()
    def usb2xxx_uart_configure(
        uart_id: Port, baud: Annotated[int, Field(ge=1200, le=1000000)] = 115200,
        parity: Annotated[int, Field(ge=0, le=2)] = 0,
        stops: Annotated[int, Field(ge=1, le=2)] = 1, confirm: bool = False,
    ) -> dict:
        """Configure one UART and start its receive buffer."""
        return adapter.use(lambda dev: dev.uart_configure(uart_id, baud, parity, stops),
                           mutating=True, confirm=confirm)

    @server.tool()
    def usb2xxx_uart_write(uart_id: Port, hex_data: Annotated[str, Field(max_length=1024)],
                          confirm: bool = False, dry_run: bool = False) -> dict:
        """Queue up to 512 bytes on a configured UART."""
        return adapter.use(lambda dev: {"queued": dev.uart_write(uart_id, bytes.fromhex(hex_data))},
                           mutating=True, confirm=confirm, dry_run=dry_run)

    @server.tool()
    def usb2xxx_uart_read(uart_id: Port, limit: ReadLength = 128) -> dict:
        """Return a bounded hex chunk and UART counters."""
        return adapter.use(lambda dev: {"hex": dev.uart_read(uart_id, limit).hex(),
                                        "counters": dev.uart_stats(uart_id)})

    @server.tool()
    def usb2xxx_pwm_configure(group: Port, frequency_hz: Annotated[int, Field(ge=1, le=100000)],
                              duty_ppm: Annotated[int, Field(ge=0, le=1000000)],
                              invert: bool = False, confirm: bool = False,
                              dry_run: bool = False) -> dict:
        """Prepare a bounded configuration without starting PWM output."""
        return adapter.use(lambda dev: dev.pwm_configure(group, frequency_hz, duty_ppm, invert),
                           mutating=True, confirm=confirm, dry_run=dry_run)

    @server.tool()
    def usb2xxx_pwm_start(group: Port, duration_ms: Annotated[int, Field(ge=1, le=1000)],
                          pulses: Annotated[int, Field(ge=0, le=10000)] = 0,
                          arm: bool = False, confirm: bool = False) -> dict:
        """Start a prepared PWM output for at most one second."""
        return adapter.use(lambda dev: dev.pwm_start(group, duration_ms, pulses),
                           mutating=True, confirm=arm and confirm)

    @server.tool()
    def usb2xxx_pwm_update(group: Port, frequency_hz: Annotated[int, Field(ge=1, le=100000)],
                           duty_ppm: Annotated[int, Field(ge=0, le=1000000)],
                           invert: bool = False, confirm: bool = False) -> dict:
        """Queue a period-boundary update without extending the PWM deadline."""
        return adapter.use(lambda dev: dev.pwm_update(group, frequency_hz, duty_ppm, invert),
                           mutating=True, confirm=confirm)

    @server.tool()
    def usb2xxx_pwm_stop(group: Port) -> dict:
        """Stop and release one PWM group."""
        return adapter.use(lambda dev: dev.pwm_stop(group))

    @server.tool()
    def usb2xxx_spi_transfer(
        segments: Annotated[list[str], Field(min_length=1, max_length=8)],
        mode: Annotated[int, Field(ge=0, le=3)] = 0,
        bits: Annotated[int, Field(ge=8, le=16)] = 8,
        cs: Annotated[int, Field(ge=0, le=3)] = 0,
        hz: Annotated[int, Field(ge=100000, le=8000000)] = 1000000,
        confirm: bool = False, dry_run: bool = False,
    ) -> dict:
        """Transfer at most 1024 bytes, holding CS between SPI segments."""
        def transfer(dev):
            if sum(len(segment) for segment in segments) > 2048:
                raise ValueError("SPI input exceeds 2048 hex characters")
            data = [bytes.fromhex(segment) for segment in segments]
            return {"segments": [item.hex() for item in dev.spi_transfer(
                data, mode=mode, bits=bits, cs=cs, hz=hz, timeout_ms=150)]}
        return adapter.use(transfer, mutating=True, confirm=confirm, dry_run=dry_run)

    @server.tool()
    def usb2xxx_adc_read(channel: Channel) -> dict:
        """Read one raw ADC sample on an available channel."""
        return adapter.use(lambda dev: {"sample": dev.adc_read_once(channel)})

    @server.tool()
    def usb2xxx_adc_capture(channel: Channel,
                            samples: Annotated[int, Field(ge=1, le=4096)] = 256,
                            sample_rate: Annotated[int, Field(ge=1000, le=100000)] = 10000,
                            confirm: bool = False) -> dict:
        """Perform a bounded capture and return a local in-memory capture ID."""
        def capture(dev):
            data: list[int] = []
            dev.adc_configure([(channel, 7 if channel >= 16 else 2)])
            try:
                dev.adc_start(sample_rate, samples)
                until = monotonic() + samples / sample_rate + 1.0
                while True:
                    block = dev.adc_read()
                    data.extend(block.samples)
                    if not block.running and not block.more_available:
                        break
                    if monotonic() >= until:
                        raise Deadline("capture completion deadline")
                    sleep(0.005)
            finally:
                dev.adc_stop()
            completed = ADCBlock(block.rate, 0, block.dropped, block.resolution, False, False, False, block.threshold_seen, tuple(data))
            adapter.next_capture += 1
            capture_id = adapter.next_capture
            adapter.captures[capture_id] = completed
            while len(adapter.captures) > 3:
                adapter.captures.popitem(last=False)
            return {"capture_id": capture_id, "total": len(data),
                    "sample_rate": completed.rate, "dropped": completed.dropped}
        return adapter.use(capture, mutating=True, confirm=confirm)

    @server.tool()
    def usb2xxx_read_capture_chunk(capture_id: Annotated[int, Field(ge=1)],
                                   offset: Annotated[int, Field(ge=0, le=4096)] = 0,
                                   count: ReadLength = 128) -> dict:
        """Read a limited range from one of the three most recent captures."""
        with adapter.lock:
            if capture_id not in adapter.captures:
                return adapter.result("unknown_capture")
            block = adapter.captures[capture_id]
            return adapter.result("ok", {"capture_id": capture_id, "offset": offset,
                                         "samples": list(block.samples[offset:offset + count])})

    @server.tool()
    def usb2xxx_get_diagnostics() -> dict:
        """Inspect current resource ownership and recent capture summaries."""
        return adapter.use(lambda dev: {
            "owners": dev.resources(),
            "protocol_counters": dev.diagnostics(),
            "captures": {str(key): {name: value for name, value in asdict(block).items()
                                   if name != "samples"} for key, block in adapter.captures.items()},
        })

    @server.tool()
    def usb2xxx_self_test(confirm: bool = False) -> dict:
        """Run the safe simulator SPI loopback or the explicitly wired real SPI loopback."""
        def run(dev):
            data = b"\x55\xaa\x00\xff"
            return {"loopback": dev.spi_transfer([data]) == [data]}
        return adapter.use(run, mutating=True, confirm=confirm,
                           warnings=("On real hardware, confirm the MOSI-to-MISO jumper first.",))

    @server.tool()
    def usb2xxx_stop_all() -> dict:
        """End the device session and stop all outputs."""
        with adapter.lock:
            if adapter.device is None:
                return adapter.result("ok", {"session": "closed"})
            try:
                adapter.device.stop_all()
            except DeviceError as error:
                return adapter.result("error", warnings=(str(error),))
            finally:
                adapter.close()
            return adapter.result("ok", {"session": "closed"})

    @server.resource("usb2xxx://devices/{device_id}/capabilities")
    def capabilities(device_id: str) -> str:
        import json
        selected = adapter.devices()
        if device_id not in {dev["device_id"] for dev in selected}:
            return json.dumps(adapter.result("unknown_device"))
        return json.dumps(usb2xxx_get_capabilities())

    @server.resource("usb2xxx://devices/{device_id}/status")
    def status(device_id: str) -> str:
        import json
        selected = adapter.devices()
        if device_id not in {dev["device_id"] for dev in selected}:
            return json.dumps(adapter.result("unknown_device"))
        return json.dumps(usb2xxx_get_status())

    return server


def main() -> None:
    parser = ArgumentParser(prog="usb2xxx-mcp")
    connection_args(parser)
    parser.add_argument("--allow-writes", action="store_true")
    args = parser.parse_args()
    if not args.mock and (not args.serial or not args.vid or not args.pid):
        parser.error("real devices require an authorized --serial and assigned --vid and --pid")
    logging.basicConfig(level=logging.INFO)
    adapter = Adapter(settings(args), args.allow_writes)
    try:
        create_server(adapter).run(transport="stdio")
    finally:
        adapter.close()


if __name__ == "__main__":
    main()
