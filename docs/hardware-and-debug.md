# Hardware rig, SWD dump & on-chip debug

Everything needed to read/write/inspect the GD32F303 over SWD, and the hard-won gotchas.
The chip **firmware-gates debug**: a plain attach can't touch memory while the app runs
(see [Debug gate](#the-debug-gate) below), so dumping needs *connect-under-reset* and live
inspection needs a *halt*.

## Hardware rig
- **MCU:** GD32F303CBT6 (Cortex-M4, 128 KB flash @0x08000000, 48 KB SRAM). Board silkscreen
  `TSL_WXBS01_V2.0`. DPIDR = `0x2ba01477`, CPUID = `0x410FC241`.
- **Probe:** Raspberry Pi Debug Probe (CMSIS-DAP, USB VID:PID `0x2e8a:0x000c`). Use its
  **"D" (SWD) port** (3-pin: SWCLK/GND/SWDIO), *not* the "U" UART port.
- **Board power:** separate 3.3 V rail (a Freenove supply). **Common all grounds** (probe
  GND + supply GND + board GND) or reads glitch.
- **SWD header:** the **left vertical 6-pin header (silkscreen "GND")**, NOT the top-right
  5-hole header (that's the FR8010HA BLE chip — a dead end). Soldered pin map:
  - hole1 = **GND**, hole2 = **3V3**, hole3 = **SWDIO**, hole4 = **SWCLK**,
    hole5 = **STANDBY/sleep (NOT reset!)**, hole6 = nothing.
  - Probe "D" cable: yellow=SWDIO→h3, orange=SWCLK→h4, black=GND→h1.
- **True NRST:** the reset-cap pad next to the `103` (10 kΩ) resistor beside the GD32,
  wired to a **momentary pushbutton to GND**. ⚠️ Use the resistor's **NRST-side pad** (the
  one that reboots the chip when grounded); the other side is the 3V3 rail (grounding it
  dead-shorts power). NRST is not on any header. Holding true NRST low keeps the debug bus
  alive; the sleep pin (h5) freezes the bus and needs a power-cycle to recover.

## Debug UART
- **USART0, PA9 (TX) / PA10 (RX), 115200 8N1.** The bootloader prints its debug here. Not
  yet wired on the bench; needs a 2-wire tap (PA9 + GND to a serial adapter / the probe's
  "U" UART port) to get a live console.

## The debug gate
The bootloader gates the debug memory path. Confirmed behaviour while the **app is running**:
- DPIDR reads (`0x2ba01477`); DP is powered (CTRL/STAT top nibble `0xf` = CDBG/CSYS
  PWRUPACK); AP0 is a valid AHB-AP (IDR `0x24770011`, ROM table `0xe00ff000`).
- **PPB/debug registers ARE writable** (DHCSR `0xE000EDF0`, DBG_CTL0 `0xE0042004`).
- **But flash/SRAM reads & writes FAIL** — the memory path is gated while running.
- Only a **reset-halt** (dumping) or a **halt** (live inspect) opens memory.

## Dumping flash (connect-under-reset — the reliable method)
Because memory is gated while running, catch the core at the reset vector *before* firmware
re-locks debug. The debug domain isn't reset by NRST, so the vector-catch arm survives the
release:
1. Connect while **NRST is held** (button pressed): `mem_ap` target, `adapter speed 300`.
2. Arm halt-on-reset: `mww 0xE000EDF0 0xA05F0001` (DHCSR = DBGKEY|C_DEBUGEN) then
   `mww 0xE000EDFC 0x01000001` (DEMCR = TRCENA|VC_CORERESET). (A "FAIL" reading DEMCR back
   is cosmetic.)
3. **Release NRST** → core halts at instruction 0 (poll DHCSR for S_HALT `0x20000`). No
   timing race — it stays frozen; release whenever.
4. `mww 0xE000EDF0 0xA05F0003` (keep halted), then **dump in 64 × 2 KB chunks, retrying each
   up to 120× at `adapter speed 50`**, re-halting before each. `dump_image` whole-flash is
   all-or-nothing — one glitched read aborts 64 KB, so **chunk with per-chunk retry**.

### Dump gotchas (all bit us)
- **Flaky SWD = silent bad data.** A completed dump is NOT proof of a good dump: the first
  attempt reported success but had ~25 KB of silent `0x00`. **Solder SWDIO/SWCLK/GND**; a
  bad *GND* is the classic cause of random mid-read failures.
- **Verify:** (a) erased flash must read `0xFF`, never `0x00` (a `0x00` block = a missed
  read); (b) two independent dumps must be **byte-identical (sha256)**. The verified image:
  sha256 `970934547eb7c927ca6a87379626321da03f1cc569e8022a4d8d853e9d4a4e15`, 131072 B.
- **Bouncy NRST release fails the catch** — use a clean button, not a hand-held wire.

## Live inspection while running (halt-mode, no NRST)
We *can* debug over just the 2 SWD wires without the NRST button — by halting the running
core. The debug registers stay writable even when memory is gated, so:
1. Power AP0 (a `mem_ap` target's examine does this), set CSW `0x23000002` (32-bit, no
   auto-increment).
2. **Freeze the watchdog on halt** first — else FWDGT resets the chip ~1 s in:
   raw write `DBG_CTL0 (0xE0042004) |= 0x300` (FWDGT+WWDGT hold).
3. **Halt:** raw write `DHCSR (0xE000EDF0) = 0xA05F0003` (DBGKEY|C_HALT|C_DEBUGEN). S_HALT
   (bit 17) sets → **memory now reads** (confirmed by reading our real flashed vectors
   `0x2000c000`, `0x080051bd`, `0x08005151`).
4. **Resume:** raw write `DHCSR = 0xA05F0001`.

⚠️ **OpenOCD's own memory layer is incompatible with this gate.** `read_memory` and the
`cortex_m` target's `arp_examine` (it reads CPUID at `0xE000ED00`) FAIL even after a
confirmed halt — only **raw `dap apreg` DRW transfers** work. DRW reads are posted
(pipelined): per word, write TAR then read DRW **twice** and use the 2nd. A failed
`cortex_m` examine leaves a **DP sticky error** that blocks further raw ops until the ABORT
register is cleared (`dap dpreg 0 0x1e`). So a clean tool needs **hand-rolled raw-AP Tcl**,
not OpenOCD built-ins — see `tools/openocd/`. This is *halt/inspect/resume*, not a
free-running RTT console (that would need running-state memory access, which is gated).

## Flashing
- **Preferred: SD card, no wiring** — see [deploy-and-flashing.md](deploy-and-flashing.md).
- **SWD write:** possible via connect-under-reset + the `stm32f1x` flash driver (GD32F303 is
  STM32F1-compatible; FMC unlock keys `0x45670123` / `0xCDEF89AB`, `fmc_unlock`@0x08000c50).
  The SD path is preferred because it needs no wiring and sidesteps the gate.

## ⚠️ Safety
This drives a car-door latch. Keep `reference/gd32_backup.bin` (full image) and
`reference/CX_CAN_original.bin` (SD rollback) as the un-brick path; the bootloader region
(0x08000000–0x08004000) is never written by any app update. Verify interior egress after
any flash. Stage flashes; don't auto-flash.
