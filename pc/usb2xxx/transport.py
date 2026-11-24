"""Bulk transports with per-device ownership and exact reads."""
from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from queue import Empty, Full, Queue
import hashlib
import os
import subprocess
import tempfile
from threading import Event, Thread
from time import monotonic

from .protocol import Busy, Deadline, Disconnected, FRAGMENT, HEADER, ProtocolError


class Transport(ABC):
    simulated = False

    @abstractmethod
    def write(self, data: bytes, timeout: float) -> None: ...

    @abstractmethod
    def read_exact(self, endpoint: int, size: int, timeout: float) -> bytes: ...

    @abstractmethod
    def close(self) -> None: ...


class MockTransport(Transport):
    simulated = True

    def __init__(self, executable: str | Path | None = None):
        path = Path(executable or os.getenv("USB2XXX_MOCK_DEVICE", "build/host/mock-device"))
        self._process = subprocess.Popen([str(path.resolve()), "--bulk"], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, bufsize=0)
        self._queues: dict[int, Queue[bytes]] = {0x81: Queue(32), 0x82: Queue(32)}
        self._buffers = {0x81: bytearray(), 0x82: bytearray()}
        self._ended = Event()
        self._reader_error: Exception | None = None
        self._reader = Thread(target=self._receive, daemon=True)
        self._reader.start()

    def _pipe_exact(self, count: int) -> bytes:
        data = bytearray()
        while len(data) < count:
            block = self._process.stdout.read(count - len(data))
            if not block:
                break
            data.extend(block)
        return bytes(data)

    def _receive(self) -> None:
        try:
            prefix = self._pipe_exact(3)
            if prefix == b"U2S":
                # Older simulator executables keep raw frames on stdout.
                self._queues[0x81].put_nowait(prefix)
                while block := self._process.stdout.read(1024):
                    self._queues[0x81].put_nowait(block)
                return
            while prefix:
                if len(prefix) != 3 or prefix[0] not in self._queues:
                    raise ProtocolError("invalid simulator endpoint record")
                length = int.from_bytes(prefix[1:], "little")
                if not HEADER.size <= length <= HEADER.size + FRAGMENT:
                    raise ProtocolError("invalid simulator record length")
                block = self._pipe_exact(length)
                if len(block) != length:
                    raise Disconnected("simulator stopped during a frame")
                self._queues[prefix[0]].put_nowait(block)
                prefix = self._pipe_exact(3)
        except Full:
            self._reader_error = ProtocolError("simulator endpoint queue overflow")
        except (Disconnected, ProtocolError, OSError) as error:
            self._reader_error = error
        finally:
            self._ended.set()

    def write(self, data: bytes, timeout: float) -> None:
        try:
            sent = 0
            while sent < len(data):
                count = self._process.stdin.write(data[sent:])
                if not count:
                    raise Disconnected("mock pipe write returned no bytes")
                sent += count
        except (BrokenPipeError, OSError, ValueError) as error:
            raise Disconnected("mock device closed") from error

    def read_exact(self, endpoint: int, size: int, timeout: float) -> bytes:
        buffer = self._buffers[endpoint]
        queue = self._queues[endpoint]
        end = monotonic() + timeout
        while len(buffer) < size:
            if self._ended.is_set() and queue.empty():
                if self._reader_error:
                    raise Disconnected("mock device link failed") from self._reader_error
                raise Disconnected("mock device exited")
            try:
                data = queue.get(timeout=max(0.0, min(0.05, end - monotonic())))
            except Empty as error:
                if monotonic() >= end:
                    raise Deadline("mock response deadline") from error
                continue
            buffer.extend(data)
        result = bytes(buffer[:size])
        del buffer[:size]
        return result

    def close(self) -> None:
        self._process.stdin.close()
        try:
            self._process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait()
        self._reader.join(timeout=1)
        self._process.stdout.close()


def discover(vid: int, pid: int):
    if not 1 <= vid <= 0xffff or not 1 <= pid <= 0xffff:
        raise ValueError("supply a legally assigned nonzero VID/PID")
    import usb.core
    import usb.backend.libusb1
    location = os.getenv("USB2XXX_LIBUSB")
    backend = usb.backend.libusb1.get_backend(find_library=lambda _: location) if location else None
    if location and backend is None:
        raise Disconnected("Unable to load configured libusb library")
    try:
        return list(usb.core.find(find_all=True, idVendor=vid, idProduct=pid, backend=backend))
    except usb.core.NoBackendError as error:
        raise Disconnected("Install libusb or set USB2XXX_LIBUSB") from error
    except usb.core.USBError as error:
        raise Disconnected("USB device enumeration failed") from error


def acquire_device_lock(path: Path):
    """Hold a nonblocking per-device lock until the returned file is closed."""
    lock = path.open("a+b")
    try:
        if os.name == "nt":
            import msvcrt
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as error:
        lock.close()
        raise Busy(0x2001) from error
    return lock


class BulkTransport(Transport):
    def __init__(self, vid: int, pid: int, serial: str | None = None):
        import usb.core
        import usb.util
        candidates = discover(vid, pid)
        if serial is not None:
            candidates = [dev for dev in candidates if dev.serial_number == serial]
        if len(candidates) != 1:
            raise Disconnected("select exactly one USB2Box by VID/PID and optional serial")
        self._usb = usb.core
        self._util = usb.util
        self._device = candidates[0]
        identity = serial or self._device.serial_number
        key = hashlib.sha256(f"{vid}:{pid}:{identity}".encode()).hexdigest()[:24]
        self._lock = None
        try:
            self._lock = acquire_device_lock(Path(tempfile.gettempdir()) / f"usb2xxx-{key}.lock")
            try:
                self._device.get_active_configuration()
            except usb.core.USBError:
                self._device.set_configuration(1)
            usb.util.claim_interface(self._device, 0)
            usb.util.claim_interface(self._device, 1)
        except BaseException:
            self.close()
            raise
        self._buffers = {0x81: bytearray(), 0x82: bytearray()}

    def _io(self, method, *args, timeout: float):
        try:
            return method(*args, timeout=max(1, int(timeout * 1000)))
        except self._usb.USBTimeoutError as error:
            raise Deadline("USB endpoint timeout") from error
        except self._usb.USBError as error:
            raise Disconnected("USB transfer unavailable") from error

    def write(self, data: bytes, timeout: float) -> None:
        if self._io(self._device.write, 0x01, data, timeout=timeout) != len(data):
            raise Disconnected("short USB write")

    def read_exact(self, endpoint: int, size: int, timeout: float) -> bytes:
        buffer = self._buffers[endpoint]
        end = monotonic() + timeout
        while len(buffer) < size:
            remaining = end - monotonic()
            if remaining <= 0:
                raise Deadline("USB response deadline")
            buffer.extend(self._io(self._device.read, endpoint, 64, timeout=remaining))
        result = bytes(buffer[:size])
        del buffer[:size]
        return result

    def close(self) -> None:
        try:
            self._util.dispose_resources(self._device)
        finally:
            if self._lock is not None:
                self._lock.close()
