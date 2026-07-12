# Deploy & flashing

## No integrity check — modified firmware IS deployable
A full trace of the bootloader (`Boot_main` @0x08003298) found **no CRC and no signature**
anywhere in the update or boot path:
- **SD update** (`SD_flash_CX_CAN_raw`): opens `CX_CAN.BIN`, erases 0xd000 @0x08005000, and
  flashes the file **raw** sector-by-sector. No header, CRC, or signature.
- **Boot gate** (`Boot_check_and_jump_to_app`): the only check is
  `(app_SP & 0x2FFE0000) == 0x20000000` — i.e. the first vector word is an SRAM address.
  Otherwise it blinks "power on no app code" forever.
- **BLE-OTA apply** (`Boot_apply_staged_OTA`): gated only by magic bytes AA / A5 / BB.

⇒ A freshly-built app (vector table @0x08005000, valid SP word) flashes and boots.

## SD-card flash (recommended — no wiring, sidesteps the debug lock)
1. `make` in `firmware/` → `CX_CAN.bin`.
2. Copy it to the root of a FAT SD card, power off, insert, power on (it flashes; PB13 blinks).
3. **Remove the card** and power-cycle — otherwise it re-flashes on every boot.

## SWD flash (fast iteration, once our unlocked fw is running)
`make flash-swd` (OpenOCD + CMSIS-DAP). The very first flash over the *stock locked*
firmware needs connect-under-reset (the debug lock); the SD path above avoids that.

## Rollback (always available)
- `reference/CX_CAN_original.bin` → SD-flash to restore the factory app.
- `reference/gd32_backup.bin` → full 128 KB SWD restore (ultimate un-brick).

The bootloader region (0x08000000–0x08004000) is never written by any app update, so it's
always intact to flash you back.
