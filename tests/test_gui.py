import os
import time
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6.QtWidgets import QApplication, QPushButton
from usb2xxx.gui import Workbench


class GUITests(unittest.TestCase):
    def test_mock_workbench(self):
        app = QApplication.instance() or QApplication([])
        window = Workbench({"mock": True})
        window.show()

        def wait(condition):
            end = time.monotonic() + 3
            while not condition() and time.monotonic() < end:
                app.processEvents()
                time.sleep(0.003)
            self.assertTrue(condition(), window.log.toPlainText())

        def click(tab, text):
            page = window.tabs.widget(tab)
            button = next(control for control in page.findChildren(QPushButton)
                          if control.text() == text)
            button.click()
            wait(lambda: window.pending == 0)
            return page

        try:
            self.assertEqual(window.tabs.count(), 6)
            window.connect_device()
            wait(lambda: window.state.text() == "SIMULATED · Connected")
            click(1, "Configure")
            click(2, "Configure + receive")
            click(2, "Statistics")
            click(3, "Configure")
            click(3, "Start bounded output")
            click(3, "Actual")
            click(3, "Stop")
            click(4, "Transfer")
            click(5, "Configure")
            click(5, "Capture")
            wait(lambda: len(window.plot.values) == 1024)
            self.assertIsNotNone(window.last_block)
            window.emergency_stop()
            wait(lambda: window.state.text() == "SIMULATED · Disconnected")
        finally:
            window.close()
            app.processEvents()


if __name__ == "__main__":
    unittest.main()
