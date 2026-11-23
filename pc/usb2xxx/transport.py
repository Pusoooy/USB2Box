"""Bulk transports with per-device ownership and exact reads."""
from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from queue import Empty, Queue
import hashlib
import os
import subprocess
import tempfile
from threading import Thread
from time import monotonic

from .protocol import Busy, Deadline, Disconnected


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
        self._process = subprocess.Popen([str(path.resolve())], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, bufsize=0)
        self._queue: Queue[bytes] = Queue(32)
        self._buffer = bytearray()
        self._reader = Thread(target=self._receive, daemon=True)
        self._reader.start()

    def _receive(self) -> None:
        while True:
            block = self._process.stdout.read(1024)
            self._queue.put(block)
            if not block:
                break

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
        if endpoint != 0x81:
            raise ValueError("MockTransport uses control reads")
        end = monotonic() + timeout
        while len(self._buffer) < size:
            try:
                data = self._queue.get(timeout=max(0.0, end - monotonic()))
            except Empty as error:
                raise Deadline("mock response deadline") from error
            if not data:
                raise Disconnected("mock device exited")
            self._buffer.extend(data)
        result = bytes(self._buffer[:size])
        del self._buffer[:size]
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
    return list(usb.core.find(find_all=True, idVendor=vid, idProduct=pid))


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
        self._lock = open(Path(tempfile.gettempdir()) / f"usb2xxx-{key}.lock", "a+b")
        try:
            self._acquire()
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

    def _acquire(self) -> None:
        try:
            if os.name == "nt":
                import msvcrt
                self._lock.seek(0)
                msvcrt.locking(self._lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self._lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise Busy(0x2001) from error

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
            self._lock.close()
