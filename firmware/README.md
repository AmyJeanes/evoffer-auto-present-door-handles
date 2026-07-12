# firmware — custom app for the EVOffer door-handle ECU (GD32F303CB)

Step 1 of the rebuild, **DONE**: we build our own image, get it onto the device through
the stock bootloader, and watch it boot. It blinks **PB13** (visible with no extra wires).
See [../docs/bring-up-log.md](../docs/bring-up-log.md) for the (bumpy) road to a clean blink.

## Why this is safe
The stock **bootloader (0x08000000–0x08004000) is never touched.** It only erases and
flashes the 52 KB app slot at **0x08005000**, and validates *nothing* except that our
first vector word (the stack pointer) is a `0x2000xxxx` SRAM address — which it is. If our
app misbehaves, reflash the factory image and we're back:
**rollback = `../reference/CX_CAN_original.bin`**, or a full SWD restore of
`../reference/gd32_backup.bin`.

## Build
Needs `arm-none-eabi-gcc` (the copy bundled with PlatformIO works — no PATH setup needed).

**Windows (no make required):**
```
pwsh firmware\build.ps1        # -> CX_CAN.bin
```
It auto-finds the PlatformIO ARM toolchain; pass `-Toolchain "<bin dir>"` if yours is elsewhere.

**With make (Linux/macOS/MSYS):**
```
make          # -> CX_CAN.bin
```

## Flash
See [../docs/deploy-and-flashing.md](../docs/deploy-and-flashing.md). Short version: copy
`CX_CAN.bin` to the root of a FAT32 microSD, insert it, power-cycle.

## What we learned bringing this up (the non-obvious bits)
The stock bootloader hands the app a **live, messy state**, so a naive CMSIS startup dies
silently. What a working app needs here:

- **Set the stack pointer explicitly** (`ldr sp,=_estack`) as the first instruction — don't
  trust the bootloader's SP hand-off. This was the single biggest blocker.
- **Mask interrupts early** (`cpsid i`) — the bootloader leaves IRQs enabled/pending that
  will vector through our small table into garbage.
- **Feed the free watchdog** (`0xAAAA → FWDGT_KR` @0x40003000) early and often, in case the
  factory left a hardware FWDGT running.
- **The bootloader already brings up the real clock** (HSE crystal + PLL, ~72 MHz) and
  leaves it running — our app inherits it, so no clock init is needed just to run. We'll add
  our own deterministic `SystemInit` when exact USART1 baud timing matters (step 3) — the
  decoded factory recipe is in [../docs/clock.md](../docs/clock.md).
- **We fly blind:** the bootloader gates SWD, so there's no live debugger on the running
  app (yet). Bring-up is via **LED diagnostic patterns** — e.g. a *grouped* blink vs an
  *even* blink distinguishes "reached main" from "faulted", independent of clock speed.

## Notes
- LED polarity: PB13 high = on (confirmed by the visible blink).
- `-mfloat-abi=soft` avoids needing to enable the FPU for now.
