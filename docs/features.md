# Features & I/O

## CAN IDs decoded (CAN_decode_car_tsl @ 0x08005924)
Frame struct: +0 = std ID (word), +2 = format (0 std / 4 ext), +0xa = DLC, +0xb = data[0]…+0x12 = data[7].

| ID | Meaning | Effect |
|---|---|---|
| 0x118 | gear (P/R/N/D) | sets g_gear_is_drive (0 = Park) |
| 0x122 / 0x123 | left/right indicator bytes | |
| 0x229 | **Park button** | if gear == Park → present (PC15 pulse) |
| 0x257 | UI level | clamped LED level |
| 0x339 | **lock / unlock** | on any change → present-request counter++ (feature #3; also the bug) |
| 0x3c2 | movement / odometer delta | sets a mode (0/3/4/5) |
| 0x3f5 | turn signals | left/right on/off + LED animation |
| 0x7fe | packs 3 bytes into a value | |

## Features
1. **BLE phone pop** — app `'U' 0x01` frame → schedules handle task slot 2. (`'U' 0x02` = OTA.) The
   phone-facing BLE (FR8010HA) advertises as `EVOHandle_RGB`, pairing password `666666` (Mark 3) / `123456` (Mark 2).
2. **Park-while-parked pop** — CAN 0x229 with gear already Park → PC15 pulse.
3. **Lock/unlock pop** — CAN 0x339 lock-change → present-request counter. *(Also the parked-present bug — see [bug-analysis.md](bug-analysis.md).)*
4. **RGB gesture** — steering-wheel KEY_INDEX up/down cycles mode 1–10; left-held-5s = manual present/retract.
5. **Turn-signal effect** — CAN 0x3f5 → handle LED turn animation.

## Present / retract triggers (product spec)
From EVOffer's user guide — the product-level behaviour the RE above implements.

| Source | Present | Retract |
|---|---|---|
| Touch screen | unlock edge | lock edge |
| Gear stalk | press **P twice** (while in P) | shift **out of P** |
| Handle | press the handle tip | close door (hall sensor) |
| Key fob / phone key | in range | — |
| Timer | — | **20 s** after a present |

Only one auto-retract timer is documented (**20 s / 20000 ms**), and retract keys off **gear ≠ P**,
not road speed — there is **no speed threshold** in the spec.

## RGB colour table (@0x0800aea8, indexed by LED mode)
| mode | colour | mode | colour |
|---|---|---|---|
| 0 | off | 5 | red |
| 1 | blue | 6 | purple |
| 2 | teal | 7 | white |
| 3 | green | 8,9 | animations |
| 4 | yellow | 10 | custom |

> The GitBook lists 9 user-facing effects in gesture-cycle order: Multicolor Gradient, RGB Cycle,
> Red, Grass Green, Green, Blue, White, Cyan, Purple. Those are the marketing names — the mode
> indices above are what the firmware actually stores and are the source of truth.

## On-board I/O
| Pin | Function |
|---|---|
| **PC14** | on-board multi-function button (active-low, 1 ms debounce ticks): **3–8 s hold** = toggle handle lights on/off; **10 s hold** = toggle the turn-signal-flash effect. Both saved to flash. |
| **PA8** | "Bluetooth proximity" digital input from the radio (approach / disconnect, 300 ms debounce) → frame byte 13 bit 3 (the passive walk-up line) |
| **PC15** | present **output** pulse (park-button path) |
| **PB13** | status LED (the bootloader blinks it) |
