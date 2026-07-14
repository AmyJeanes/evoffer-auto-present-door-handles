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

## Step 2 — the present-emitter, and two transport traps (bench-verified, field test pending)
With arbitrary image size unblocked, `firmware/src/main.c` became the handle **present-frame
emitter**: `system_init` (72 MHz) → SysTick 1 ms timing → USART1 on PA2 → stream the 23-byte frame
(see [handle-protocol.md](handle-protocol.md)) in a ~4 s present / ~4 s idle loop. It builds and
runs cleanly at ~1.3 KB on the mid-RAM stack. But two bugs would each have caused a **silent field
failure** — handles dead, everything otherwise "looking" fine — and both were caught on the bench
by SWD, not by watching the LED.

**Trap 1 — USART baud (the ÷16 oversampling).** `BRR = 0x1388` is *wrong*: the USART divides the
peripheral clock by **16 × USARTDIV**, so `0x1388` @ PCLK1 36 MHz = **7200 baud**, not 115200. The
dump confirms the factory calls the library `usart_init(115200)` (literal `0x1c200` at
`0x080056d4`), which computes **`BRR = 0x138`** at 36 MHz. Correct value in code: `0x138`.

**Trap 2 — a GPIO config write dropped right after the RCU clock enable.** SWD read `GPIOA_CTL0`
back with PA2's nibble = `0x3` (general-purpose output) instead of `0xB` (alternate-function), so
USART TX was never routed to the pin — it sat as a plain GPIO driven **low** (`ODR.2 = 0`; a UART
line idles *high*). Chased down cleanly:
- The debug **read path is faithful** — immutable bit-11-set bootloader words read back exactly,
  so `0x3` was real, not a read glitch.
- **Not a GPIO lock** (`GPIOA_LOCK = 0`), and a **debugger** write of `0xB` to the nibble *sticks*
  — the bit is fully writable.
- The compiled firmware *does* write `0xB00`, but the disassembly shows it lands **~1 instruction
  after** `RCU_APB2EN |= GPIOA/AFIO` — inside the peripheral clock's sync window, so the write is
  dropped. Classic GD32/STM32 "delay after RCC clock enable" gotcha. `led_init`'s GPIOB write and
  the USART register writes survive only because more cycles pass first; the factory never hits it
  because it makes function *calls* (burns cycles) between clock-enable and pin config.

**Fix:** a one-line readback barrier — `(void)RCU_APB2EN;` between the clock enable and the PA2
config write (`usart1_init` in [../firmware/src/main.c](../firmware/src/main.c)). SWD then confirms
end-to-end: PA2 nibble = `0xB`, `IDR.2 = 1` (pin idles high = live UART), `BAUD = 0x138`,
`CTL0 = 0x2008`, a firmware frame counter incrementing at ~33 Hz, zero TBE timeouts, `CFSR = 0`.
The transmit chain is now correct; the only unverified thing left is whether the frame *semantics*
(`byte2 = 6` + the `byte14` counter) actually pop the handles — which only the car can answer.

> This build carries **temporary bench diagnostics**: the firmware writes `GPIOA_CTL0` / a frame
> counter / a TBE-timeout counter to free RAM at `0x20000010+` (SWD-readable). Harmless (RAM clears
> on power-cycle); **strip before the final field build**.

## Open threads
- **A real console over the 2 SWD wires:** the running app's memory is *gated* — only a HALT
  unlocks it (DHCSR C_HALT; freeze the watchdog via `DBG_CTL0 |= 0x300` first). Halt-mode
  *memory* reads work at low speed with retries; *core-register* reads via DCRSR are flaky.
  See [hardware-and-debug.md](hardware-and-debug.md).
- **Next:** field-test the emitter in RF range of the awake car — transport is bench-verified, so
  the frame *semantics* are the only unknown (see [handle-protocol.md](handle-protocol.md)). On a
  successful pop: strip the temporary diagnostics and bank Step 2. If it doesn't pop: iterate on
  `byte2`/`byte14`/`byte13` and cadence, or capture the real USART1-TX frames for ground truth.
