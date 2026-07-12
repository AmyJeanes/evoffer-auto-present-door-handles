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
- 🚧 Custom firmware: step-1 skeleton (blink + boot proof) under [`firmware/`](firmware/).

## Layout
| Path | What |
|---|---|
| [`firmware/`](firmware/) | our custom app — builds a flashable `CX_CAN.bin` for 0x08005000 |
| [`docs/`](docs/) | the reverse-engineering reference |
| `reference/` | factory firmware: full dump + carved app (rollback / RE source of truth) |

## Docs
- [firmware-map.md](docs/firmware-map.md) — memory layout, bootloader/app split, key addresses
- [handle-protocol.md](docs/handle-protocol.md) — the ECU→handle command frame (**the thing to reproduce**)
- [features.md](docs/features.md) — CAN IDs, features, on-board I/O, RGB colours
- [bug-analysis.md](docs/bug-analysis.md) — the parked-present bug + fix options
- [deploy-and-flashing.md](docs/deploy-and-flashing.md) — how updates work, flashing, rollback

## Hardware
- MCU: **GD32F303CBT6** — Cortex-M4, 128 KB flash @0x08000000, 48 KB SRAM. Board silkscreen `TSL_WXBS01_V2.0`.
- CAN: NXP **TJA1042** (Tesla bus). Radios: **FR8010HA** (phone BLE) + **PHY6212** (handle link), each with a GD25Q16 SPI flash holding its own firmware.

## ⚠️ Safety
This drives a **car-door latch.** The stock bootloader (0x08000000–0x08004000) is never
modified and is the un-brick path. Always keep `reference/CX_CAN_original.bin` (and the
full `reference/gd32_backup.bin` SWD image) as rollback, and verify interior emergency
egress after any flash. The factory binaries here are EVOffer's proprietary firmware,
kept for personal reverse-engineering and rollback — not for redistribution.
