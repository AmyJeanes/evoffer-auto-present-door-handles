# ECU → handle command protocol

The single interface to reproduce if you want to drive the handles. **No encryption,
no rolling code, no pairing key** — a plaintext frame with an additive checksum.

## Physical layer
- The GD32 does **not** do RF itself. It streams bytes over **USART1** (0x40004400) to a
  radio module (very likely the **PHY6212**), which relays over the air to the four handle
  modules. The over-the-air protocol lives in the radio-chip firmware, not the GD32.
- TX is DMA-driven: `HandleCmd_TX_start_DMA` (0x08006614) loads DMA0 **channel 6**
  (= USART1_TX on GD32F303) with the byte count and enables it. Frame buffer @ RAM 0x20000a1a.
- The phone-app link is a **separate** BLE radio (FR8010HA) — unrelated to the handle link.

## The frame (23 bytes, built by HandleCmd_BuildFrame @0x08007ea8)
| # | Value | Meaning |
|---|---|---|
| 0 | `0x2e` | header |
| 1 | flags | bit4 = turn-left, bit5 = turn-right, bit6 + bit7 always set |
| 2 | `0x06` / `0x00` | **present command — 6 = present, 0 = idle** |
| 3–9 | `f0 c0 1a 15 14 14 07` | fixed LED curve / timing |
| 10 | 1–10 | LED mode |
| 11,12 | | RGB high bytes (from the colour table) |
| 13 | bits | lock_state / direction: bit0/1 = lock state, bit2 = drive, bit3 = BT-proximity |
| 14 | counter | **present-request counter — changes on each pop request** |
| 15,16 | `96 28` | fixed |
| 17,18 | | turn-signal fade timers |
| 19 | | RGB low byte |
| 20 | | park-press counter |
| 21 | `0xe8` | trailer |
| 22 | sum | **8-bit additive checksum of bytes 0..21** |

The build argument (0 / 1) selects animation-phase variants; bytes 13 and 14 are only
filled when the arg ≠ 0. The likely direct "open" trigger is **byte 2 = 6**,
event-sequenced by **byte 14**.

> ⚠️ Confirm the exact present-vs-retract encoding with a **live USART1-TX capture**
> (capture a known park-button present and a retract). That's the ground-truth recipe.

## Reproducing open/close on your own hardware
Wire any MCU's UART to a PHY6212 module, replicate USART1's power-on init handshake and
baud (capture it from power-on to see if it's one-way or handshaked), then emit this frame
with byte 2 / byte 14 set. The handles obey — there's nothing cryptographic to defeat.
