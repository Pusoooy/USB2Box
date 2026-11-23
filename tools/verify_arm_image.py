"""Check the linked STM32F401RC startup vector and memory bounds."""
import argparse
from pathlib import Path
import struct
import subprocess


def verify(nm, image):
    symbols = {}
    output = subprocess.check_output([nm, "--defined-only", image.with_suffix(".elf")],
                                     text=True)
    for line in output.splitlines():
        address, _, name = line.split()
        symbols[name] = int(address, 16)
    vectors = struct.unpack_from("<101I", image.read_bytes())
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
    print(f"{image}: core and peripheral IRQ vectors, stack bounds and no allocator verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nm")
    parser.add_argument("image", type=Path, nargs="+")
    args = parser.parse_args()
    for image in args.image:
        verify(args.nm, image)
