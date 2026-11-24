"""Generate the F401RC candidate CSVs from pinned ST and Embassy snapshots."""
import argparse
import csv
import json
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

GROUPS = {
    "USB": [("PA11", "USB_OTG_FS_DM", 10), ("PA12", "USB_OTG_FS_DP", 10)],
    "SWD": [("PA13", "SYS_JTMS-SWDIO", 0), ("PA14", "SYS_JTCK-SWCLK", 0)],
    "I2C1": [("PB8", "I2C1_SCL", 4), ("PB9", "I2C1_SDA", 4)],
    "I2C2": [("PB10", "I2C2_SCL", 4), ("PB3", "I2C2_SDA", 9)],
    "USART1": [("PB6", "USART1_TX", 7), ("PB7", "USART1_RX", 7)],
    "USART2": [("PA2", "USART2_TX", 7), ("PA3", "USART2_RX", 7)],
    "PWM1": [("PA0", "TIM5_CH1", 2)],
    "PWM1_CH2": [("PA1", "TIM5_CH2", 2)],
    "PWM2": [("PC6", "TIM3_CH1", 2)],
    "PWM2_CH2": [("PC7", "TIM3_CH2", 2)],
    "PWM_CAPTURE": [("PB6", "TIM4_CH1", 2)],
    "SPI1": [("PA5", "SPI1_SCK", 5), ("PA6", "SPI1_MISO", 5),
             ("PA7", "SPI1_MOSI", 5), ("PB12", "GPIO", "output")],
    "ADC1": [(pin, f"ADC1_IN{channel}", "analog") for channel, pin in enumerate(
        [f"PA{i}" for i in range(8)] + ["PB0", "PB1"] + [f"PC{i}" for i in range(6)])],
    "I2C3": [("PA8", "I2C3_SCL", 4), ("PC9", "I2C3_SDA", 4)],
    "SPI2": [("PB13", "SPI2_SCK", 5), ("PB14", "SPI2_MISO", 5), ("PB15", "SPI2_MOSI", 5)],
    "SPI3": [("PC10", "SPI3_SCK", 6), ("PC11", "SPI3_MISO", 6), ("PC12", "SPI3_MOSI", 6)],
    "USART6": [("PC6", "USART6_TX", 8), ("PC7", "USART6_RX", 8)],
}


def write_csv(path, header, rows):
    with path.open("w", newline="") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def import_pins(package_path, af_path, output):
    package = ET.parse(package_path).getroot()
    if package.get("Package") != "LQFP64" or package.get("RefName") != "STM32F401R(B-C)Tx":
        raise ValueError("Expected STM32F401R(B-C)Tx LQFP64")
    pins = {node.get("Name").split("-")[0]: node for node in package.findall("{*}Pin")}
    af_pins = {node.get("Name").split("-")[0]: node for node in ET.parse(af_path).iterfind(".//{*}GPIO_Pin")}
    rows = []
    for group, candidates in GROUPS.items():
        for pin, signal, mode in candidates:
            package_pin = pins[pin]
            if not any(node.get("Name") == signal for node in package_pin.findall("{*}Signal")):
                raise ValueError(f"{pin} does not expose {signal}")
            if isinstance(mode, int):
                modes = af_pins[pin].findall(f"{{*}}PinSignal[@Name='{signal}']/{{*}}SpecificParameter/{{*}}PossibleValue")
                if not any(re.match(rf"GPIO_AF{mode}_", item.text or "") for item in modes):
                    raise ValueError(f"{pin} {signal} does not support AF{mode}")
            rows.append((group, pin, package_pin.get("Position"), signal, mode))
    write_csv(output, ("group", "gpio", "package_pin", "signal", "mode"), rows)


def import_dma(chip_path, output):
    chip = json.loads(chip_path.read_text())
    if chip["name"] != "STM32F401RC":
        raise ValueError("Expected STM32F401RC")
    routes = defaultdict(set)
    for peripheral in chip["cores"][0]["peripherals"]:
        for route in peripheral.get("dma_channels", []):
            dma, stream = re.fullmatch(r"DMA([12])_CH([0-7])", route["channel"]).groups()
            signal = route["signal"]
            name = peripheral["name"]
            if name == "SDIO" and signal != "SDIO":
                continue  # RX/TX are aliases for the same hardware request.
            request = name if signal == name else f"{name}_{signal}"
            routes[int(dma), int(stream), route["request"]].add(request)
    rows = [
        (dma, stream, *("|".join(sorted(routes.get((dma, stream, channel), ()))) for channel in range(8)))
        for dma in (1, 2) for stream in range(8)
    ]
    write_csv(output, ("dma", "stream", *(f"ch{i}" for i in range(8))), rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("package", "af", "chip", "output"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    import_pins(args.package, args.af, args.output / "pinmap_candidate.csv")
    import_dma(args.chip, args.output / "dma_matrix.csv")
