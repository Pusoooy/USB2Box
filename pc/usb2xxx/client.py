"""Session-owning API shared by CLI, GUI and MCP."""
from __future__ import annotations

import asyncio
from dataclasses import dataclass
import csv
from pathlib import Path
import struct
from threading import Event, RLock, Thread
from time import monotonic, sleep
from typing import AsyncIterator, Callable, Concatenate, ParamSpec, TypeVar

from .protocol import Busy, Deadline, DeviceError, ProtocolError, RemoteError, check, packet, receive
from .streams import StreamReceiver, StreamSubscription
from .transport import BulkTransport, MockTransport, Transport, discover

Parameters = ParamSpec("Parameters")
Result = TypeVar("Result")

U16 = struct.Struct("<H")
U32 = struct.Struct("<I")
SYS, I2C, UART, PWM, SPI, ADC = 0, 1, 2, 3, 4, 5
FEATURE_BULK_STREAMS = 1 << 0
FEATURE_I2C_EVENTS = 1 << 1
FEATURE_PWM_UPDATE = 1 << 2
FEATURE_ADC_TIMING = 1 << 3
FEATURE_I2C_RECOVERY = 1 << 4
FEATURE_DIAGNOSTIC_LOG = 1 << 5


@dataclass(frozen=True)
class I2CMessage:
    data: bytes = b""
    read: int = 0

    def encode(self) -> bytes:
        count = self.read or len(self.data)
        if not 1 <= count <= 255 or (self.read and self.data):
            raise ValueError("I2C message must be either a read length or write bytes")
        return bytes((bool(self.read), count)) + self.data


@dataclass(frozen=True)
class I2CSlaveEvent:
    sequence: int
    device_ms: int
    dropped: int
    received: int
    errors: int
    kind: str | None
    count: int
    data: bytes

    @classmethod
    def decode(cls, raw: bytes) -> I2CSlaveEvent:
        if len(raw) < 22:
            raise ProtocolError("short I2C slave event")
        sequence, tick, dropped, received, errors, kind, count = struct.unpack_from("<IIIIIBB", raw)
        if (kind > 3 or count > 64 or len(raw) != 22 + (count if kind == 1 else 0)
                or (kind in (0, 3) and count)):
            raise ProtocolError("invalid I2C slave event")
        return cls(sequence, tick, dropped, received, errors, (None, "rx", "tx", "error")[kind],
                   count, raw[22:])


