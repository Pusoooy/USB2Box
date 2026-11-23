CROSS_COMPILE ?= arm-none-eabi-
HOST_CC ?= cc
PYTHON ?= python3
ARM_DEPS ?= .deps
HSE_HZ ?= 0
CLOCK_SUFFIX := $(if $(filter-out 0,$(HSE_HZ)),-hse$(HSE_HZ))
DEBUG_DIR := build/debug$(CLOCK_SUFFIX)
RELEASE_DIR := build/release$(CLOCK_SUFFIX)
ARM_CC = $(CROSS_COMPILE)gcc
ARM_SIZE = $(CROSS_COMPILE)size
ARM_OBJCOPY = $(CROSS_COMPILE)objcopy
ARM_NM = $(CROSS_COMPILE)nm

WARNINGS := -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -Ifirmware/include
ARM_FLAGS := -mcpu=cortex-m4 -mthumb -ffreestanding -ffunction-sections -fdata-sections
ARM_CPPFLAGS := -DSTM32F401xC -DUSER_VECT_TAB_ADDRESS -DHSE_VALUE=$(HSE_HZ)u \
	-isystem $(ARM_DEPS)/cmsis-core/CMSIS/Core/Include \
	-isystem $(ARM_DEPS)/cmsis-device/Include -isystem $(ARM_DEPS)/hal/Inc
HAL_SOURCES := $(addprefix $(ARM_DEPS)/hal/Src/stm32f4xx_,hal.c hal_rcc.c hal_gpio.c hal_cortex.c)
ARM_VENDOR_SOURCES := $(ARM_DEPS)/cmsis-device/Source/Templates/system_stm32f4xx.c $(HAL_SOURCES)
ARM_SOURCES := firmware/src/startup.S firmware/src/main.c firmware/src/frame.c \
	firmware/src/bsp.c firmware/src/clock_policy.c firmware/src/memory.c \
	$(ARM_VENDOR_SOURCES)
ARM_HEADERS := $(wildcard firmware/include/*.h firmware/src/*.h)
ARM_LINKER := firmware/stm32f401rc.ld

.DEFAULT_GOAL := debug
.PHONY: debug release test-host test-arm test-clock test-protocol test-resources fetch-deps verify verify-arm-deps offline-verify clean

debug: $(DEBUG_DIR)/usb2box.bin $(DEBUG_DIR)/usb2box.hex
release: $(RELEASE_DIR)/usb2box.bin $(RELEASE_DIR)/usb2box.hex

$(DEBUG_DIR)/usb2box.elf: OPT := -Og -g3
$(RELEASE_DIR)/usb2box.elf: OPT := -Os
$(DEBUG_DIR)/usb2box.elf $(RELEASE_DIR)/usb2box.elf: $(ARM_SOURCES) $(ARM_LINKER) $(ARM_HEADERS) Makefile tools/arm-dependencies.lock | verify-arm-deps
	mkdir -p $(@D)
	$(ARM_CC) $(CPPFLAGS) $(ARM_CPPFLAGS) $(WARNINGS) $(ARM_FLAGS) $(OPT) $(ARM_SOURCES) -nostdlib \
		-T$(ARM_LINKER) -Wl,--gc-sections,-z,noexecstack,-Map,$(@:.elf=.map) -lgcc -o $@
	$(ARM_SIZE) $@

# Verify the dependency cache before using any vendor source, including direct builds.
$(ARM_VENDOR_SOURCES): | verify-arm-deps

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

build/host/test-clock: tests/test_clock.c firmware/src/clock_policy.c firmware/src/clock_policy.h firmware/include/bsp.h Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) -Ifirmware/src $(WARNINGS) -g $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

test-clock: build/host/test-clock
	./build/host/test-clock

test-host: test-resources test-protocol test-clock

fetch-deps:
	$(PYTHON) tools/arm_dependencies.py fetch "$(ARM_DEPS)"

verify-arm-deps:
	$(PYTHON) tools/arm_dependencies.py verify "$(ARM_DEPS)"

offline-verify: verify-arm-deps
	$(ARM_CC) --version
	$(ARM_SIZE) --version
	$(ARM_OBJCOPY) --version

test-arm: debug release
	$(PYTHON) tools/verify_arm_image.py $(ARM_NM) $(DEBUG_DIR)/usb2box.bin $(RELEASE_DIR)/usb2box.bin

verify: offline-verify test-host test-arm

clean:
	rm -rf build
