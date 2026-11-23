"""USB2Box v1 wire codec shared by every PC entry point."""
import struct

HEADER = struct.Struct("<4sBBBBHBBHHIIIIHH")
FRAGMENT, MESSAGE = 512, 1024
FIRST, LAST, ERROR = 1, 2, 8


class DeviceError(Exception):
    """A typed host or device failure."""


class Disconnected(DeviceError):
    pass


class Deadline(DeviceError):
    pass


class ProtocolError(DeviceError):
    pass


class RemoteError(DeviceError):
    def __init__(self, status: int, partial: bytes = b""):
        self.status, self.partial = status, partial
        super().__init__(f"device status 0x{status:04x}")


class Busy(RemoteError):
    pass


class Cancelled(RemoteError):
    pass


class LeaseExpired(RemoteError):
    pass


class BufferFull(RemoteError):
    pass


def check(status: int, partial: bytes = b"") -> None:
    if status:
        error = {0x2001: Busy, 0x6001: Cancelled, 0x7001: LeaseExpired,
                 0x5001: BufferFull}.get(status, RemoteError)
        raise error(status, partial)


def packet(service: int, opcode: int, request: int, data: bytes) -> bytes:
    if len(data) > MESSAGE:
        raise ValueError("request exceeds negotiated 1024-byte limit")
    frames = []
    for offset in range(0, max(len(data), 1), FRAGMENT):
        body = data[offset:offset + FRAGMENT]
        flags = (FIRST if offset == 0 else 0) | (LAST if offset + len(body) == len(data) else 0)
        frames.append(HEADER.pack(b"U2SX", 1, 0, HEADER.size, 1, flags, service, opcode,
                                  0, 0, request, len(body), len(data), offset, 0,
                                  offset // FRAGMENT) + body)
    return b"".join(frames)


def receive(transport, endpoint: int, timeout: float,
            expected: tuple[int, int, int] | None = None) -> tuple[int, bytes]:
    from time import monotonic

    end = monotonic() + timeout
    header = transport.read_exact(endpoint, HEADER.size, timeout)
    (magic, major, minor, size, kind, flags, service, opcode, status, reserved,
     request, length, total, offset, stream, sequence) = HEADER.unpack(header)
    if (magic, major, minor, size, reserved, offset) != (b"U2SX", 1, 0, HEADER.size, 0, 0):
        raise ProtocolError("invalid frame header")
    if total != length or length > FRAGMENT or flags & ~(FIRST | LAST | ERROR):
        raise ProtocolError("invalid response length or flags")
    if flags & (FIRST | LAST) != FIRST | LAST or bool(flags & ERROR) != bool(status):
        raise ProtocolError("invalid response boundaries")
    if endpoint == 0x81:
        if (kind, stream, sequence) != (2, 0, 0) or expected != (service, opcode, request):
            raise ProtocolError("unexpected response ID or opcode")
    elif (kind, service, opcode, request, stream, status) != (4, 6, 7, 0, 1, 0):
        raise ProtocolError("unexpected stream frame")
    remaining = end - monotonic()
    if remaining <= 0:
        raise Deadline("response deadline")
    return sequence if endpoint == 0x82 else status, transport.read_exact(endpoint, length, remaining)
