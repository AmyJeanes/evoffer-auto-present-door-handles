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

## SystemInit and USART1 both work — the "USART blocker" was a red herring
Re-running the factory `SystemInit` ([clock.md](clock.md)) on the v6 startup blinks cleanly
(v5's old hang there was the missing SP load, fixed in v6), and configuring USART1 works too.
The earlier "any USART1 access freezes the app" symptom was **not** the USART — adding USART
code simply grew the image past a size threshold and tripped the bug below.

## The size-threshold fault (root cause + fix)
**Symptom:** any app image larger than **~900 bytes** faults in the first few instructions — an
imprecise data bus fault (`CFSR` BFSR bit2 IMPRECISERR + bit4 STKERR) that escalates to
HardFault. Because it fires before the app sets `VTOR`, it vectors to the **bootloader's**
HardFault handler (`0x08001098` = `b .` spin), so the LED sits solid / "tiny flash then dead".
Images ≤ ~884 bytes run fine.

**Proof it's size, not content:** a pure blink with **no USART at all**, padded to 1364 bytes,
faults identically; the same blink at 824 bytes runs. SWD-verified the flashed image is
**byte-perfect** (incl. the SP literal) and the FMC controller is clean — so it's a *runtime*
size-dependent fault, not flash corruption.

**Root cause:** the stock bootloader corrupts the **top of RAM** when it flashes/launches a
larger image. Our stack lived at `0x2000C000` (top of the 48 KB SRAM), so its first `push`
(and the exception-stacking that follows) writes into the corrupted region and the bus errors.
Both the original store *and* the stacking fault — hence IMPRECISERR **and** STKERR, both being
stack writes near the top.

**Fix:** link the stack in **mid-RAM** (`_estack = 0x20008000`, see
[../firmware/linker.ld](../firmware/linker.ld)). The same 1364-byte image then runs perfectly —
SWD confirms `CFSR = 0`, `VTOR = 0x08005000`, 72 MHz PLL, no reset loop. This unblocks arbitrary
app size (the full present-emitter included).

**Loose end:** the fixed large build blinks slower (~2 s vs 0.6 s) than the small baseline
despite identical code, confirmed 72 MHz, and no watchdog/reset loop. Benign (health is
SWD-verified); cause not yet understood.

## Open threads
- **A real console over the 2 SWD wires:** the running app's memory is *gated* — only a HALT
  unlocks it (DHCSR C_HALT; freeze the watchdog via `DBG_CTL0 |= 0x300` first). Halt-mode
  *memory* reads work at low speed with retries; *core-register* reads via DCRSR are flaky.
  See [hardware-and-debug.md](hardware-and-debug.md).
- **Next:** build the present-emitter (now unblocked by the RAM fix) and test it wirelessly in
  range of the car — see [handle-protocol.md](handle-protocol.md).
