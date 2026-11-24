"""Check STM32F401RC startup, memory bounds and compiled USB descriptors."""
import argparse
from pathlib import Path
import struct
import subprocess
from uuid import UUID


def verify_usb(image, device, configuration, bos, winusb, vid, pid):
    def require(condition, item):
        if not condition:
            raise ValueError(f"{image}: invalid USB {item}")

    # One USB 2.1 device with EP0 and iManufacturer, iProduct, iSerialNumber.
    require(device == struct.pack("<BBHBBBBHHHBBBB", 18, 1, 0x0210, 0, 0, 0, 64,
                                  vid, pid, 0x0100, 1, 2, 3, 1), "device descriptor or identity")
    require(configuration[:9] == struct.pack("<BBH5B", 9, 2, len(configuration), 2, 1, 0,
                                              0x80, 50), "configuration header")
    offset = 9
    endpoints = ((0x01, 0x81), (0x82,))
    for interface, addresses in enumerate(endpoints):
        require(configuration[offset:offset + 9] ==
                bytes((9, 4, interface, 0, len(addresses), 0xff, 0, 0, 0)),
                f"vendor interface {interface}")
        offset += 9
        for address in addresses:
            require(configuration[offset:offset + 7] ==
                    struct.pack("<BBBBHB", 7, 5, address, 2, 64, 0),
                    f"bulk endpoint 0x{address:02x}")
            offset += 7
    require(offset == len(configuration), "configuration length")

    windows_version = 0x06030000
    platform = UUID("d8dd60df-4589-4cc7-9cd2-659d9e648a9f").bytes_le
    require(bos == struct.pack("<BBHB4B", 5, 15, 33, 1, 28, 16, 5, 0) +
            platform + struct.pack("<IHBB", windows_version, len(winusb), 0x20, 0),
            "BOS Microsoft platform capability")
    require(winusb[:10] == struct.pack("<HHIH", 10, 0, windows_version, len(winusb)),
            "Microsoft descriptor set header")
    # Microsoft config subset 0 describes the first USB configuration.
    require(winusb[10:18] == struct.pack("<HHBBH", 8, 1, 0, 0, len(winusb) - 10),
            "Microsoft configuration subset")

    name = "DeviceInterfaceGUIDs\0".encode("utf-16-le")
    property_size = 10 + len(name) + 80
    function_size = 8 + 20 + property_size
    guids = set()
    offset = 18
    for interface in range(len(endpoints)):
        function = winusb[offset:offset + function_size]
        require(len(function) == function_size and
                function[:8] == struct.pack("<HHBBH", 8, 2, interface, 0, function_size),
                f"Microsoft function subset {interface}")
        require(function[8:28] == struct.pack("<HH8s8s", 20, 3, b"WINUSB", b""),
                f"WinUSB compatible ID on interface {interface}")
        field = struct.pack("<4H", property_size, 4, 7, len(name)) + name + struct.pack("<H", 80)
        require(function[28:28 + len(field)] == field, f"registry property on interface {interface}")
        guid_text = function[28 + len(field):].decode("utf-16-le")
        require(guid_text.startswith("{") and guid_text.endswith("}\0\0"),
                f"GUID termination on interface {interface}")
        guid = UUID(guid_text[1:-3])
        require(guid not in guids, "repeated device interface GUID")
        guids.add(guid)
        offset += function_size
    require(offset == len(winusb), "Microsoft descriptor set length")


def verify(nm, image, vid, pid):
    symbols = {}
    sizes = {}
    output = subprocess.check_output([nm, "-S", "--defined-only", image.with_suffix(".elf")],
                                     text=True)
    for line in output.splitlines():
        address, *size, _, name = line.split()
        symbols[name] = int(address, 16)
        if size:
            sizes[name] = int(size[0], 16)
    data = image.read_bytes()
    vectors = struct.unpack_from("<101I", data)
    expected = {
        0: 0x20010000,
        1: symbols["Reset_Handler"] | 1,
        2: symbols["NMI_Handler"] | 1,
        15: symbols["SysTick_Handler"] | 1,
    }
    interrupts = {
        16: "DMA1_Stream5", 17: "DMA1_Stream6", 18: "ADC", 29: "TIM3",
        31: "I2C1_EV", 32: "I2C1_ER", 33: "I2C2_EV", 34: "I2C2_ER",
        35: "SPI1", 37: "USART1", 38: "USART2", 50: "TIM5",
        56: "DMA2_Stream0", 58: "DMA2_Stream2", 59: "DMA2_Stream3",
        60: "DMA2_Stream4", 67: "OTG_FS", 70: "DMA2_Stream7",
    }
    expected.update({16 + slot: symbols[f"{handler}_IRQHandler"] | 1
                     for slot, handler in interrupts.items()})
    for slot, word in expected.items():
        if vectors[slot] != word:
            raise ValueError(f"{image}: vector {slot} is 0x{vectors[slot]:08x}, expected 0x{word:08x}")
    if not 0x20000000 <= symbols["__bss_end"] <= symbols["__stack_limit"] == 0x2000E000:
        raise ValueError(f"{image}: invalid SRAM or stack bounds")
    if any(name in symbols for name in ("malloc", "calloc", "realloc", "free")):
        raise ValueError(f"{image}: dynamic allocator linked into firmware")
    if image.parent.name.startswith(("debug", "release")):
        debug = image.parent.name.startswith("debug")
        if ("usb2_assert_failed" in symbols) != debug:
            raise ValueError(f"{image}: incorrect Debug assertion configuration")
    descriptors = []
    for name in ("usb2_device_descriptor", "usb2_configuration", "usb2_bos", "usb2_winusb"):
        start = symbols[name] - 0x08000000
        end = start + sizes[name]
        if not 0 <= start < end <= len(data):
            raise ValueError(f"{image}: {name} is outside the flash image")
        descriptors.append(data[start:end])
    verify_usb(image, *descriptors, vid, pid)
    print(f"{image}: IRQ vectors, memory bounds, allocator and Debug policy, "
          "USB endpoints and both WinUSB subsets verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0)
    parser.add_argument("nm")
    parser.add_argument("image", type=Path, nargs="+")
    args = parser.parse_args()
    for image in args.image:
        verify(args.nm, image, args.vid, args.pid)
