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
- **A real console over the existing 2 SWD wires:** if the running app leaves the debug
  port accessible (our clean loop doesn't gate it), OpenOCD can poll a RAM buffer the app
  writes to — an RTT-style console with no extra wiring. To be tested.
