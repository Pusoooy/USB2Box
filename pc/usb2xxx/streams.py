"""Single reader for the credit-controlled bulk endpoint."""
from __future__ import annotations

from secrets import randbelow
from threading import Condition, Lock, Thread
from time import monotonic
from typing import Callable

from .protocol import Deadline, DeviceError, Frame, FrameReader, ProtocolError
from .transport import Transport

StreamKey = tuple[int, int, int]  # service, operation, stream ID


class StreamSubscription:
    def __init__(self, receiver: StreamReceiver, key: StreamKey, grant: Callable[[int], None]):
        self.receiver = receiver
        self.key = key
        self.tag = randbelow(0xffffffff) + 1
        self._grant = grant
        self._read_lock = Lock()
        self.closed = False
        self.pending = False
        self.frame: Frame | None = None
        self.sequence: int | None = None

    def read(self, timeout: float) -> Frame:
        end = monotonic() + timeout
        receiver = self.receiver
        with self._read_lock:
            with receiver.condition:
                receiver.check(self)
                if end <= monotonic() and self.frame is None:
                    raise Deadline("bulk stream read deadline")
                grant = not self.pending
                if grant:
                    self.pending = True
            if grant:
                try:
                    self._grant(self.tag)
                except BaseException:
                    # A failed control round-trip may still have granted the credit.
                    # Dispose of this tag before issuing further grants.
                    self.close()
                    raise
            with receiver.condition:
                while self.frame is None:
                    receiver.check(self)
                    remaining = end - monotonic()
                    if remaining <= 0:
                        raise Deadline("bulk stream read deadline")
                    receiver.condition.wait(remaining)
                receiver.check(self)
                frame, self.frame = self.frame, None
                self.pending = False
                return frame

    def close(self) -> None:
        self.receiver.unregister(self)


class StreamReceiver:
    def __init__(self, transport: Transport):
        self.condition = Condition()
        self._reader = FrameReader(transport, 0x82)
        self._subscriptions: dict[StreamKey, StreamSubscription] = {}
        self._closed = False
        self._failure: DeviceError | None = None
        self._thread = Thread(target=self._run, name="usb2xxx-bulk-in", daemon=True)
        self._thread.start()

    def subscribe(self, key: StreamKey, grant: Callable[[int], None]) -> StreamSubscription:
        with self.condition:
            subscription = StreamSubscription(self, key, grant)
            self.check(subscription)
            if key in self._subscriptions:
                raise DeviceError("bulk stream already subscribed")
            self._subscriptions[key] = subscription
            return subscription

    def unregister(self, subscription: StreamSubscription) -> None:
        with self.condition:
            if self._subscriptions.get(subscription.key) is subscription:
                del self._subscriptions[subscription.key]
            subscription.closed = True
            subscription.frame = None
            self.condition.notify_all()

    def check(self, subscription: StreamSubscription) -> None:
        if self._failure is not None:
            raise self._failure
        if self._closed or subscription.closed:
            raise DeviceError("bulk stream is closed")

    def _run(self) -> None:
        while True:
            with self.condition:
                if self._closed:
                    return
            try:
                frame = self._reader.read(0.05)
                self._deliver(frame)
            except Deadline:
                continue
            except DeviceError as error:
                with self.condition:
                    self._failure = error
                    self.condition.notify_all()
                return

    def _deliver(self, frame: Frame) -> None:
        if frame.kind != 4 or frame.status or frame.request:
            raise ProtocolError("unexpected frame on bulk stream endpoint")
        with self.condition:
            subscription = self._subscriptions.get((frame.service, frame.opcode, frame.stream))
            if subscription is None:
                return  # May be an old, fully framed packet left in the endpoint.
            if len(frame.payload) < 4:
                raise ProtocolError("bulk stream frame has no subscription tag")
            if int.from_bytes(frame.payload[:4], "little") != subscription.tag:
                return
            if not subscription.pending or subscription.frame is not None:
                raise ProtocolError("bulk stream sent a frame without credit")
            if subscription.sequence is not None and frame.sequence != subscription.sequence:
                raise ProtocolError("bulk stream sequence gap")
            subscription.sequence = (frame.sequence + 1) & 0xffff
            subscription.frame = frame
            self.condition.notify_all()

    def close(self) -> None:
        with self.condition:
            self._closed = True
            self.condition.notify_all()
        self._thread.join()
