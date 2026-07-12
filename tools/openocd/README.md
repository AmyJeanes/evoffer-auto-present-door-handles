# OpenOCD configs for the EVOffer GD32F303 board

The chip firmware-gates the debug memory path (see
[../../docs/hardware-and-debug.md](../../docs/hardware-and-debug.md)), so stock
`target ... cortex_m` + `flash`/`mdw` don't work while the app runs. These configs use the
workarounds that do.

**Probe:** Raspberry Pi Debug Probe (CMSIS-DAP). **OpenOCD:** the xPack build
(`winget install xpack-dev-tools.openocd-xpack`), e.g.
`…\xpack-openocd-0.12.0-7\bin\openocd.exe`.

Run: `openocd.exe -f <config>` (each config is self-contained — no board/target `-f` needed).

| Config | Needs NRST button? | Use |
|---|---|---|
| `read-under-reset.cfg` | **Yes** (hold, release on cue) | Reliable read/inspect/verify — catches the core at reset before debug re-locks. Extend to chunked dumping. |
| `halt-inspect.cfg` | No (2 SWD wires only) | Quick peek at a running app: freezes the watchdog, halts, raw-reads memory + PC, resumes. Experimental. |

## Why raw AP, not `mdw`/`read_memory`
OpenOCD's memory layer is incompatible with this gate — `read_memory` and `cortex_m`
examine fail even after a confirmed halt. Only raw `dap apreg` DRW transfers work. DRW reads
are posted: per word, write TAR then read DRW **twice**, use the 2nd. A failed `cortex_m`
examine sets a DP sticky error → clear ABORT (`dap dpreg 0 0x1e`) before more raw ops.
`halt-inspect.cfg` bakes all this in.

## Gotchas
- Common **all grounds**; solder SWDIO/SWCLK/GND (flaky contact = silent bad reads).
- Under-reset dumps are all-or-nothing per read → chunk with per-chunk retry; verify erased
  flash reads `0xFF` (not `0x00`) and two dumps match by sha256.
- A halt without freezing the watchdog (`DBG_CTL0 |= 0x300`) self-resets ~1 s in.
