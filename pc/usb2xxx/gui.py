"""PySide6 workbench for the shared USB2XXX session."""
from __future__ import annotations

from argparse import ArgumentParser
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
import json
from pathlib import Path
from threading import Event, Lock
import subprocess
import sys

from PySide6.QtCore import QObject, QPointF, QTimer, Signal
from PySide6.QtGui import QColor, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import (
    QApplication, QFileDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit,
    QMainWindow, QPlainTextEdit, QPushButton, QTabWidget, QVBoxLayout, QWidget,
)

from . import ADCBlock, I2CMessage, USB2XXX, list_devices
from .cli import connection_args, settings


class _Results(QObject):
    finished = Signal(object, object, object)


class Plot(QWidget):
    def __init__(self):
        super().__init__()
        self.values: deque[int] = deque(maxlen=2048)
        self.ceiling = 4095
        self.setMinimumHeight(150)

    def add(self, block: ADCBlock) -> None:
        self.values.extend(block.samples)
        self.ceiling = max(1, (1 << block.resolution) - 1)
        self.update()

    def paintEvent(self, _):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#16202e"))
        painter.setPen(QPen(QColor("#2f465c"), 1))
        for quarter in range(1, 4):
            y = self.height() * quarter // 4
            painter.drawLine(0, y, self.width(), y)
        if len(self.values) > 1:
            points = QPolygonF([QPointF(i * (self.width() - 2) / (len(self.values) - 1) + 1,
                                         (self.height() - 4) * (1 - value / self.ceiling) + 2)
                                  for i, value in enumerate(self.values)])
            painter.setPen(QPen(QColor("#64e6bc"), 1.5))
            painter.drawPolyline(points)


class Panel(QWidget):
    def __init__(self):
        super().__init__()
        self.rows = QFormLayout()
        self.layout = QVBoxLayout(self)
        self.layout.addLayout(self.rows)
        self.actions = QHBoxLayout()
        self.layout.addLayout(self.actions)
        self.fields: dict[str, QLineEdit] = {}

    def field(self, key: str, name: str, value: str) -> None:
        line = QLineEdit(value)
        self.fields[key] = line
        self.rows.addRow(name, line)

    def values(self) -> dict[str, str]:
        return {key: line.text() for key, line in self.fields.items()}

    def button(self, text, action) -> QPushButton:
        control = QPushButton(text)
        control.clicked.connect(action)
        self.actions.addWidget(control)
        return control


