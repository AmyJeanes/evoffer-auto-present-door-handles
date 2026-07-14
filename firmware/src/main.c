/* EVOffer door-handle ECU - custom app: PRESENT-frame emitter (Step 2 - drive the handles).
 * Target: GD32F303CB (Cortex-M4). App region @ 0x08005000; bootloader untouched.
 *
 * Streams the reverse-engineered 23-byte handle command over USART1 (PA2 -> on-board PHY6212
 * radio -> the car's paired handles). Loops an open/close cycle: ~4 s "present" (byte2 = 6,
 * LED solid) then ~4 s "idle" (byte2 = 0, LED off). Bench-test in RF range of the awake car -
 * the real handles should pop at the start of each present phase.
 * Frame + protocol: docs/handle-protocol.md.  Clock: docs/clock.md.
 *
 * Stack lives in MID-RAM (see linker.ld): the stock bootloader corrupts the TOP of RAM when it
 * launches an app image larger than ~900 bytes, so a top-of-RAM stack faults on its first push.
 * Full story: docs/bring-up-log.md ("The size-threshold fault"). */

#include <stdint.h>

#define RCU_CTL      (*(volatile uint32_t *)0x40021000u)
#define RCU_CFG0     (*(volatile uint32_t *)0x40021004u)
#define RCU_INT      (*(volatile uint32_t *)0x40021008u)
#define RCU_APB2EN   (*(volatile uint32_t *)0x40021018u)
#define RCU_APB1EN   (*(volatile uint32_t *)0x4002101Cu)
#define FMC_WS       (*(volatile uint32_t *)0x40022000u)
#define GPIOA_CTL0   (*(volatile uint32_t *)0x40010800u)
#define GPIOB_CTL1   (*(volatile uint32_t *)0x40010C04u)
#define GPIOB_BOP    (*(volatile uint32_t *)0x40010C10u)
#define FWDGT_KR     (*(volatile uint32_t *)0x40003000u)
#define SCB_VTOR     (*(volatile uint32_t *)0xE000ED08u)

#define STK_CTRL     (*(volatile uint32_t *)0xE000E010u)
#define STK_LOAD     (*(volatile uint32_t *)0xE000E014u)
#define STK_VAL      (*(volatile uint32_t *)0xE000E018u)

#define USART1_STAT  (*(volatile uint32_t *)0x40004400u)   /* TBE = bit7 */
#define USART1_DATA  (*(volatile uint32_t *)0x40004404u)
#define USART1_BAUD  (*(volatile uint32_t *)0x40004408u)
#define USART1_CTL0  (*(volatile uint32_t *)0x4000440Cu)   /* UEN b13, TEN b3 */

#define APP_BASE     0x08005000u
#define PB13         (1u << 13)
#define PET()        (FWDGT_KR = 0x0000AAAAu)

/* --- temporary bench diagnostics in free low RAM (this build has no .data/.bss). SWD-read these
 *     with peek.cfg to see what the CPU actually observes. Remove before the real field build. --- */
#define DIAG_GPIOA_CTL0  (*(volatile uint32_t *)0x20000010u)   /* GPIOA_CTL0 as the CPU reads it */
#define DIAG_USART_CTL0  (*(volatile uint32_t *)0x20000014u)   /* USART1_CTL0 as the CPU reads it */
#define DIAG_TXCOUNT     (*(volatile uint32_t *)0x20000018u)   /* frames handed to the UART so far */
#define DIAG_TBETMO      (*(volatile uint32_t *)0x2000001Cu)   /* times TBE wait timed out (radio? bus?) */

void Reset_Handler(void);
void Fault_Handler(void);

static void led_init(void) {
    RCU_APB2EN |= (1u << 3);
    GPIOB_CTL1 = (GPIOB_CTL1 & ~(0xFu << 20)) | (0x3u << 20);
}

/* Factory SystemInit, decoded in docs/clock.md: reset the tree to IRC8M, then HSE + PLL ~72MHz.
 * Watchdog fed inside every spin so a slow crystal can't reset us. */
static void system_init(void) {
    RCU_CTL  |= 0x00000001u;   /* IRC8MEN */
    RCU_CFG0 &= 0xf8ff0000u;   /* IRC8M as sysclk, clear prescalers */
    RCU_CTL  &= 0xfef6ffffu;   /* disable PLL + HXTAL */
    RCU_CTL  &= 0xfffbffffu;   /* clear HXTALBPS */
    RCU_CFG0 &= 0xff80ffffu;   /* clear PLLSEL/PREDV/PLLMF */
    RCU_INT   = 0x009f0000u;   /* clear clock interrupt flags */

    RCU_CTL |= 0x00010000u;                        /* HXTALEN */
    for (uint32_t t = 0; t < 0xffffu; t++) { PET(); if (RCU_CTL & 0x00020000u) break; }
    if (RCU_CTL & 0x00020000u) {
        FMC_WS  |= 0x00000010u;                     /* prefetch enable */
        FMC_WS   = (FMC_WS & ~0x3u) | 0x2u;         /* 2 flash wait states */
        RCU_CFG0 |= 0x00000400u;                    /* APB1 = /2 -> PCLK1 36MHz */
        RCU_CFG0 &= 0xffc0ffffu;                    /* clear PLLSEL + PLLMF[3:0] */
        RCU_CFG0 |= 0x00110000u;                    /* PLLSEL=HXTAL + PLLMF x6 */
        RCU_CTL  |= 0x01000000u;                    /* PLLEN */
        while ((RCU_CTL & 0x02000000u) == 0) PET(); /* wait PLLSTB */
        RCU_CFG0 &= 0xfffffffcu;                    /* clear SCS */
        RCU_CFG0 |= 0x00000002u;                    /* SCS = PLL */
        while ((RCU_CFG0 & 0x0000000cu) != 0x8u) PET(); /* wait SCSS = PLL */
    }
}

