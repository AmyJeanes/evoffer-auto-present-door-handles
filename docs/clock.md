# Clock setup (factory `SystemInit`, decoded)

The app does **not** run on the internal 8 MHz RC. The stock bootloader (and the factory
app's own `SystemInit`) bring up **HSE crystal + PLL ≈ 72 MHz** (12 MHz crystal × 6), and
the bootloader leaves that clock running when it hands off — so our custom app **inherits a
~72 MHz clock without doing anything**. That's why the v6 blink runs with no clock init.

We'll want to run our *own* `SystemInit` once exact peripheral timing matters (USART1 baud
to the handle radio). Below is the factory sequence, decoded from the dump register-by-
register, so it can be re-added verbatim (the exact values *are* the factory clock — don't
"tidy" them). RCU base = `0x40021000`, FMC base = `0x40022000`.

Factory chain: `App_Reset_Handler`(0x08005100) → `SystemInit` = `FUN_080082b0` (reset the
tree) → `FUN_080080f8` (HSE + PLL).

```c
/* --- reset the clock tree to a known IRC8M base (FUN_080082b0) --- */
RCU_CTL  |= 0x00000001u;   /* IRC8MEN: internal 8 MHz RC on (safe base) */
RCU_CFG0 &= 0xf8ff0000u;   /* clear SCS/SCSS + AHB/APB/ADC prescalers -> IRC8M is sysclk */
RCU_CTL  &= 0xfef6ffffu;   /* disable PLL (bit24) and HXTAL (bit16) - safe, sysclk is IRC8M */
RCU_CTL  &= 0xfffbffffu;   /* clear HXTALBPS (bit18) */
RCU_CFG0 &= 0xff80ffffu;   /* clear PLLSEL/PREDV/PLLMF region */
RCU_INT   = 0x009f0000u;   /* clear all clock interrupt flags */

/* --- bring up HSE crystal + PLL (FUN_080080f8) --- */
RCU_CTL |= 0x00010000u;                         /* HXTALEN */
for (uint32_t t=0; t<0xffffu; t++)              /* wait HXTALSTB, factory timeout */
    if (RCU_CTL & 0x00020000u) break;
if (RCU_CTL & 0x00020000u) {                    /* crystal up -> switch to PLL */
    FMC_WS  |= 0x00000010u;                      /* prefetch enable */
    FMC_WS  = (FMC_WS & ~0x3u) | 0x2u;           /* 2 flash wait states (needed >48 MHz) */
    RCU_CFG0 |= 0x00000400u;                     /* APB1 = /2 */
    RCU_CFG0 &= 0xffc0ffffu;                     /* clear PLLSEL + PLLMF[3:0] */
    RCU_CFG0 |= 0x00110000u;                     /* PLLSEL=HXTAL + PLLMF (x6, factory value) */
    RCU_CTL  |= 0x01000000u;                     /* PLLEN */
    while ((RCU_CTL & 0x02000000u) == 0) { }     /* wait PLLSTB */
    RCU_CFG0 &= 0xfffffffcu;                      /* clear SCS */
    RCU_CFG0 |= 0x00000002u;                      /* SCS = PLL */
    while ((RCU_CFG0 & 0x0000000cu) != 0x8u) { }  /* wait SCSS = PLL used */
}
/* else HXTAL failed -> stays on 8 MHz IRC8M. Core still runs; timing just slows.
   This safe fallback is why a faithful copy won't hang the way a blind switch does. */
```

**USART baud shortcut:** rather than compute the exact SYSCLK, copy the factory's USART
`BAUD` register value straight from the dump — it was computed for this same clock, so the
same divisor gives the same baud. USART0 (debug) is on APB2 = PCLK2; USART1 (handle link)
is on APB1 = PCLK1 (which this sets to /2).

> ⚠️ **The ÷16 trap.** Baud = `f_PCLK / (16 × USARTDIV)` — the register holds `USARTDIV`, *not*
> `f_PCLK / baud`. For the handle link (USART1, 115200, PCLK1 = 36 MHz): `USARTDIV = 36e6 /
> (16 × 115200) ≈ 19.5`, so **`BAUD = 0x138`**. `0x138` gives 115200; `0x1388` (forgetting the
> ×16) gives **7200** — a trap that cost us a debugging session. The factory doesn't hit it: it
> calls the library `usart_init(115200)` (literal `0x1c200` @ `0x080056d4`), which does the math.
