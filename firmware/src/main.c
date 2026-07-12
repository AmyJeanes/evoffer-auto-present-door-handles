/* EVOffer door-handle ECU - custom app skeleton.
 * Target: GD32F303CB (Cortex-M4). App region @ 0x08005000; bootloader untouched.
 *
 * v6 - DECISIVE MINIMAL BISECTION TEST.
 *
 * v4 (no clock init, fast blink) and v5 (factory clock init, slow grouped blink) BOTH
 * end up "solid red". The bootloader is proven to hand off to the app (factory app boots
 * with the card in), so our app IS getting control - it dies before toggling the LED.
 *
 * v6 removes every variable to answer one question: "can our code run and blink the LED
 * at all, at whatever clock the bootloader leaves?" So:
 *   - explicit stack-pointer load (don't trust the bootloader's SP hand-off);
 *   - NO system_init (rule out the clock spin-waits);
 *   - blink inlined into the reset handler (no main() call, minimal stack use);
 *   - slow, EVEN blink so it's unmistakable at any clock 8-120 MHz.
 *
 * Outcomes:  slow even blink -> our code runs (v5 died in system_init);
 *            fast even blink  -> a CPU fault (Fault_Handler);
 *            solid red        -> dies on entry (deeper hand-off/linker problem). */

#include <stdint.h>

#define RCU_APB2EN   (*(volatile uint32_t *)0x40021018u)
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

/* Fault -> distinctly FAST even blink (10x main), so a fault can't be read as "running". */
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
        "ldr sp, =_estack \n"   /* set OUR stack - don't trust the bootloader's SP */
        "cpsid i          \n"   /* mask IRQs the bootloader may have left enabled */
        "b   app_start    \n");
}

void app_start(void) {
    PET();
    SCB_VTOR = APP_BASE;
    led_init();
    for (;;) {                                   /* slow, even, unmistakable blink */
        GPIOB_BOP = PB13;        wait_fed(150);   /* ON  ~0.4s@120MHz .. ~5.6s@8MHz */
        GPIOB_BOP = PB13 << 16;  wait_fed(150);   /* OFF */
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