@dataclass(frozen=True)
class ADCBlock:
    rate: int
    offset: int
    dropped: int
    resolution: int
    running: bool
    awaiting_trigger: bool
    more_available: bool
    threshold_seen: bool
    samples: tuple[int, ...]
    first_device_us: int | None = None
    scan_period_ns: int = 0
    first_rank: int = 0
    scan_channels: tuple[int, ...] = ()
    rank_offsets_ns: tuple[int, ...] = ()

    @classmethod
    def decode(cls, raw: bytes, *, timed: bool = False) -> ADCBlock:
        metadata = {}
        if timed:
            if len(raw) < 18:
                raise ProtocolError("short ADC timing header")
            _, first_us, scan_ns, ranks, rank = struct.unpack_from("<IQIBB", raw)
            prefix = 18 + ranks * 5
            if not 1 <= ranks <= 8 or rank >= ranks or len(raw) < prefix:
                raise ProtocolError("invalid ADC scan ranks")
            sequence = tuple(struct.iter_unpack("<BI", raw[18:prefix]))
            if any(channel > 18 for channel, _ in sequence):
                raise ProtocolError("invalid ADC scan channel")
            metadata = dict(first_device_us=first_us, scan_period_ns=scan_ns, first_rank=rank,
                            scan_channels=tuple(channel for channel, _ in sequence),
                            rank_offsets_ns=tuple(delay for _, delay in sequence))
            raw = raw[prefix:]
        if len(raw) < 16:
            raise ProtocolError("short ADC metadata")
        rate, offset, dropped, count, bits, flags = struct.unpack_from("<IIIHBB", raw)
        if len(raw) != 16 + count * 2 or bits not in (6, 8, 10, 12):
            raise ProtocolError("ADC metadata does not match samples")
        return cls(rate, offset, dropped, bits, bool(flags & 1), bool(flags & 2), bool(flags & 4), bool(flags & 8),
                   struct.unpack_from(f"<{count}H", raw, 16), **metadata)

    def sample_position(self, index: int) -> tuple[int, int]:
        """Return the scan rank and estimated device time in µs."""
        if self.first_device_us is None or not self.scan_channels:
            raise ValueError("this ADC block has no timing metadata")
        scans, rank = divmod(self.first_rank + index, len(self.scan_channels))
        delta = scans * self.scan_period_ns + self.rank_offsets_ns[rank] - self.rank_offsets_ns[self.first_rank]
        return rank, self.first_device_us + delta // 1000

    def volts(self, reference_v: float) -> tuple[float, ...]:
        if not 0 < reference_v <= 5:
            raise ValueError("reference voltage must be greater than zero and at most 5 V")
        full_scale = (1 << self.resolution) - 1
        return tuple(sample * reference_v / full_scale for sample in self.samples)

    def save(self, filename: str | Path) -> None:
        path = Path(filename)
        if path.suffix.lower() == ".csv":
            with path.open("w", newline="", encoding="utf-8") as file:
                writer = csv.writer(file)
                if self.first_device_us is None:
                    writer.writerow(("sample_index", "raw"))
                    writer.writerows(enumerate(self.samples, self.offset))
                else:
                    writer.writerow(("sample_index", "channel", "device_us_estimated", "raw"))
                    for index, sample in enumerate(self.samples):
                        rank, timestamp = self.sample_position(index)
                        writer.writerow((self.offset + index, self.scan_channels[rank], timestamp, sample))
        elif path.suffix.lower() == ".npy":
            import numpy
            numpy.save(path, numpy.asarray(self.samples, dtype="<u2"), allow_pickle=False)
        else:
            path.write_bytes(struct.pack(f"<{len(self.samples)}H", *self.samples))


@dataclass(frozen=True)
class UARTBlock:
    port: int
    offset: int
    device_ms: int
    dropped: int
    errors: int
    sequence: int
    data: bytes


class UARTSubscription:
    def __init__(self, device: USB2XXX, port: int, subscription: StreamSubscription):
        self._device = device
        self.port = port
        self._subscription = subscription

    def __enter__(self) -> UARTSubscription:
        return self

    def __exit__(self, *_):
        self.close()

    def read(self, timeout: float = 0.8) -> UARTBlock:
        frame = self._subscription.read(timeout)
        if len(frame.payload) <= 20:
            raise ProtocolError("invalid UART stream payload")
        offset, now, dropped, errors = struct.unpack_from("<IIII", frame.payload, 4)
        return UARTBlock(self.port, offset, now, dropped, errors, frame.sequence, frame.payload[20:])

    def close(self) -> None:
        self._device._uart_unsubscribe(self.port, self._subscription)


