# firmware — custom app for the EVOffer door-handle ECU (GD32F303CB)

Step 1 of the rebuild: prove we can build our own image, get it onto the device through
the stock bootloader, and see it boot. Blinks **PB13** (visible with no extra wires) and
prints "hello" over **semihosting on the existing SWD** when a debugger is attached. Also
reads the on-board button **PC14**.

## Why this is safe
The stock **bootloader (0x08000000–0x08004000) is never touched.** It only erases and
flashes the 52 KB app slot at **0x08005000**, and validates *nothing* except that our
first vector word (the stack pointer) is a `0x2000xxxx` SRAM address — which it is. If our
app misbehaves, reflash the factory image and we're back:
**rollback = `../reference/CX_CAN_original.bin`**, or a full SWD restore of
`../reference/gd32_backup.bin`.

## Build
Needs `arm-none-eabi-gcc` + `make`.
```
make          # -> CX_CAN.bin
```

## Flash
See [../docs/deploy-and-flashing.md](../docs/deploy-and-flashing.md). Short version: copy
`CX_CAN.bin` to an SD card, power-cycle with it inserted, then remove it.

## Notes
- Runs on the default 8 MHz internal oscillator — no clock setup yet. We'll switch to the
  real PLL clock when we need exact USART1 / radio timing (step 3).
- LED polarity is unknown, so the "on/off" phase may be inverted — it still blinks.
- `-mfloat-abi=soft` avoids needing to enable the FPU for now.