/* 72MHz core clock -> 1ms SysTick. Free-runs; delay_ms counts COUNTFLAG and pets the watchdog. */
static void systick_init(void) {
    STK_LOAD = 72000u - 1u;
    STK_VAL  = 0u;
    STK_CTRL = (1u << 2) | (1u << 0);   /* processor clock, no interrupt, enable */
}
static void delay_ms(uint32_t ms) {
    while (ms--) {
        while ((STK_CTRL & (1u << 16)) == 0u) { }   /* COUNTFLAG: reads clear it */
        PET();
    }
}

static void usart1_init(void) {
    RCU_APB1EN |= (1u << 17);                                /* USART1 clock */
    RCU_APB2EN |= (1u << 2) | (1u << 0);                     /* GPIOA + AFIO clock */
    (void)RCU_APB2EN;   /* readback barrier: a GPIO config write within ~2 cycles of the RCU clock
                           enable is dropped (GD32/STM32 gotcha) - PA2 landed as 0x3 not 0xB. */
    GPIOA_CTL0 = (GPIOA_CTL0 & ~(0xFu << 8)) | (0xBu << 8);  /* PA2 = AF push-pull, 50MHz */
    /* BRR for 115200 @ PCLK1 36MHz. NOT 0x1388: that ignores the USART /16 oversampling and
     * gives 7200 baud. Factory calls usart_init(115200) @0x080056d4 -> library writes 0x138. */
    USART1_BAUD = 0x0138u;
    USART1_CTL0 = (1u << 13) | (1u << 3);                    /* UEN | TEN, 8N1 by reset default */
}

static void u1_putc(uint8_t c) {
    uint32_t guard = 0;
    while (!(USART1_STAT & 0x80u)) { if (++guard > 20000u) { DIAG_TBETMO++; PET(); return; } }
    USART1_DATA = c;
}
static void u1_send(const uint8_t *p, int n) { for (int i = 0; i < n; i++) u1_putc(p[i]); }

/* 23-byte frame, verified against HandleCmd_BuildFrame @0x08007ea8 and docs/handle-protocol.md.
 * present=1 -> pop (byte2=6, request counter in byte14); present=0 -> idle (byte2=0). */
static void build_frame(uint8_t *f, int present, uint8_t counter) {
    for (int i = 0; i < 23; i++) f[i] = 0;
    f[0]  = 0x2e; f[1] = 0xC0; f[2] = present ? 0x06 : 0x00;
    f[3]  = 0xf0; f[4] = 0xc0; f[5] = 0x1a; f[6] = 0x15; f[7] = 0x14; f[8] = 0x14; f[9] = 0x07;
    f[10] = 0x01; f[11] = 0xff; f[12] = 0x00;   /* LED mode 1 + its RGB (table @0x0800aea8) */
    if (present) { f[13] = 0x01; f[14] = counter; }
    f[15] = 0x96; f[16] = 0x28; f[19] = 0x00; f[21] = 0xe8;
    uint8_t sum = 0;
    for (int i = 0; i < 22; i++) sum += f[i];
    f[22] = sum;                                /* additive checksum of bytes 0..21 */
}

/* Fault -> distinctly FAST even blink, so a fault can't be read as "running". */
void Fault_Handler(void) {
    led_init();
    for (;;) {
        GPIOB_BOP = PB13;        for (volatile uint32_t d = 0; d < 200000u; d++) { }
        GPIOB_BOP = PB13 << 16;  for (volatile uint32_t d = 0; d < 200000u; d++) { }
    }
}

extern uint32_t _estack;

__attribute__((naked)) void Reset_Handler(void) {
    __asm volatile(
        "ldr sp, =_estack \n"   /* set OUR stack (mid-RAM, see linker.ld) - not the bootloader's */
        "cpsid i          \n"   /* mask IRQs the bootloader may have left enabled */
        "b   app_start    \n");
}

void app_start(void) {
    PET();
    SCB_VTOR = APP_BASE;
    system_init();
    systick_init();
    led_init();
    usart1_init();

    DIAG_GPIOA_CTL0 = GPIOA_CTL0;   /* what the CPU actually sees post-config (expect PA2 nibble 0xB) */
    DIAG_USART_CTL0 = USART1_CTL0;
    DIAG_TXCOUNT = 0;
    DIAG_TBETMO  = 0;

    uint8_t frame[23];
    uint8_t counter = 0;
    for (;;) {
        counter++;
        build_frame(frame, 1, counter);                 /* present: LED on, ~4 s */
        GPIOB_BOP = PB13;
        for (int i = 0; i < 130; i++) { u1_send(frame, 23); DIAG_TXCOUNT++; delay_ms(30); }
        build_frame(frame, 0, counter);                 /* idle: LED off, ~4 s */
        GPIOB_BOP = PB13 << 16;
        for (int i = 0; i < 130; i++) { u1_send(frame, 23); DIAG_TXCOUNT++; delay_ms(30); }
    }
}

#define F (uint32_t)Fault_Handler
__attribute__((section(".isr_vector"), used))
const uint32_t g_vectors[] = {
    (uint32_t)&_estack,          /* 0  initial SP */
    (uint32_t)Reset_Handler,     /* 1  reset */
    F, F, F, F, F,               /* 2-6  NMI, Hard, Mem, Bus, Usage */
    0, 0, 0, 0,                  /* 7-10 reserved */
    F, F,                        /* 11 SVC, 12 DebugMon */
    0,                           /* 13 reserved */
    F, F,                        /* 14 PendSV, 15 SysTick */
    F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
    F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
    F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
    F, F, F, F, F, F, F, F,
};
#undef F
