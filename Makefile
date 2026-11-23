CROSS_COMPILE ?= arm-none-eabi-
HOST_CC ?= cc
PYTHON ?= python3
ARM_DEPS ?= .deps
HSE_HZ ?= 0
USB_VID ?= 0
USB_PID ?= 0
ARM_NEWLIB_INCLUDE ?= $(dir $(shell command -v $(ARM_CC)))../include/newlib
USB_SUFFIX := $(if $(filter-out 0,$(USB_VID) $(USB_PID)),-usb$(USB_VID)-$(USB_PID))
CLOCK_SUFFIX := $(if $(filter-out 0,$(HSE_HZ)),-hse$(HSE_HZ))
DEBUG_DIR := build/debug$(CLOCK_SUFFIX)$(USB_SUFFIX)
RELEASE_DIR := build/release$(CLOCK_SUFFIX)$(USB_SUFFIX)
ARM_CC = $(CROSS_COMPILE)gcc
ARM_SIZE = $(CROSS_COMPILE)size
ARM_OBJCOPY = $(CROSS_COMPILE)objcopy
ARM_NM = $(CROSS_COMPILE)nm

WARNINGS := -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -Ifirmware/include -Ibuild/generated
ARM_FLAGS := -mcpu=cortex-m4 -mthumb -ffreestanding -ffunction-sections -fdata-sections
ARM_CPPFLAGS := -DSTM32F401xC -DUSER_VECT_TAB_ADDRESS -DHSE_VALUE=$(HSE_HZ)u \
	-DUSB2BOX_VID=$(USB_VID) -DUSB2BOX_PID=$(USB_PID) \
	-isystem $(ARM_NEWLIB_INCLUDE) -isystem $(ARM_DEPS)/tinyusb/src \
	-isystem $(ARM_DEPS)/cmsis-core/CMSIS/Core/Include \
	-isystem $(ARM_DEPS)/cmsis-device/Include -isystem $(ARM_DEPS)/hal/Inc
HAL_SOURCES := $(addprefix $(ARM_DEPS)/hal/Src/stm32f4xx_,hal.c hal_rcc.c hal_gpio.c hal_cortex.c hal_dma.c hal_uart.c)
TINYUSB_SOURCES := $(addprefix $(ARM_DEPS)/tinyusb/src/,tusb.c common/tusb_fifo.c \
	device/usbd.c device/usbd_control.c class/vendor/vendor_device.c \
	portable/synopsys/dwc2/dcd_dwc2.c portable/synopsys/dwc2/dwc2_common.c)
ARM_VENDOR_SOURCES := $(ARM_DEPS)/cmsis-device/Source/Templates/system_stm32f4xx.c $(HAL_SOURCES) $(TINYUSB_SOURCES)
ARM_SOURCES := firmware/src/startup.S firmware/src/main.c firmware/src/frame.c \
	firmware/src/bsp.c firmware/src/clock_policy.c firmware/src/memory.c \
	firmware/src/device.c firmware/src/app.c firmware/src/uart_service.c firmware/src/uart_stm32.c firmware/src/jobs.c firmware/src/resources.c firmware/src/usb_link.c firmware/src/usb_descriptors.c \
	$(ARM_VENDOR_SOURCES)
ARM_HEADERS := $(wildcard firmware/include/*.h firmware/src/*.h)
ARM_LINKER := firmware/stm32f401rc.ld

.DEFAULT_GOAL := debug
.PHONY: debug release test-host test-arm test-clock test-protocol test-resources fetch-deps verify verify-arm-deps offline-verify clean

debug: $(DEBUG_DIR)/usb2box.bin $(DEBUG_DIR)/usb2box.hex
release: $(RELEASE_DIR)/usb2box.bin $(RELEASE_DIR)/usb2box.hex

$(DEBUG_DIR)/usb2box.elf: OPT := -Og -g3
$(RELEASE_DIR)/usb2box.elf: OPT := -Os
$(DEBUG_DIR)/usb2box.elf $(RELEASE_DIR)/usb2box.elf: $(ARM_SOURCES) $(ARM_LINKER) $(ARM_HEADERS) build/generated/dma_routes.inc Makefile tools/arm-dependencies.lock | verify-arm-deps
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

build/host/test-device: tests/test_device.c firmware/src/device.c firmware/src/frame.c $(ARM_HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: test-device
test-device: build/host/test-device
	./build/host/test-device

test-host: test-device

build/generated/dma_routes.inc: schemas/dma_matrix.csv tools/generate_dma.py
	mkdir -p $(@D)
	$(PYTHON) tools/generate_dma.py $< $@

build/host/test-runtime: tests/test_runtime.c firmware/src/resources.c firmware/src/jobs.c firmware/src/device.c firmware/src/frame.c $(ARM_HEADERS) build/generated/dma_routes.inc Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: test-runtime
test-runtime: build/host/test-runtime
	./build/host/test-runtime

test-host: test-runtime

HOST_UART_SOURCES := firmware/src/uart_service.c firmware/src/resources.c firmware/src/device.c firmware/src/frame.c host/uart_mock.c
build/host/test-uart: tests/test_uart.c $(HOST_UART_SOURCES) $(ARM_HEADERS) build/generated/dma_routes.inc Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: test-uart
test-uart: build/host/test-uart
	./build/host/test-uart

test-host: test-uart
