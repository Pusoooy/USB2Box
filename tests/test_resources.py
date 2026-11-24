import csv
import itertools
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMAS = ROOT / "schemas"


def table(name):
    with (SCHEMAS / name).open(newline="") as source:
        return list(csv.DictReader(source))


class ResourceTests(unittest.TestCase):
    def test_requirement_trace_matches_confirmed_decisions(self):
        decisions = (ROOT / "docs" / "decisions.md").read_text(encoding="utf-8")
        expected = dict(re.findall(r"^\| ([A-Z]\d{2}) \| ([A-D]) \|", decisions, re.MULTILINE))
        expected.update({"GUI-01": "", "MCP-01": ""})
        trace = table("requirements_trace.csv")
        self.assertEqual(len(trace), len(expected))
        self.assertEqual({row["requirement_id"]: row["choice"] for row in trace}, expected)

    def test_package_and_gpio_conflicts(self):
        pins = table("pinmap_candidate.csv")
        groups = {}
        for row in pins:
            groups.setdefault(row["group"], set()).add(row["gpio"])
            self.assertIn(int(row["package_pin"]), range(1, 65))
            self.assertNotEqual(row["gpio"], "PB11")
        self.assertEqual(len({(row["gpio"], row["package_pin"]) for row in pins}),
                         len({row["package_pin"] for row in pins}))
        primary = ("USB", "SWD", "I2C1", "I2C2", "USART1", "USART2", "PWM1", "PWM2", "SPI1")
        for left, right in itertools.combinations(primary, 2):
            self.assertFalse(groups[left] & groups[right], (left, right))
        self.assertEqual(groups["PWM2"] & groups["USART6"], {"PC6"})
        self.assertEqual(groups["PWM1_CH2"] & groups["ADC1"], {"PA1"})
        self.assertEqual(groups["PWM2_CH2"] & groups["USART6"], {"PC7"})
        self.assertEqual(groups["ADC1"] & set.union(*(groups[key] for key in primary)),
                         {"PA0", "PA2", "PA3", "PA5", "PA6", "PA7"})

    def test_dma_pair_and_cross_service_allocations(self):
        matrix = table("dma_matrix.csv")
        self.assertEqual([(int(row["dma"]), int(row["stream"])) for row in matrix],
                         list(itertools.product((1, 2), range(8))))
        routes = {}
        for row in matrix:
            for channel in range(8):
                for request in filter(None, row[f"ch{channel}"].split("|")):
                    routes.setdefault(request, set()).add((int(row["dma"]), int(row["stream"])))
        uart = ("USART1_RX", "USART1_TX", "USART2_RX", "USART2_TX")
        i2c = ("I2C1_RX", "I2C1_TX", "I2C2_RX", "I2C2_TX")
        spi_adc = ("SPI1_RX", "SPI1_TX", "ADC1")

        def possible(requests):
            return any(len(set(choice)) == len(choice)
                       for choice in itertools.product(*(routes[key] for key in requests)))

        self.assertTrue(possible(uart))
        self.assertTrue(possible(i2c))
        self.assertTrue(possible(uart + spi_adc))
        self.assertFalse(possible(uart + i2c))
        self.assertTrue(possible(uart + tuple(key for key in i2c if key != "I2C1_TX") + spi_adc))
