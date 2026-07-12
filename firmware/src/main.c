/* EVOffer door-handle ECU - custom app skeleton (Step 1: prove flash + boot).
 * Target: GD32F303CB (Cortex-M4). App region @ 0x08005000; bootloader untouched.
 * Blinks PB13 (the same LED the stock bootloader toggles) so boot is provable with
 * zero extra wires. If a debugger is attached it also prints over semihosting on the
 * SWD wires you already soldered. Reads the on-board button (PC14, active-low).
 * Runs on the default 8 MHz internal clock - no PLL, deliberately minimal to bring up. */

#include <stdint.h>

/* GD32F303 == STM32F103-compatible register map */
#define RCU_APB2EN   (*(volatile uint32_t *)0x40021018u)
#define GPIOB_CTL1   (*(volatile uint32_t *)0x40010C04u)  /* pins 8..15 config */
#define GPIOB_BOP    (*(volatile uint32_t *)0x40010C10u)  /* atomic set/reset */
#define GPIOC_CTL1   (*(volatile uint32_t *)0x40011004u)
#define GPIOC_OCTL   (*(volatile uint32_t *)0x4001100Cu)
#define GPIOC_ISTAT  (*(volatile uint32_t *)0x40011008u)
#define SCB_VTOR     (*(volatile uint32_t *)0xE000ED08u)
#define DHCSR        (*(volatile uint32_t *)0xE000EDF0u)  /* bit0 = debugger present */

#define APP_BASE     0x08005000u
#define PB13         (1u << 13)
#define BTN_PC14     (1u << 14)

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

/* semihosting over the existing SWD - only safe when a debugger is attached,
 * otherwise BKPT would halt the core, so we gate it on DHCSR. */
static int sh(int op, const void *arg) {
    register int r0 asm("r0") = op;
    register const void *r1 asm("r1") = arg;
    asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_print(const char *s) { if (DHCSR & 1u) sh(0x04, s); }  /* SYS_WRITE0 */

static void delay(volatile uint32_t n) { while (n--) asm volatile("nop"); }

int main(void) {
    RCU_APB2EN |= (1u << 3) | (1u << 4);   /* clock GPIOB + GPIOC */

    GPIOB_CTL1 = (GPIOB_CTL1 & ~(0xFu << 20)) | (0x3u << 20);  /* PB13 = output PP 50MHz */
    GPIOC_CTL1 = (GPIOC_CTL1 & ~(0xFu << 24)) | (0x8u << 24);  /* PC14 = input, pulled */
    GPIOC_OCTL |= BTN_PC14;                                     /* -> pull-up (btn active-low) */

    sh_print("evoffer custom fw: hello, world\n");

    for (;;) {
        GPIOB_BOP = PB13;              /* drive PB13 high */
        delay(400000);
        GPIOB_BOP = PB13 << 16;        /* drive PB13 low  */
        delay(400000);
        if ((GPIOC_ISTAT & BTN_PC14) == 0)
            sh_print("PC14 button: pressed\n");
    }
}

/* ---------------- minimal startup ---------------- */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

void Reset_Handler(void) {
    SCB_VTOR = APP_BASE;               /* our vector table lives at 0x08005000 */
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss;) *d++ = 0;
    main();
    for (;;);
}
void Default_Handler(void) { for (;;); }

__attribute__((section(".isr_vector"), used))
const uint32_t g_vectors[] = {
    (uint32_t)&_estack,          /* 0  initial SP - bootloader checks this is 0x2000xxxx */
    (uint32_t)Reset_Handler,     /* 1  reset */
    (uint32_t)Default_Handler,   /* 2  NMI */
    (uint32_t)Default_Handler,   /* 3  HardFault */
    (uint32_t)Default_Handler,   /* 4  MemManage */
    (uint32_t)Default_Handler,   /* 5  BusFault */
    (uint32_t)Default_Handler,   /* 6  UsageFault */
    0, 0, 0, 0,                  /* 7-10 reserved */
    (uint32_t)Default_Handler,   /* 11 SVCall */
    (uint32_t)Default_Handler,   /* 12 DebugMon */
    0,                           /* 13 reserved */
    (uint32_t)Default_Handler,   /* 14 PendSV */
    (uint32_t)Default_Handler,   /* 15 SysTick */
};
