CROSS_COMPILE ?= arm-none-eabi-
HOST_CC ?= cc
PYTHON ?= python3
ARM_CC = $(CROSS_COMPILE)gcc
ARM_SIZE = $(CROSS_COMPILE)size
ARM_OBJCOPY = $(CROSS_COMPILE)objcopy

WARNINGS := -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -Ifirmware/include
ARM_FLAGS := -mcpu=cortex-m4 -mthumb -ffreestanding -ffunction-sections -fdata-sections
ARM_SOURCES := firmware/src/startup.S firmware/src/main.c firmware/src/frame.c
ARM_LINKER := firmware/stm32f401rc.ld

.DEFAULT_GOAL := debug
.PHONY: debug release test-host test-protocol test-resources verify offline-verify clean

debug: build/debug/usb2box.bin build/debug/usb2box.hex
release: build/release/usb2box.bin build/release/usb2box.hex

build/debug/usb2box.elf: OPT := -Og -g3
build/release/usb2box.elf: OPT := -Os
build/debug/usb2box.elf build/release/usb2box.elf: $(ARM_SOURCES) $(ARM_LINKER) firmware/include/frame.h Makefile
	mkdir -p $(@D)
	$(ARM_CC) $(CPPFLAGS) $(WARNINGS) $(ARM_FLAGS) $(OPT) $(ARM_SOURCES) -nostdlib \
		-T$(ARM_LINKER) -Wl,--gc-sections,-z,noexecstack,-Map,$(@:.elf=.map) -o $@
	$(ARM_SIZE) $@

%.bin: %.elf
	$(ARM_OBJCOPY) -O binary $< $@

%.hex: %.elf
	$(ARM_OBJCOPY) -O ihex $< $@

build/host/test-frame: tests/test_frame.c firmware/src/frame.c firmware/include/frame.h Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) -g $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

test-resources:
	$(PYTHON) -m unittest discover -s tests -p 'test_resources.py' -v

test-protocol: build/host/test-frame
	./build/host/test-frame

test-host: test-resources test-protocol

offline-verify:
	$(ARM_CC) --version
	$(ARM_SIZE) --version
	$(ARM_OBJCOPY) --version

verify: offline-verify test-host debug release

clean:
	rm -rf build
