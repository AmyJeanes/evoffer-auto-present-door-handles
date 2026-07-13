/* EVOffer door-handle ECU - custom app skeleton.
 * Target: GD32F303CB (Cortex-M4). App region @ 0x08005000; bootloader untouched.
 *
 * Known-good baseline: SystemInit (HSE+PLL ~72MHz) + a slow even blink on PB13.
 *
 * IMPORTANT - stack lives in MID-RAM, not the top. The stock bootloader corrupts the TOP of
 * RAM when it flashes/launches an app image larger than ~900 bytes, so a stack at 0x2000c000
 * takes an imprecise bus fault on its first push. Linking the stack at 0x20008000 dodges it and
 * lets the app be any reasonable size. Full story: docs/bring-up-log.md ("The size-threshold
 * fault"). This is the fix that unblocked everything past the trivial blink. */

#include <stdint.h>

#define RCU_CTL      (*(volatile uint32_t *)0x40021000u)
#define RCU_CFG0     (*(volatile uint32_t *)0x40021004u)
#define RCU_INT      (*(volatile uint32_t *)0x40021008u)
#define RCU_APB2EN   (*(volatile uint32_t *)0x40021018u)
#define FMC_WS       (*(volatile uint32_t *)0x40022000u)
#define GPIOB_CTL1   (*(volatile uint32_t *)0x40010C04u)
#define GPIOB_BOP    (*(volatile uint32_t *)0x40010C10u)
#define FWDGT_KR     (*(volatile uint32_t *)0x40003000u)
#define SCB_VTOR     (*(volatile uint32_t *)0xE000ED08u)

#define APP_BASE     0x08005000u
#define PB13         (1u << 13)
#define PET()        (FWDGT_KR = 0x0000AAAAu)

void Reset_Handler(void);
void Fault_Handler(void);

static void delay(volatile uint32_t n) { while (n--) __asm volatile("nop"); }
static void wait_fed(uint32_t chunks) { while (chunks--) { PET(); delay(300000); } }

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
        RCU_CFG0 |= 0x00000400u;                    /* APB1 = /2 */
        RCU_CFG0 &= 0xffc0ffffu;                    /* clear PLLSEL + PLLMF[3:0] */
        RCU_CFG0 |= 0x00110000u;                    /* PLLSEL=HXTAL + PLLMF x6 */
        RCU_CTL  |= 0x01000000u;                    /* PLLEN */
        while ((RCU_CTL & 0x02000000u) == 0) PET(); /* wait PLLSTB */
        RCU_CFG0 &= 0xfffffffcu;                    /* clear SCS */
        RCU_CFG0 |= 0x00000002u;                    /* SCS = PLL */
        while ((RCU_CFG0 & 0x0000000cu) != 0x8u) PET(); /* wait SCSS = PLL */
    }
}

/* Fault -> distinctly FAST even blink, so a fault can't be read as "running". */
void Fault_Handler(void) {
    led_init();
    for (;;) {
        GPIOB_BOP = PB13;        wait_fed(15);
        GPIOB_BOP = PB13 << 16;  wait_fed(15);
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
    led_init();
    for (;;) {                                   /* slow, even, unmistakable blink */
        GPIOB_BOP = PB13;        wait_fed(150);
        GPIOB_BOP = PB13 << 16;  wait_fed(150);
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
