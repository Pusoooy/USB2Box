import asyncio
import json
from pathlib import Path
import sys
import unittest

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def outcome(result):
    if result.structuredContent is not None:
        return result.structuredContent
    assert not result.isError, result.content
    return json.loads(result.content[0].text)


class MCPTests(unittest.TestCase):
    def test_stdio_policy_and_capture(self):
        async def run():
            params = StdioServerParameters(command=sys.executable, args=[
                "-m", "usb2xxx.mcp_server", "--mock", "--allow-writes"], env={"PYTHONPATH": str(Path("pc").resolve())})
            async with stdio_client(params) as (read, write):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    tools = {tool.name: tool for tool in (await session.list_tools()).tools}
                    self.assertGreaterEqual(len(tools), 16)
                    self.assertEqual(tools["usb2xxx_pwm_start"].inputSchema[
                        "properties"]["duration_ms"]["maximum"], 1000)
                    capability = outcome(await session.call_tool("usb2xxx_get_capabilities"))
                    self.assertTrue(capability["simulated"])
                    self.assertEqual(capability["device_id"], "mock")
                    device_resource = await session.read_resource("usb2xxx://devices/mock/capabilities")
                    self.assertTrue(json.loads(device_resource.contents[0].text)["simulated"])
                    denied = outcome(await session.call_tool("usb2xxx_pwm_start", {
                        "group": 0, "duration_ms": 100, "arm": True}))
                    self.assertEqual(denied["status"], "permission_denied")
                    too_long = await session.call_tool("usb2xxx_pwm_start", {
                        "group": 0, "duration_ms": 1001, "arm": True, "confirm": True})
                    self.assertTrue(too_long.isError)
                    config = outcome(await session.call_tool("usb2xxx_pwm_configure", {
                        "group": 0, "frequency_hz": 1000, "duty_ppm": 250000, "confirm": True}))
                    self.assertEqual(config["status"], "ok")
                    started = outcome(await session.call_tool("usb2xxx_pwm_start", {
                        "group": 0, "duration_ms": 100, "arm": True, "confirm": True}))
                    self.assertTrue(started["actual_values"]["running"])
                    updated = outcome(await session.call_tool("usb2xxx_pwm_update", {
                        "group": 0, "frequency_hz": 1200, "duty_ppm": 350000, "confirm": True}))
                    self.assertEqual(updated["status"], "ok")
                    slave = outcome(await session.call_tool("usb2xxx_i2c_slave_configure", {
                        "bus_id": 1, "address": 42, "reply_hex": "aabb", "confirm": True}))
                    self.assertEqual(slave["status"], "ok")
                    transfer = outcome(await session.call_tool("usb2xxx_i2c_transfer", {
                        "bus_id": 0, "address": 42, "messages": [{"hex": "17"}, {"read": 2}],
                        "confirm": True}))
                    self.assertEqual(transfer["actual_values"]["hex"], "aabb")
                    events = outcome(await session.call_tool("usb2xxx_i2c_slave_events", {"bus_id": 1}))
                    self.assertEqual([item["kind"] for item in events["actual_values"]["events"]], ["rx", "tx"])
                    denied_recovery = outcome(await session.call_tool("usb2xxx_i2c_recover", {"bus_id": 0}))
                    self.assertEqual(denied_recovery["status"], "permission_denied")
                    capture = outcome(await session.call_tool("usb2xxx_adc_capture", {
                        "channel": 10, "samples": 32, "confirm": True}))
                    self.assertEqual(capture["actual_values"]["total"], 32)
                    self.assertIsNotNone(capture["actual_values"]["first_device_us_estimated"])
                    capture_id = capture["actual_values"]["capture_id"]
                    chunk = outcome(await session.call_tool("usb2xxx_read_capture_chunk",
                                                            {"capture_id": capture_id, "count": 8}))
                    self.assertEqual(len(chunk["actual_values"]["samples"]), 8)
                    stopped = outcome(await session.call_tool("usb2xxx_stop_all"))
                    self.assertEqual(stopped["actual_values"]["session"], "closed")

            params.args.remove("--allow-writes")
            async with stdio_client(params) as (read, write):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    result = outcome(await session.call_tool("usb2xxx_spi_transfer", {
                        "segments": ["aa"], "confirm": True}))
                    self.assertEqual(result["status"], "permission_denied")
        asyncio.run(run())


if __name__ == "__main__":
    unittest.main()
