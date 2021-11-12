FQBN = dev:gd32:gd_generic_f30x
GCC_PREFIX = /usr
# TODO: arduino-cli --upload still doesn’t find openocd for some
# reason. Maybe the method above is wrong?
OPENOCD_PATH = $(HOME)/.local/bin
DFUUTIL_PATH = /usr/bin
OUTPUT_PATH = ./build
SU = /usr/bin/doas
SERIAL = /dev/serial/by-id/usb-ARM_DAPLink_CMSIS-DAP_31043602313837311239333437353533a5a5a5a597969908-if01

#USB_VID_PID = 0xdead:0xbeef

COMPILE_FLAGS = -b $(FQBN) --build-property "runtime.tools.xpack-arm-none-eabi-gcc-9.3.1-1.3.path=$(GCC_PREFIX)" --output-dir "$(OUTPUT_PATH)" --warnings more

ifdef USB_VID_PID
COMPILE_FLAGS += --vid-pid $(USB_VID_PID)
endif

.PHONY: all build clean openocd gdb serial foo

all: build

build:
	arduino-cli compile $(COMPILE_FLAGS) gd32usb-test.ino

clean:
	arduino-cli compile $(COMPILE_FLAGS) --clean

foo:
	arduino-cli compile $(COMPILE_FLAGS) --upload --verify gd32usb-test.ino.elf

openocd:
	$(OPENOCD_PREFIX)/bin/openocd -f interface/cmsis-dap.cfg -f target/stm32f3x.cfg

gdb: build
	$(GCC_PREFIX)/bin/arm-none-eabi-gdb -x gdbinit build/gd32usb-test.ino.elf

serial:
#	$(SU) stty -F $(SERIAL) min 100 time 2 hupcl -brkint -ignpar icrnl opost onlcr isig icanon echo 115200
#	$(SU) cat $(SERIAL)
	$(SU) screen $(SERIAL) 115200

etags:
	find . -name '*.[ch]' -o -name '*.cpp' -o -name '*.ino' -print | etags -

