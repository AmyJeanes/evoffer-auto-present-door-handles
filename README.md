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
- 🚧 Next: emit the handle **present frame** and test it wirelessly against the car
  (the ECU reaches its paired handles over RF — see [handle-protocol.md](docs/handle-protocol.md)).

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
- [hardware-and-debug.md](docs/hardware-and-debug.md) — SWD rig, dump procedure, the debug gate & halt-mode debug
- [bring-up-log.md](docs/bring-up-log.md) — the v1→v6 road to a working custom app

## Working from a fresh clone (e.g. laptop)
Everything needed is in the repo **except the factory binaries** (`reference/*.bin`), which
are git-ignored (EVOffer proprietary). Copy those over from your own local store to enable
rollback / SWD restore. Build with `pwsh firmware\build.ps1` (finds PlatformIO's arm-gcc,
no PATH setup) or `make` in `firmware/`. Flash via SD card (see deploy doc).

## Hardware
- MCU: **GD32F303CBT6** — Cortex-M4, 128 KB flash @0x08000000, 48 KB SRAM. Board silkscreen `TSL_WXBS01_V2.0`.
- CAN: NXP **TJA1042** (Tesla bus). Radios: **FR8010HA** (phone BLE) + **PHY6212** (handle link), each with a GD25Q16 SPI flash holding its own firmware.

## ⚠️ Safety
This drives a **car-door latch.** The stock bootloader (0x08000000–0x08004000) is never
modified and is the un-brick path. Always keep `reference/CX_CAN_original.bin` (and the
full `reference/gd32_backup.bin` SWD image) as rollback, and verify interior emergency
egress after any flash. The factory binaries here are EVOffer's proprietary firmware,
kept for personal reverse-engineering and rollback — not for redistribution.