class Workbench(QMainWindow):
    def __init__(self, options: dict):
        super().__init__()
        self.options = options
        self.dev: USB2XXX | None = None
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="usb2box")
        self.emergency = ThreadPoolExecutor(max_workers=1, thread_name_prefix="usb2box-stop")
        self.connection_guard = Lock()
        self.events = _Results(self)
        self.events.finished.connect(self._finished)
        self.pending = 0
        self.stop_requested = Event()
        self.capturing = False
        self.last_block: ADCBlock | None = None

        self.setWindowTitle("USB2Box Workbench")
        self.resize(920, 680)
        root = QWidget()
        layout = QVBoxLayout(root)
        self.setCentralWidget(root)
        toolbar = QHBoxLayout()
        self.state = QLabel("SIMULATED · Disconnected" if options["mock"] else "USB · Disconnected")
        self.state.setStyleSheet("font-weight: 700; padding: 6px;")
        toolbar.addWidget(self.state, 1)
        for title, action in (("Connect", self.connect_device), ("Disconnect", self.disconnect),
                              ("Stop all", self.emergency_stop)):
            button = QPushButton(title)
            button.clicked.connect(action)
            toolbar.addWidget(button)
        layout.addLayout(toolbar)
        self.tabs = QTabWidget()
        layout.addWidget(self.tabs, 1)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.document().setMaximumBlockCount(450)
        self.log.setMaximumHeight(155)
        layout.addWidget(self.log)
        self._build_device()
        self._build_i2c()
        self._build_uart()
        self._build_pwm()
        self._build_spi()
        self._build_adc()
        self.timer = QTimer(self)
        self.timer.setInterval(20)
        self.timer.timeout.connect(self._poll_adc)
        self.timer.start()

    def submit(self, action, callback=None, *, require_device=True) -> None:
        if self.pending >= 8:
            self.log.appendPlainText("Command queue is full.")
            return
        self.pending += 1

        def run():
            if require_device and self.stop_requested.is_set():
                raise RuntimeError("Command cancelled by Stop all.")
            with self.connection_guard:
                dev = self.dev
            if require_device and dev is None:
                raise RuntimeError("Connect a device first.")
            return action(dev)

        future = self.executor.submit(run)

        def done(task):
            try:
                value = task.result()
            except Exception as error:
                self.events.finished.emit(callback, None, str(error))
            else:
                self.events.finished.emit(callback, value, None)
        future.add_done_callback(done)

    def _finished(self, callback, value, error) -> None:
        self.pending -= 1
        if error:
            if callback == self._on_adc:
                self.capturing = False
            self.log.appendPlainText(error)
        elif callback:
            callback(value)
        elif value is not None:
            self.log.appendPlainText(str(value))

    def connect_device(self) -> None:
        self.stop_requested.clear()

        def connect(dev):
            if dev is None:
                dev = USB2XXX.open(**self.options)
                with self.connection_guard:
                    cancelled = self.stop_requested.is_set()
                    if not cancelled:
                        self.dev = dev
                if cancelled:
                    dev.close()
                    raise RuntimeError("Connection cancelled by Stop all.")
            return dev.capabilities()

        def ready(capabilities):
            mode = "SIMULATED" if capabilities["simulated"] else "USB"
            self.state.setText(f"{mode} · Connected")
            self.log.appendPlainText(json.dumps(capabilities))

        self.submit(connect, ready, require_device=False)

    def _close(self, emergency: bool = False) -> bool:
        with self.connection_guard:
            dev, self.dev = self.dev, None
        if dev is not None:
            try:
                if emergency:
                    dev.stop_all()
            finally:
                dev.close()
        return True

    def _closed(self, _) -> None:
        mode = "SIMULATED" if self.options["mock"] else "USB"
        self.state.setText(f"{mode} · Disconnected")

    def disconnect(self) -> None:
        self.capturing = False
        self.submit(lambda _: self._close(), self._closed, require_device=False)

    def emergency_stop(self) -> None:
        self.capturing = False
        self.stop_requested.set()
        # The control is independent of the usual pending action limit.
        self.pending += 1
        future = self.emergency.submit(self._close, True)
        future.add_done_callback(lambda task: self.events.finished.emit(
            self._closed, None, str(task.exception()) if task.exception() else None))

    def closeEvent(self, event) -> None:
        self.timer.stop()
        self.stop_requested.set()
        self.emergency.submit(self._close)
        self.emergency.shutdown(wait=True)
        self.executor.shutdown(wait=True, cancel_futures=True)
        event.accept()

    def _panel(self, name, fields):
        page = Panel()
        for key, label, value in fields:
            page.field(key, label, value)
        self.tabs.addTab(page, name)
        return page

    def _action(self, page, text, action, callback=None):
        def clicked():
            values = page.values()
            self.submit(lambda dev: action(dev, values), callback)
        return page.button(text, clicked)

    def _build_device(self):
        page = self._panel("Device & diagnostics", [])
        page.button("Find devices", lambda: self.submit(lambda _: list_devices(
            mock=self.options["mock"], vid=self.options.get("vid", 0),
            pid=self.options.get("pid", 0)), require_device=False))
        self._action(page, "Refresh status", lambda dev, _: {
            "capabilities": dev.capabilities(), "protocol": dev.diagnostics()})
        self._action(page, "Resource owners", lambda dev, _: {
            index: owner for index, owner in enumerate(dev.resources()) if owner})
        page.button("Export log", self._export_log)
        page.button("Run Host regression", lambda: self.submit(
            lambda _: subprocess.run(["make", "test-host"], check=False, capture_output=True,
                                     timeout=120, text=True).stdout[-6000:],
            require_device=False))
        page.layout.addWidget(QLabel("UART1↔UART2 and SPI MOSI↔MISO use short loopback jumpers.\n"
                                     "Confirm the board pinout before wiring."))
        page.layout.addStretch()

    def _export_log(self):
        filename, _ = QFileDialog.getSaveFileName(self, "Save diagnostic log", filter="Text (*.txt)")
        if filename:
            Path(filename).write_text(self.log.toPlainText(), encoding="utf-8")

    def _build_i2c(self):
        page = self._panel("I²C", [("bus", "Bus (0 or 1)", "0"),
                                  ("hz", "Clock Hz", "10000"),
                                  ("address", "7-bit address", "0x2a"),
                                  ("messages", "Messages (hex or r:length, separated by ;)", "00; r:4"),
                                  ("reply", "Slave reply hex", "01 02 03 04")])
        self._action(page, "Configure", lambda d, v: d.i2c_configure(int(v["bus"]), int(v["hz"])))
        self._action(page, "Master", lambda d, v: d.i2c_role(int(v["bus"])))
        self._action(page, "Slave", lambda d, v: d.i2c_role(int(v["bus"]), True, int(v["address"], 0)))
        self._action(page, "Transfer", self._i2c_transfer)
        self._action(page, "Set reply", lambda d, v: d.i2c_slave_buffer(int(v["bus"]), bytes.fromhex(v["reply"])))
        self._action(page, "Read event", lambda d, v: d.i2c_slave_event(int(v["bus"])))
        self._action(page, "Release", lambda d, v: d.i2c_release(int(v["bus"])))
        page.layout.addWidget(QLabel("Internal pull-ups are weak. Start with short jumpers and low speed."))
        page.layout.addStretch()

    @staticmethod
    def _i2c_transfer(dev, values):
        messages = []
        for token in values["messages"].split(";"):
            token = token.strip()
            if token.startswith("r:"):
                messages.append(I2CMessage(read=int(token[2:])))
            elif token:
                messages.append(I2CMessage(data=bytes.fromhex(token)))
        return dev.i2c_transfer(int(values["bus"]), int(values["address"], 0), messages).hex(" ")

    def _build_uart(self):
        page = self._panel("UART", [("port", "Port (0 or 1)", "0"), ("baud", "Baud", "115200"),
                                   ("parity", "Parity (0 none, 1 even, 2 odd)", "0"),
                                   ("stops", "Stop bits", "1"), ("text", "Text or hex", "hello")])
        self._action(page, "Configure + receive", lambda d, v: d.uart_configure(
            int(v["port"]), int(v["baud"]), int(v["parity"]), int(v["stops"])))
        self._action(page, "Send text", lambda d, v: d.uart_write(int(v["port"]), v["text"].encode()))
        self._action(page, "Send hex", lambda d, v: d.uart_write(int(v["port"]), bytes.fromhex(v["text"])))
        self._action(page, "Read", lambda d, v: self._uart_read(d, int(v["port"])))
        self._action(page, "Statistics", lambda d, v: d.uart_stats(int(v["port"])))
        self._action(page, "Stop", lambda d, v: d.uart_stop(int(v["port"])))
        page.button("Send file", lambda: self._send_file(page.fields["port"].text()))
        page.layout.addStretch()

    @staticmethod
    def _uart_read(dev, port):
        data = dev.uart_read(port)
        return f"UART{port} {data.hex(' ')} | {data.decode(errors='replace')!r}"

    def _send_file(self, raw_port):
        try:
            port = int(raw_port)
        except ValueError:
            self.log.appendPlainText("UART port must be 0 or 1.")
            return
        if port not in (0, 1):
            self.log.appendPlainText("UART port must be 0 or 1.")
            return
        filename, _ = QFileDialog.getOpenFileName(self, "Send UART file")
        if filename:
            def send(dev):
                from time import sleep
                from . import BufferFull
                from time import monotonic
                sent = 0
                deadline = monotonic() + 5
                with open(filename, "rb") as file:
                    while chunk := file.read(512):
                        while True:
                            if self.stop_requested.is_set():
                                raise RuntimeError(f"UART file cancelled after {sent} bytes")
                            try:
                                sent += dev.uart_write(port, chunk)
                                break
                            except BufferFull:
                                if monotonic() >= deadline:
                                    raise TimeoutError(f"UART file deadline after {sent} bytes")
                                sleep(0.02)
                        if monotonic() >= deadline:
                            raise TimeoutError(f"UART file deadline after {sent} bytes")
                return f"UART{port} queued {sent} bytes"
            self.submit(send)

    def _build_pwm(self):
        page = self._panel("PWM", [("group", "Frequency group", "0"), ("freq", "Frequency Hz", "1000"),
                                   ("duty", "Duty ppm", "250000"), ("invert", "Invert (0 or 1)", "0"),
                                   ("ms", "Maximum duration ms", "500"), ("pulses", "Pulse count", "0")])
        self._action(page, "Configure", lambda d, v: d.pwm_configure(int(v["group"]), int(v["freq"]),
                      int(v["duty"]), bool(int(v["invert"]))))
        self._action(page, "Start bounded output", lambda d, v: d.pwm_start(int(v["group"]), int(v["ms"]), int(v["pulses"])))
        self._action(page, "Actual", lambda d, v: d.pwm_status(int(v["group"])))
        self._action(page, "Stop", lambda d, v: d.pwm_stop(int(v["group"])))
        self._action(page, "Capture PB6", lambda d, _: d.pwm_capture_start())
        self._action(page, "Measure", lambda d, _: d.pwm_capture_read())
        self._action(page, "Release input", lambda d, _: d.pwm_capture_stop())
        page.layout.addStretch()

    def _build_spi(self):
        page = self._panel("SPI", [("mode", "Mode", "0"), ("bits", "Word bits", "8"), ("cs", "Chip select", "0"),
                                   ("lsb", "LSB first (0 or 1)", "0"), ("hz", "Clock Hz", "1000000"),
                                   ("segments", "Hex segments (separated by ;)", "aa 55; 01 02")])
        self._action(page, "Transfer", lambda d, v: [piece.hex(" ") for piece in d.spi_transfer(
            [bytes.fromhex(x) for x in v["segments"].split(";")], mode=int(v["mode"]),
            bits=int(v["bits"]), cs=int(v["cs"]), hz=int(v["hz"]), lsb=bool(int(v["lsb"])))])
        page.layout.addStretch()

    def _build_adc(self):
        page = self._panel("ADC", [("channels", "Channel:sampling-code list", "10:2"),
                                   ("res", "Resolution bits", "12"), ("hz", "Aggregate samples/s", "10000"),
                                   ("count", "Samples (0 for continuous)", "1024"),
                                   ("pre", "Pre-trigger samples", "0"), ("post", "Post-trigger samples", "0"),
                                   ("watchdog", "Watchdog channel:low:high", "10:0:4095")])
        self._action(page, "Configure", lambda d, v: d.adc_configure(
            [tuple(map(int, part.split(":"))) for part in v["channels"].split(",")], int(v["res"])))
        self._action(page, "Capture", lambda d, v: d.adc_start(
            int(v["hz"]), int(v["count"]), int(v["pre"]), int(v["post"])), self._capture_started)
        self._action(page, "Trigger", lambda d, _: d.adc_trigger())
        self._action(page, "Watchdog", lambda d, v: d.adc_watchdog(*map(int, v["watchdog"].split(":"))))
        self._action(page, "Stop", lambda d, _: d.adc_stop(), lambda _: setattr(self, "capturing", False))
        page.button("Export samples", self._export_adc)
        self.adc_status = QLabel("Idle")
        self.plot = Plot()
        page.layout.addWidget(self.adc_status)
        page.layout.addWidget(self.plot, 1)

    def _capture_started(self, _):
        self.plot.values.clear()
        self.last_block = None
        self.capturing = True

    def _poll_adc(self):
        if self.capturing and self.pending == 0:
            def collect(dev):
                blocks = []
                for _ in range(8):
                    block = dev.adc_read()
                    blocks.append(block)
                    if not block.samples or not (block.running or block.more_available):
                        break
                return blocks
            self.submit(collect, self._on_adc)

    def _on_adc(self, blocks):
        for block in blocks:
            if block.samples:
                self.last_block = block
                self.plot.add(block)
        block = blocks[-1]
        self.adc_status.setText(f"{block.rate:,} samples/s  ·  {block.dropped:,} lost  ·  "
                                f"{'Waiting for trigger' if block.awaiting_trigger else 'Capturing' if block.running else 'Stopped'}")
        self.capturing = block.running or block.more_available

    def _export_adc(self):
        if self.last_block is None:
            return
        filename, _ = QFileDialog.getSaveFileName(self, "Export ADC samples",
                                                   filter="CSV (*.csv);;Binary (*.bin);;NumPy (*.npy)")
        if filename:
            last = self.last_block
            values = tuple(self.plot.values)
            block = ADCBlock(**{**asdict(last), "samples": values,
                                "offset": last.offset + len(last.samples) - len(values)})
            self.submit(lambda _: block.save(filename), require_device=False)


def main() -> None:
    parser = ArgumentParser(prog="usb2xxx-gui")
    connection_args(parser)
    args = parser.parse_args()
    application = QApplication(sys.argv[:1])
    window = Workbench(settings(args))
    window.show()
    raise SystemExit(application.exec())


if __name__ == "__main__":
    main()
