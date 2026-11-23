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
HAL_SOURCES := $(addprefix $(ARM_DEPS)/hal/Src/stm32f4xx_,hal.c hal_rcc.c hal_gpio.c hal_cortex.c hal_dma.c hal_uart.c hal_i2c.c hal_spi.c hal_adc.c hal_adc_ex.c)
TINYUSB_SOURCES := $(addprefix $(ARM_DEPS)/tinyusb/src/,tusb.c common/tusb_fifo.c \
	device/usbd.c device/usbd_control.c class/vendor/vendor_device.c \
	portable/synopsys/dwc2/dcd_dwc2.c portable/synopsys/dwc2/dwc2_common.c)
ARM_VENDOR_SOURCES := $(ARM_DEPS)/cmsis-device/Source/Templates/system_stm32f4xx.c $(HAL_SOURCES) $(TINYUSB_SOURCES)
ARM_SOURCES := firmware/src/startup.S firmware/src/main.c firmware/src/frame.c \
	firmware/src/bsp.c firmware/src/clock_policy.c firmware/src/memory.c \
	firmware/src/device.c firmware/src/app.c firmware/src/uart_service.c firmware/src/uart_stm32.c firmware/src/i2c_service.c firmware/src/i2c_stm32.c firmware/src/pwm_service.c firmware/src/pwm_stm32.c firmware/src/spi_service.c firmware/src/spi_stm32.c firmware/src/adc_service.c firmware/src/adc_stm32.c firmware/src/jobs.c firmware/src/resources.c firmware/src/usb_link.c firmware/src/usb_descriptors.c \
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

HOST_I2C_SOURCES := firmware/src/i2c_service.c firmware/src/jobs.c firmware/src/resources.c firmware/src/device.c firmware/src/frame.c host/i2c_mock.c
build/host/test-i2c: tests/test_i2c.c $(HOST_I2C_SOURCES) $(ARM_HEADERS) build/generated/dma_routes.inc Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: test-i2c
test-i2c: build/host/test-i2c
	./build/host/test-i2c

test-host: test-i2c

HOST_PWM_SOURCES := firmware/src/pwm_service.c firmware/src/resources.c firmware/src/device.c firmware/src/frame.c host/pwm_mock.c
build/host/test-pwm: tests/test_pwm.c $(HOST_PWM_SOURCES) $(ARM_HEADERS) build/generated/dma_routes.inc Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: test-pwm
test-pwm: build/host/test-pwm
	./build/host/test-pwm

test-host: test-pwm

HOST_SPI_SOURCES := firmware/src/spi_service.c firmware/src/jobs.c firmware/src/resources.c firmware/src/device.c firmware/src/frame.c host/spi_mock.c
build/host/test-spi: tests/test_spi.c $(HOST_SPI_SOURCES) $(ARM_HEADERS) build/generated/dma_routes.inc Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: test-spi
test-spi: build/host/test-spi
	./build/host/test-spi

test-host: test-spi

HOST_ADC_SOURCES := firmware/src/adc_service.c firmware/src/resources.c firmware/src/device.c firmware/src/frame.c host/adc_mock.c
build/host/test-adc: tests/test_adc.c $(HOST_ADC_SOURCES) $(ARM_HEADERS) build/generated/dma_routes.inc Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: test-adc
test-adc: build/host/test-adc
	./build/host/test-adc

test-host: test-adc

MOCK_SOURCES := $(sort $(HOST_UART_SOURCES) $(HOST_I2C_SOURCES) $(HOST_PWM_SOURCES) $(HOST_SPI_SOURCES) $(HOST_ADC_SOURCES) firmware/src/app.c)
MOCK_EXE ?= build/host/mock-device
$(MOCK_EXE): host/mock_device.c $(MOCK_SOURCES) $(ARM_HEADERS) build/generated/dma_routes.inc Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(WARNINGS) $(HOST_CFLAGS) $(filter %.c,$^) $(HOST_LDFLAGS) -o $@

.PHONY: mock-device
mock-device: $(MOCK_EXE)

.PHONY: test-sdk
test-sdk: mock-device
	PYTHONPATH=pc $(PYTHON) -m unittest discover -s tests -p 'test_sdk.py' -v

test-host: test-sdk

.PHONY: test-gui
test-gui: mock-device
	QT_QPA_PLATFORM=offscreen PYTHONPATH=pc $(PYTHON) -m unittest discover -s tests -p 'test_gui.py' -v

.PHONY: test-mcp
test-mcp: mock-device
	PYTHONPATH=pc $(PYTHON) -m unittest discover -s tests -p 'test_mcp.py' -v

ST_FLASH ?= st-flash
HIL_ARGS ?=

.PHONY: flash hil-verify verify-all
flash: debug
	$(ST_FLASH) --reset write $(DEBUG_DIR)/usb2box.bin 0x08000000

hil-verify:
	PYTHONPATH=pc $(PYTHON) tools/hil.py$(if $(strip $(HIL_ARGS)), $(HIL_ARGS))

verify-all: verify test-gui test-mcp

WIN_CC ?= x86_64-w64-mingw32-gcc
SEVEN_ZIP ?= 7z
WIN_PACKAGE ?= dist/usb2xxx-windows-x64
WIN_STAGE_ARGS ?=

.PHONY: package-windows
package-windows:
	$(MAKE) mock-device MOCK_EXE=dist/windows-x64/mock-device.exe HOST_CC=$(WIN_CC) HOST_LDFLAGS=-static
	$(PYTHON) -m pip wheel --no-build-isolation --no-deps . -w dist
	$(PYTHON) tools/stage_windows_release.py --output "$(WIN_PACKAGE)" --mock dist/windows-x64/mock-device.exe --sdk-wheel dist/usb2xxx-0.1.0-py3-none-any.whl --seven-zip "$(SEVEN_ZIP)" $(WIN_STAGE_ARGS)
