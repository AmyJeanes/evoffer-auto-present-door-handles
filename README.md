# EVOffer Auto-Present Door Handles — reverse engineering & custom firmware

Reverse engineering and clean-room rebuild of the firmware for the **EVOffer RGB
Mark 3.0 "1 Wireless Connected ECU"** auto-present door-handle controller
(Tesla Model 3/Y), built around a **GD32F303CB** (ARM Cortex-M4).

Two goals:
1. Fix the **random-present-while-parked** bug — the handles pop by themselves when
   the car is parked, traced to an ungated CAN lock-state trigger.
2. Extract the system's behaviour into clean, maintainable source we can build with a
   standard ARM toolchain — and eventually run on our own open hardware.

## Status
- ✅ Full firmware dumped over SWD (128 KB, verified byte-exact) and reverse-engineered.
- ✅ Bug root-caused; deploy path proven — **no CRC/signature, so it's SD-card flashable**.
- ✅ **Step 1 done** (tag `v0.1-blink`): custom app builds, SD-flashes, boots and blinks —
  full build→flash→boot→our-code loop proven. See [bring-up-log.md](docs/bring-up-log.md).
- ✅ **Size-threshold fault root-caused + fixed**: the bootloader corrupts the top of RAM when
  launching an app > ~900 bytes, faulting a top-of-RAM stack. Fix: link the stack in mid-RAM
  (`_estack = 0x20008000`). This unblocks arbitrarily-sized apps. See
  [bring-up-log.md](docs/bring-up-log.md) ("The size-threshold fault").
- ✅ **Step 2 emitter built + transmit chain bench-verified**: streams the 23-byte present frame
  over USART1. Two silent-failure traps caught on the bench by SWD — wrong baud (`0x1388` = 7200,
  not 115200; correct `0x138`) and a GPIO-config write dropped in the RCU clock-enable sync window
  (PA2 stuck as GPIO not alt-function; fixed with a readback barrier). SWD confirms PA2 = AF, line
  idles high, frames streaming at ~33 Hz. See [bring-up-log.md](docs/bring-up-log.md) ("Step 2").
- 🚧 **Unproven — next:** field test in RF range of the awake car. **No handle has physically
  moved yet** — the frame *semantics* (`byte2`/`byte14`) and the actual pop are unverified until
  the in-vehicle test; only the transmit chain is proven (see [handle-protocol.md](docs/handle-protocol.md)).

## Layout
| Path | What |
|---|---|
| [`firmware/`](firmware/) | our custom app — builds a flashable `CX_CAN.bin` for 0x08005000 |
| [`docs/`](docs/) | the reverse-engineering reference |
| [`tools/`](tools/) | OpenOCD configs for SWD dump / on-chip debug |
| `reference/` | factory firmware: full dump + carved app (rollback / RE source of truth) — **git-ignored** |

## Docs
- [firmware-map.md](docs/firmware-map.md) — memory layout, bootloader/app split, key addresses
- [handle-protocol.md](docs/handle-protocol.md) — the ECU→handle command frame (**the thing to reproduce**) + how to test it wirelessly
- [features.md](docs/features.md) — CAN IDs, features, on-board I/O, RGB colours
- [bug-analysis.md](docs/bug-analysis.md) — the parked-present bug + fix options
- [clock.md](docs/clock.md) — the factory clock (HSE+PLL ~72 MHz) recipe, decoded
- [deploy-and-flashing.md](docs/deploy-and-flashing.md) — how updates work, flashing, rollback
- [factory-firmware.md](docs/factory-firmware.md) — the stock update packages, their encryption, and why we don't need them
- [hardware-and-debug.md](docs/hardware-and-debug.md) — SWD rig, dump procedure, the debug gate & halt-mode debug
- [peripheral-map.md](docs/peripheral-map.md) — GD32F303 register reference (bases, FMC, SPI-NOR, CAN0 RX, FatFs)
- [bring-up-log.md](docs/bring-up-log.md) — the v1→v6 road to a working custom app

## Working from a fresh clone (e.g. laptop)
Everything needed is in the repo **except the factory binaries** (`reference/*.bin`), which
are git-ignored (EVOffer proprietary). Copy those over from your own local store to enable
rollback / SWD restore. Build with `pwsh firmware\build.ps1` (finds PlatformIO's arm-gcc,
no PATH setup) or `make` in `firmware/`. Flash via SD card (see deploy doc).

## Hardware
- MCU: **GD32F303CBT6** — Cortex-M4, 128 KB flash @0x08000000, 48 KB SRAM. Board silkscreen `TSL_WXBS01_V2.0`.
- CAN: NXP **TJA1042** (Tesla bus). Radios: **FR8010HA** (phone BLE) + **PHY6212** (handle link), each with a GD25Q16 SPI flash holding its own firmware.

## Scope & rollback
This ECU only **pops the door handles out** (auto-present) and drives their LED effects — the
lock, latch, and door-open mechanisms are stock Tesla and untouched by this firmware. Worst case
from a bad flash is a non-working handle presenter or an unresponsive ECU, not a door that won't
open. The stock bootloader (0x08000000–0x08004000) is never modified and is the recovery path:
keep `reference/CX_CAN_original.bin` (SD rollback) and the full `reference/gd32_backup.bin` SWD
image to restore. The factory binaries here are EVOffer's proprietary firmware, kept for personal
reverse-engineering and rollback — not for redistribution.
