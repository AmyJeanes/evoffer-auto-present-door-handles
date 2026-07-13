/* WIP experiment - handle PRESENT-frame emitter over USART1.  DOES NOT WORK YET.
 * ----------------------------------------------------------------------------
 * This is the first attempt at Step 2 (drive the handles). The 23-byte frame and the
 * USART1 config below are faithfully reverse-engineered from the factory firmware
 * (see ../../docs/handle-protocol.md and ../../docs/clock.md), so the DATA is trustworthy.
 *
 * BLOCKER (2026-07-13): on the real board, *any* access to USART1 freezes the app (LED goes
 * "solid"). Ruled out: flash/hardware (the plain v6 blink reflashes and runs fine) and the
 * clock-enable bit (RCU_APB1EN |= 0x20000 = bit17, identical to the factory). Could not yet
 * tell a bus-stall from a caught fault (SWD halt-mode reads are unreliable on this gated
 * chip). Leading theory: the app inherits the bootloader's clock and needs the factory
 * SystemInit (which sets APB1=/2 and the full tree) run BEFORE touching USART1 - the factory
 * always does SystemInit first. Next step: retry SystemInit (docs/clock.md; v5's hang there
 * was the missing SP load, now fixed) in isolation, then add this USART1 code back.
 *
 * To try this build: copy over firmware/src/main.c and rebuild. Runs an open/close loop:
 * present ~5 s (LED on) / idle ~5 s (LED off). Test in RF range of the awake car. */

#include <stdint.h>

#define RCU_APB2EN   (*(volatile uint32_t *)0x40021018u)
#define RCU_APB1EN   (*(volatile uint32_t *)0x4002101Cu)
#define GPIOA_CTL0   (*(volatile uint32_t *)0x40010800u)
#define GPIOB_CTL1   (*(volatile uint32_t *)0x40010C04u)
#define GPIOB_BOP    (*(volatile uint32_t *)0x40010C10u)
#define FWDGT_KR     (*(volatile uint32_t *)0x40003000u)
#define SCB_VTOR     (*(volatile uint32_t *)0xE000ED08u)

#define USART1_STAT  (*(volatile uint32_t *)0x40004400u)   /* TBE = bit7 */
#define USART1_DATA  (*(volatile uint32_t *)0x40004404u)
#define USART1_BAUD  (*(volatile uint32_t *)0x40004408u)
#define USART1_CTL0  (*(volatile uint32_t *)0x4000440Cu)   /* UEN b13, TEN b3 */

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

static void usart1_init(void) {
    RCU_APB1EN |= (1u << 17);                                /* USART1 clock (matches factory) */
    (void)RCU_APB1EN;
    RCU_APB2EN |= (1u << 2) | (1u << 0);                     /* GPIOA + AFIO clock */
    GPIOA_CTL0 = (GPIOA_CTL0 & ~(0xFu << 8)) | (0xBu << 8);  /* PA2 = AF push-pull */
    USART1_BAUD = 0x1388u;                                   /* 115200 @ PCLK1 36 MHz */
    USART1_CTL0 = (1u << 13) | (1u << 3);                    /* UEN | TEN */
}

static void u1_putc(uint8_t c) {
    uint32_t guard = 0;
    while (!(USART1_STAT & 0x80u)) { if (++guard > 20000u) { PET(); return; } }
    USART1_DATA = c;
}
static void u1_send(const uint8_t *p, int n) { for (int i = 0; i < n; i++) u1_putc(p[i]); }

/* 23-byte frame, verified against HandleCmd_BuildFrame @0x08007ea8.
 * present=1 -> pop (byte2=6, counter in byte14); present=0 -> idle (byte2=0). */
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

void app_start(void) {
    PET();
    SCB_VTOR = APP_BASE;
    led_init();
    usart1_init();

    uint8_t frame[23];
    uint8_t counter = 0;
    for (;;) {
        counter++;
        build_frame(frame, 1, counter);                 /* OPEN: present ~5 s, LED on */
        GPIOB_BOP = PB13;
        for (int t = 0; t < 50; t++) { u1_send(frame, 23); wait_fed(50); }
        build_frame(frame, 0, counter);                 /* CLOSE: idle ~5 s, LED off */
        GPIOB_BOP = PB13 << 16;
        for (int t = 0; t < 50; t++) { u1_send(frame, 23); wait_fed(50); }
    }
}

void Fault_Handler(void) {
    led_init();
    for (;;) { GPIOB_BOP = PB13; wait_fed(15); GPIOB_BOP = PB13 << 16; wait_fed(15); }
}

extern uint32_t _estack;

__attribute__((naked)) void Reset_Handler(void) {
    __asm volatile(
        "ldr sp, =_estack \n"
        "cpsid i          \n"
        "b   app_start    \n");
}

#define F (uint32_t)Fault_Handler
__attribute__((section(".isr_vector"), used))
const uint32_t g_vectors[] = {
    (uint32_t)&_estack,
    (uint32_t)Reset_Handler,
    F, F, F, F, F,
    0, 0, 0, 0,
    F, F,
    0,
    F, F,
    F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
    F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
    F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
    F, F, F, F, F, F, F, F,
};
#undef F
