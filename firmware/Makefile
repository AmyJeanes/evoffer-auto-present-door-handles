CROSS   = arm-none-eabi
CFLAGS  = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -O2 -Wall -ffreestanding -fno-common
LDFLAGS = -T linker.ld -nostdlib -nostartfiles -Wl,--gc-sections

all: CX_CAN.bin

firmware.elf: src/main.c linker.ld
	$(CROSS)-gcc $(CFLAGS) $(LDFLAGS) src/main.c -o $@

CX_CAN.bin: firmware.elf
	$(CROSS)-objcopy -O binary $< $@
	@echo "built CX_CAN.bin ($$(stat -c%s CX_CAN.bin) bytes)"

# First flash of a locked stock unit: use the SD card (see README) - no debug fight.
# After our (unlocked) fw is running, SWD is open for fast iteration:
flash-swd: firmware.elf
	openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/stm32f1x.cfg \
	  -c "program firmware.elf verify reset exit"

clean:
	rm -f firmware.elf CX_CAN.bin