class USB2XXX:
    def __init__(self, transport: Transport):
        self._transport = transport
        self.simulated = transport.simulated
        self._lock = RLock()
        self._job_lock = RLock()
        self._adc_lock = RLock()
        self._bulk: StreamReceiver | None = None
        self._uart_streams: dict[int, StreamSubscription] = {}
        self._adc_stream: StreamSubscription | None = None
        self._features: int | None = None
        self._request = 0
        self._job = 0
        self._closed = Event()
        self._error: DeviceError | None = None
        self.last_request_id = 0
        try:
            self.lease = U32.unpack(self.call(SYS, 5))[0]
        except BaseException:
            transport.close()
            raise
        self._thread = Thread(target=self._heartbeats, daemon=True)
        self._thread.start()

    @classmethod
    def open(cls, *, mock: bool = False, executable: str | Path | None = None,
             vid: int = 0, pid: int = 0, serial: str | None = None) -> USB2XXX:
        return cls(MockTransport(executable) if mock else BulkTransport(vid, pid, serial))

    @classmethod
    async def open_async(cls, **kwargs) -> AsyncDevice:
        return AsyncDevice(await asyncio.to_thread(cls.open, **kwargs))

    def __enter__(self) -> USB2XXX:
        return self

    def __exit__(self, *_):
        self.close()

    def _heartbeats(self) -> None:
        while not self._closed.wait(0.4):
            try:
                self.call(SYS, 6, U32.pack(self.lease), timeout=0.3)
            except DeviceError as error:
                self._error = error
                return

    def call(self, service: int, opcode: int, payload: bytes = b"",
             timeout: float = 0.8) -> bytes:
        with self._lock:
            if self._error is not None:
                raise self._error
            if self._closed.is_set():
                raise DeviceError("device session is closed")
            self._request = self._request % 0xffffffff + 1
            request = self._request
            outbound = packet(service, opcode, request, payload)
            end = monotonic() + timeout
            try:
                self._transport.write(outbound, timeout)
                remaining = end - monotonic()
                if remaining <= 0:
                    raise Deadline("control write deadline")
                status, raw = receive(self._transport, 0x81, remaining, (service, opcode, request))
            except (Deadline, ProtocolError) as error:
                self._error = error  # A late reply makes this transport session ambiguous.
                raise
            self.last_request_id = request
            try:
                check(status)
            except RemoteError as error:
                if len(raw) == 12:
                    stage, resource, retry, _, tick, completed = struct.unpack("<BBBBII", raw)
                    error.detail = {"stage": stage, "resource": None if resource == 255 else resource,
                                    "retryable": bool(retry), "device_ms": tick, "completed": completed}
                raise
            return raw

    def close(self) -> None:
        with self._lock:
            if self._closed.is_set():
                return
            try:
                if self.lease:
                    self.call(SYS, 7, U32.pack(self.lease), timeout=0.3)
            except DeviceError:
                pass  # The firmware lease expires independently of this process.
            finally:
                self._closed.set()
        self._thread.join(timeout=1)
        try:
            if self._bulk:
                self._bulk.close()
        finally:
            self._transport.close()

    def stop_all(self) -> None:
        self.call(SYS, 10)
        self.close()

    def capabilities(self) -> dict:
        fragment, maximum, lease = struct.unpack("<HHI", self.call(SYS, 2))
        version = tuple(self.call(SYS, 3))
        return {"version": version, "fragment": fragment, "message": maximum,
                "lease_ms": lease, "features": self.features(), "simulated": self.simulated}

    def features(self) -> int:
        with self._lock:
            if self._features is None:
                try:
                    self._features = U32.unpack(self.call(SYS, 12))[0]
                except RemoteError as error:
                    if error.status != 2:
                        raise
                    self._features = 0
            return self._features

    def _bulk_reader(self) -> StreamReceiver:
        with self._lock:
            if self._closed.is_set():
                raise DeviceError("device session is closed")
            if self._bulk is None:
                self._bulk = StreamReceiver(self._transport)
            return self._bulk

    def diagnostics(self) -> dict:
        values = struct.unpack("<IIIII", self.call(6, 1))
        return dict(zip(("requests", "invalid_headers", "errors", "lease_expired", "resets"),
                        values, strict=True))

    def diagnostic_log(self, limit: int = 16, minimum_level: int = 0) -> dict:
        raw = self.call(6, 2, bytes((minimum_level, limit)))
        if len(raw) < 8 or len(raw) != 8 + raw[4] * 16:
            raise ProtocolError("invalid diagnostic log")
        fields = ("device_ms", "request_id", "status", "level", "kind", "service", "opcode")
        return {
            "overwritten": U32.unpack_from(raw)[0],
            "entries": [dict(zip(fields, record[:7], strict=True))
                        for record in struct.iter_unpack("<IIHBBBBH", raw[8:])],
        }

    def resources(self) -> list[int]:
        return list(self.call(SYS, 8))

    def uart_configure(self, port: int, baud: int = 115200,
                       parity: int = 0, stops: int = 1) -> None:
        self.call(UART, 1, struct.pack("<BIBB", port, baud, parity, stops))
        try:
            self.call(UART, 2, bytes((port,)))
        except RemoteError:
            self.uart_stop(port)
            raise

    def uart_write(self, port: int, data: bytes) -> int:
        return U16.unpack(self.call(UART, 4, bytes((port,)) + data))[0]

    def uart_read(self, port: int, limit: int = 512) -> bytes:
        return self.call(UART, 3, struct.pack("<BH", port, limit))

    def uart_stats(self, port: int) -> tuple[int, int, int]:
        return struct.unpack("<III", self.call(UART, 6, bytes((port,))))

    def uart_stop(self, port: int) -> None:
        with self._lock:
            self.call(UART, 5, bytes((port,)))
            stream = self._uart_streams.pop(port, None)
            if stream is not None:
                stream.close()

    def uart_subscribe(self, port: int) -> UARTSubscription:
        if port not in (0, 1):
            raise ValueError("UART port must be 0 or 1")
        with self._lock:
            if not self.features() & FEATURE_BULK_STREAMS:
                raise DeviceError("firmware does not support UART bulk streaming")
            if port in self._uart_streams:
                raise Busy(0x2001)
            channel = bytes((port,))
            subscription = self._bulk_reader().subscribe(
                (UART, 8, port + 2), lambda tag: self.call(UART, 8, channel + U16.pack(1)))
            try:
                self.call(UART, 7, channel + U32.pack(subscription.tag))
            except BaseException:
                subscription.close()
                raise
            self._uart_streams[port] = subscription
            return UARTSubscription(self, port, subscription)

    def _uart_unsubscribe(self, port: int, subscription: StreamSubscription) -> None:
        with self._lock:
            if self._uart_streams.get(port) is not subscription:
                return
            del self._uart_streams[port]
            subscription.close()
            if not self._closed.is_set():
                self.call(UART, 7, struct.pack("<BI", port, 0))

    def _new_job(self) -> int:
        with self._job_lock:
            self._job = self._job % 0xffffffff + 1
            return self._job

    def _collect(self, service: int, opcode: int, token: bytes,
                 timeout_ms: int, cancel: int) -> bytes:
        end = monotonic() + timeout_ms / 1000 + 0.2
        while monotonic() < end:
            try:
                raw = self.call(service, opcode, token)
            except Busy:
                sleep(0.005)
                continue
            status, count = struct.unpack_from("<HH", raw)
            if len(raw) != 4 + count:
                raise ProtocolError("invalid job result")
            check(status, raw[4:])
            return raw[4:]
        try:
            self.call(service, cancel, token)
            try:
                self.call(service, opcode, token)
            except RemoteError:
                pass
        except RemoteError:
            pass
        raise Deadline("job completion deadline")

    def i2c_configure(self, bus: int, hz: int = 10000) -> None:
        self.call(I2C, 1, struct.pack("<BI", bus, hz))

    def i2c_role(self, bus: int, slave: bool = False, address: int = 0) -> None:
        self.call(I2C, 2, bytes((bus, slave, address)))

    def i2c_slave_buffer(self, bus: int, data: bytes) -> None:
        self.call(I2C, 4, bytes((bus,)) + data)

    def i2c_slave_event(self, bus: int) -> tuple[int, int, bytes]:
        raw = self.call(I2C, 5, bytes((bus,)))
        return *struct.unpack_from("<II", raw), raw[8:]

    def i2c_next_slave_event(self, bus: int) -> I2CSlaveEvent:
        return I2CSlaveEvent.decode(self.call(I2C, 7, bytes((bus,))))

    def i2c_transfer(self, bus: int, address: int, messages: list[I2CMessage],
                     timeout_ms: int = 100) -> bytes:
        job = self._new_job()
        raw = struct.pack("<BIIBB", bus, job, timeout_ms, address, len(messages))
        self.call(I2C, 3, raw + b"".join(message.encode() for message in messages))
        return self._collect(SYS, 11, U32.pack(job), timeout_ms, 9)

    def i2c_release(self, bus: int) -> None:
        self.call(I2C, 6, bytes((bus,)))

    def i2c_recover(self, bus: int, timeout_ms: int = 100) -> dict:
        job = self._new_job()
        self.call(I2C, 8, struct.pack("<BII", bus, job, timeout_ms))
        raw = self._collect(SYS, 11, U32.pack(job), timeout_ms, 9)
        if len(raw) != 2:
            raise ProtocolError("invalid I²C recovery result")
        return {"pulses": raw[0], "scl_high": bool(raw[1] & 1), "sda_high": bool(raw[1] & 2)}

    def pwm_configure(self, group: int, frequency_hz: int, duty_ppm: int,
                      invert: bool = False) -> dict:
        raw = self.call(PWM, 1, struct.pack("<BIIB", group, frequency_hz, duty_ppm, invert))
        return self._pwm_state(raw)

    def pwm_update(self, group: int, frequency_hz: int, duty_ppm: int,
                   invert: bool = False) -> dict:
        raw = self.call(PWM, 3, struct.pack("<BIIB", group, frequency_hz, duty_ppm, invert))
        return self._pwm_state(raw)

    @staticmethod
    def _pwm_state(raw: bytes) -> dict:
        mhz, ppm, psc, arr, flags = struct.unpack("<IIIIB", raw)
        return {"frequency_hz": mhz / 1000, "duty_ppm": ppm, "psc": psc,
                "arr": arr, "running": bool(flags & 1), "update_pending": bool(flags & 2)}

    def pwm_start(self, group: int, duration_ms: int, pulses: int = 0) -> dict:
        return self._pwm_state(self.call(PWM, 2, struct.pack("<BII", group, duration_ms, pulses)))

    def pwm_status(self, group: int) -> dict:
        return self._pwm_state(self.call(PWM, 5, bytes((group,))))

    def pwm_capture_start(self, frequency: int = 1000000) -> None:
        self.call(PWM, 6, U32.pack(frequency))

    def pwm_capture_read(self) -> dict:
        period, high, rate = struct.unpack("<III", self.call(PWM, 7))
        return {"frequency_hz": rate / period, "duty_ppm": round(high * 1000000 / period),
                "counter_hz": rate}

    def pwm_capture_stop(self) -> None:
        self.call(PWM, 8)

    def pwm_stop(self, group: int) -> None:
        self.call(PWM, 4, bytes((group,)))

    def spi_transfer(self, segments: list[bytes], *, mode: int = 0, bits: int = 8,
                     cs: int = 0, hz: int = 1000000, lsb: bool = False,
                     active_high: bool = False, setup_us: int = 0,
                     hold_us: int = 0, timeout_ms: int = 100) -> list[bytes]:
        if bits not in (8, 16) or not segments or any(not item or len(item) % (bits // 8)
                                                    for item in segments):
            raise ValueError("SPI segments must be aligned to the configured word size")
        config = struct.pack("<BBBBBBIHH", 0, mode, bits, lsb, cs, active_high,
                             hz, setup_us, hold_us)
        with self._job_lock:
            self.call(SPI, 1, config)
            try:
                self.call(SPI, 2, b"\0")
                result = []
                for segment in segments:
                    pieces = []
                    for offset in range(0, len(segment), 256):
                        job = self._new_job()
                        token = b"\0" + U32.pack(job)
                        try:
                            self.call(SPI, 3, token + U32.pack(timeout_ms) + segment[offset:offset + 256])
                            pieces.append(self._collect(SPI, 7, token, timeout_ms, 8))
                        except RemoteError as error:
                            error.partial = b"".join([*result, *pieces, error.partial])
                            raise
                    result.append(b"".join(pieces))
                if hold_us:
                    sleep((hold_us + 999) // 1000 / 1000 + 0.002)
                self.call(SPI, 4, b"\0")
            except BaseException:
                try:
                    self.call(SPI, 5, b"\0")
                except DeviceError:
                    pass
                raise
            else:
                self.call(SPI, 5, b"\0")
                return result

    def adc_configure(self, channels: list[tuple[int, int]], resolution: int = 12) -> None:
        self.call(ADC, 1, bytes((resolution, len(channels))) + b"".join(map(bytes, channels)))

    def adc_start(self, rate: int, samples: int = 0, pre: int = 0, post: int = 0) -> None:
        with self._adc_lock:
            self.call(ADC, 3, struct.pack("<IHHH", rate, samples, pre, post))
            self._adc_unsubscribe()

    def adc_watchdog(self, channel: int, low: int, high: int) -> None:
        self.call(ADC, 8, struct.pack("<BHH", channel, low, high))

    def adc_trigger(self) -> None:
        self.call(ADC, 5)

    def adc_read(self, limit: int = 248) -> ADCBlock:
        return ADCBlock.decode(self.call(ADC, 6, U16.pack(limit)))

    def adc_read_timed(self, limit: int = 219) -> ADCBlock:
        return ADCBlock.decode(self.call(ADC, 11, U16.pack(limit)), timed=True)

    def adc_estimate_reference(self) -> float:
        code, millivolts = struct.unpack("<HH", self.call(ADC, 12))
        measured = self.adc_read_once(17)
        if not measured:
            raise ProtocolError("zero VREFINT reading")
        return millivolts * code / (1000 * measured)

    def adc_stop(self) -> None:
        with self._adc_lock:
            try:
                self.call(ADC, 4)
            finally:
                self._adc_unsubscribe()

    def _adc_unsubscribe(self) -> None:
        if self._adc_stream is not None:
            self._adc_stream.close()
            self._adc_stream = None

    def adc_read_once(self, channel: int, sample_time: int = 7) -> int:
        with self._adc_lock:
            self.adc_configure([(channel, sample_time)])
            try:
                self.call(ADC, 2)
                until = monotonic() + 0.5
                while monotonic() < until:
                    block = self.adc_read(1)
                    if block.samples:
                        return block.samples[0]
                    sleep(0.005)
                raise Deadline("ADC single-sample deadline")
            finally:
                self.adc_stop()

    def adc_next_stream(self, timeout: float = 0.8) -> ADCBlock:
        with self._adc_lock:
            features = self.features()
            if features & FEATURE_BULK_STREAMS:
                timed = bool(features & FEATURE_ADC_TIMING)
                operation = 10 if timed else 9
                if self._adc_stream is None:
                    self._adc_stream = self._bulk_reader().subscribe(
                        (ADC, operation, 1),
                        lambda tag: self.call(ADC, operation, struct.pack("<IH", tag, 1)))
                frame = self._adc_stream.read(timeout)
                return ADCBlock.decode(frame.payload if timed else frame.payload[4:], timed=timed)
            if self.simulated:
                block = self.adc_read()
                if not block.samples:
                    sleep(0.005)
                return block
            self.call(ADC, 7, U16.pack(1))
            _sequence, data = receive(self._transport, 0x82, timeout)
            return ADCBlock.decode(data)


class AsyncDevice:
    def __init__(self, device: USB2XXX):
        self.device = device

    async def __aenter__(self) -> AsyncDevice:
        return self

    async def __aexit__(self, *_):
        await self.close()

    async def close(self) -> None:
        await asyncio.to_thread(self.device.close)

    async def call(self, service: int, opcode: int, payload: bytes = b"") -> bytes:
        return await asyncio.to_thread(self.device.call, service, opcode, payload)

    async def capabilities(self) -> dict:
        return await self.run(USB2XXX.capabilities)

    async def run(self, operation: Callable[Concatenate[USB2XXX, Parameters], Result],
                  *args: Parameters.args, **kwargs: Parameters.kwargs) -> Result:
        return await asyncio.to_thread(operation, self.device, *args, **kwargs)

    async def capture_stream(self) -> AsyncIterator[ADCBlock]:
        while True:
            block = await asyncio.to_thread(self.device.adc_next_stream)
            if block.samples:
                yield block
            if not block.running and not block.more_available:
                return

    async def receive_uart(self, port: int) -> AsyncIterator[UARTBlock]:
        subscription = await self.run(USB2XXX.uart_subscribe, port)
        try:
            while True:
                try:
                    yield await asyncio.to_thread(subscription.read, 0.2)
                except Deadline:
                    continue
        finally:
            await asyncio.to_thread(subscription.close)


def list_devices(*, mock: bool, vid: int = 0, pid: int = 0) -> list[dict]:
    if mock:
        return [{"device_id": "mock", "simulated": True}]
    from hashlib import sha256

    return [{"device_id": sha256((dev.serial_number or "").encode()).hexdigest()[:12],
             "simulated": False} for dev in discover(vid, pid)]
