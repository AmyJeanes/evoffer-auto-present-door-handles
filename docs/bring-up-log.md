# Bring-up log — getting our own code to run

Reaching a simple blinking LED on this board was surprisingly hard, because the stock
bootloader hands the application a **live, non-reset hardware state** and **gates the SWD
debug port**, so we had no debugger on the running app and no serial console — every
attempt was diagnosed purely from what the on-board LED did. This log records what failed,
what worked, and why, so the next person (or the next feature) doesn't relearn it.

## The environment the app wakes up in
- **App slot:** `0x08005000` (52 KB), flashed by the stock bootloader. Vector table must
  live at the top of this region (SP word first).
- **Clock:** the bootloader has already started **HSE crystal + PLL (~72 MHz)** and leaves
  it running. The app inherits a fast clock — it does *not* wake on the 8 MHz internal RC.
- **Interrupts:** the bootloader leaves IRQs enabled/pending.
- **Watchdog:** assume a free watchdog (FWDGT) may be running.
- **Debug:** the bootloader gates SWD. Once it has run, a normal SWD attach reads the DP
  (`DPIDR`) but **AP memory access fails**, so you cannot inspect the running app. Only
  *connect-under-reset* (halt at the bootloader's reset vector, before it gates) works, and
  that halts the bootloader — not your app. ⇒ **bring-up is LED-only.**

## What failed, and why
| Ver | Change | Result | Lesson |
|-----|--------|--------|--------|
| v1  | Minimal CMSIS startup, fast blink, semihosting `printf` | Silent (no LED) | semihosting `BKPT` with no debugger attached traps the CPU; blink too fast to see |
| v2  | Added a blind switch to the internal RC clock | Dead | Never blind-switch the clock — killed the core |
| v3  | Removed clock switch + semihosting | Silent | Naive startup still derailed by the messy hand-off state |
| v4  | `cpsid i` + feed watchdog + full 84-entry vector table | LED lit, but **solid** | Progress — but "solid" was actually a ~25 Hz blink (delays too short) at the inherited fast clock, *plus* an entry problem |
| v5  | Added a faithful copy of the factory `SystemInit` (HSE+PLL) | **Solid / hung** | Re-running clock init from the bootloader's live state is fragile; and the real bug was still the stack pointer |
| v6  | **Explicit `ldr sp,=_estack`**, no clock init, big visible delays | ✅ **Clean, slow, even blink** | Don't trust the bootloader's SP hand-off. Set SP yourself, first thing. |

## The v6 recipe (works)
A `__attribute__((naked))` reset handler whose **first** instruction sets the stack
pointer, then masks interrupts, then branches to C:

```c
__attribute__((naked)) void Reset_Handler(void) {
    __asm volatile(
        "ldr sp, =_estack \n"   // set OUR stack — don't trust the bootloader's SP
        "cpsid i          \n"   // mask IRQs the bootloader left enabled/pending
        "b   app_start    \n");
}
```

`app_start()` then feeds the watchdog, sets `VTOR = 0x08005000`, inits PB13, and blinks
with delays long enough to be visible at ~72 MHz.

## Debugging blind: LED diagnostic patterns
With no console, encode state in the blink so it's readable regardless of the actual clock:
- **grouped** (e.g. 3 quick blinks + a long gap) = "reached `main`, running normally";
- **even/continuous** = a fault (the fault handler);
- **solid** = hung/dead before the first toggle;
- **fast even** vs **slow even** = distinguish two code paths by rate.
Grouping vs evenness survives clock-speed uncertainty; absolute rate does not.

## Open threads
- **Deterministic clock:** we currently inherit the bootloader's clock. A faithful copy of
  the factory `SystemInit` (HSE+PLL) will pin the exact frequency for USART1 baud math.
- **A real console over the existing 2 SWD wires:** the running app's memory is *gated*
  while running — only a HALT unlocks it (write DHCSR C_HALT over SWD; freeze the watchdog
  via `DBG_CTL0 |= 0x300` first). So halt-mode inspection is possible but flaky, and it's
  not a free-running RTT console. See [hardware-and-debug.md](hardware-and-debug.md).

## Step 2: driving the handles — USART1 blocker (in progress)
First attempt at emitting the handle present-frame over USART1 (code:
[`../firmware/experiments/present-emitter.c`](../firmware/experiments/present-emitter.c)).
**Any access to USART1 freezes the app** (LED goes solid). What we know:
- **Not** flash/hardware (the plain v6 blink reflashes and runs fine) and **not** the clock
  enable (`RCU_APB1EN |= 0x20000` = bit 17, identical to the factory's `FUN_080078e8`).
- Couldn't tell a bus-stall from a caught fault — the SWD halt-mode reads are too unreliable
  here to read the live registers/PC.
- **Leading theory / next step:** the app inherits the bootloader's clock; the factory runs
  its full `SystemInit` (APB1 = /2, whole tree) *before* it ever touches USART1. Retry
  `SystemInit` ([clock.md](clock.md)) — v5's hang there was the missing SP load, now fixed —
  **in isolation first** (does v6+SystemInit still blink?), then add the USART1 code back.
- Blind SD-flash + LED iteration is slow and ambiguous; getting the debug UART (or a working
  SystemInit) is likely the real unlock. The frame/transport data itself is fully RE'd and
  trustworthy — see [handle-protocol.md](handle-protocol.md).
