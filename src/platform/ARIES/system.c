/*
 * This file is part of Betaflight.
 * System timing and clock routines for ARIES v3.0 (THEJAS32 / VEGA ET1031).
 */

#include "platform.h"
#include "platform/bsp_thejas32.h"
#include "drivers/time.h"
#include "drivers/system.h"

uint32_t SystemCoreClock = 100000000;

FAST_CODE uint64_t getCycleCounter64(void)
{
    uint32_t hi, lo, hi2;
    do {
        __asm__ __volatile__("csrr %0, mcycleh" : "=r"(hi));
        __asm__ __volatile__("csrr %0, mcycle" : "=r"(lo));
        __asm__ __volatile__("csrr %0, mcycleh" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

FAST_CODE uint32_t getCycleCounter(void)
{
    uint32_t val;
    __asm__ __volatile__("csrr %0, mcycle" : "=r"(val));
    return val;
}

// -----------------------------------------------------------------------------
// Hardware Timer 0 (THEJAS32 / VEGA ET1031 Synopsys DW_apb_timers IP Block)
// Base Address: 0x10000A00, IRQ: 7
// -----------------------------------------------------------------------------
#define THEJAS32_TIMER0_LOAD_VALUE 0xFFFFFFFFUL

static volatile uint32_t timer0_overflows = 0;

void thejas32_timer0_isr(void)
{
    // Reading EOI register clears the active interrupt in the timer peripheral
    (void)THEJAS32_TIMER(0).EOI;
    timer0_overflows++;
}

void thejas32_timer_init(void)
{
    // 1. Register Timer 0 interrupt handler in the interrupt table (IRQ 7)
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER0, thejas32_timer0_isr);
    thejas32_interrupt_enable(THEJAS32_IRQ_TIMER0);

    // 2. Disable Timer 0 before loading configuration
    THEJAS32_TIMER(0).Control = 0x0;
    __asm__ __volatile__("fence");

    // 3. Set reload count to 0xFFFFFFFF
    THEJAS32_TIMER(0).LoadCount = THEJAS32_TIMER0_LOAD_VALUE;
    __asm__ __volatile__("fence");

    // 4. Enable timer in user-defined periodic reload mode with interrupt unmasked:
    //    Bit 0 = 1 (Timer enable)
    //    Bit 1 = 1 (User-defined reload mode)
    //    Bit 2 = 0 (Interrupt unmasked)
    THEJAS32_TIMER(0).Control = 0x03;
    __asm__ __volatile__("fence");
}

uint32_t timer0GetCurrentValue(void)
{
    return THEJAS32_TIMER(0).CurrentValue;
}

uint32_t timer0GetOverflows(void)
{
    return timer0_overflows;
}

FAST_CODE timeUs_t timer0Micros(void)
{
    uint32_t ovf, cur;
    do {
        ovf = timer0_overflows;
        cur = THEJAS32_TIMER(0).CurrentValue;
    } while (ovf != timer0_overflows);

    // If an interrupt is pending but ISR has not run yet (e.g. during a critical section),
    // and the counter has reloaded, account for the unserviced overflow.
    if ((THEJAS32_TIMER(0).IntrStatus & 1) && (cur > (THEJAS32_TIMER0_LOAD_VALUE / 2))) {
        ovf++;
    }

    uint64_t total_cycles = ((uint64_t)ovf * 0x100000000ULL) + (THEJAS32_TIMER0_LOAD_VALUE - cur);
    return (timeUs_t)(total_cycles / 100ULL);
}

// -----------------------------------------------------------------------------
// Monotonic Microsecond and Millisecond Timebase
// -----------------------------------------------------------------------------
FAST_CODE timeUs_t micros(void)
{
    // 64-bit cycle counter guarantees monotonic microsecond time without 42.9-second 32-bit rollover jitter
    return (timeUs_t)(getCycleCounter64() / 100ULL);
}

timeMs_t millis(void)
{
    // 49.7 day rollover matching Betaflight standard
    return (timeMs_t)(getCycleCounter64() / 100000ULL);
}

FAST_CODE timeUs_t microsISR(void)
{
    return micros();
}

void delayMicroseconds(uint32_t us)
{
    timeUs_t start = micros();
    while ((timeUs_t)(micros() - start) < us) {
        __NOP();
    }
}

void delay(uint32_t ms)
{
    while (ms--) {
        delayMicroseconds(1000);
    }
}

static uint32_t usTicks = 100;
static float usTicksInv = 0.01f;

void cycleCounterInit(void)
{
    usTicks = SystemCoreClock / 1000000;
    usTicksInv = 1e6f / SystemCoreClock;
}

FAST_CODE int32_t clockCyclesToMicros(int32_t clockCycles)
{
    return clockCycles / usTicks;
}

FAST_CODE float clockCyclesToMicrosf(int32_t clockCycles)
{
    return clockCycles * usTicksInv;
}

FAST_CODE int32_t clockCyclesTo10thMicros(int32_t clockCycles)
{
    return 10 * clockCycles / (int32_t)usTicks;
}

FAST_CODE int32_t clockCyclesTo100thMicros(int32_t clockCycles)
{
    return 100 * clockCycles / (int32_t)usTicks;
}

FAST_CODE uint32_t clockMicrosToCycles(uint32_t micros)
{
    return micros * usTicks;
}

bool isMPUSoftReset(void)
{
    return false;
}

void handle_trap_panic(uint32_t mcause, uint32_t mepc, uint32_t *sp)
{
    (void)mcause;
    (void)mepc;
    (void)sp;
    while (1);
}

// Peripheral interrupt callback table (32 entries for THEJAS32)
typedef void (*thejas32_irq_handler_t)(void);
static thejas32_irq_handler_t interrupt_table[32];

void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler)
{
    if (irq_number < 32) {
        interrupt_table[irq_number] = handler;
    }
}

void thejas32_interrupt_enable(uint8_t irq_number)
{
    if (irq_number < 32) {
        THEJAS32_INTR_CTRL.INTR_EN |= (1UL << irq_number);
    }
}

void thejas32_interrupt_disable(uint8_t irq_number)
{
    if (irq_number < 32) {
        THEJAS32_INTR_CTRL.INTR_EN &= ~(1UL << irq_number);
    }
}

/*
 * C trap handler invoked from startup.S trap_entry:
 * mcause: Bit 31 is interrupt flag (1 = Interrupt, 0 = Synchronous exception)
 * mepc: Program counter where trap occurred
 * sp: Pointer to saved registers on stack
 * Returns: Resume PC (usually mepc for interrupts, mepc + 4 for handled exceptions)
 */
uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp)
{
    // Check if this is an interrupt (MSB set) or an exception
    if (mcause & 0x80000000UL) {
        uint32_t cause_num = mcause & 0x7FFFFFFFUL;
        // Machine External Interrupt (cause 11)
        if (cause_num == 11) {
            uint32_t status = THEJAS32_INTR_CTRL.INTR_STATUS;
            for (uint8_t i = 0; i < 32; i++) {
                if ((status >> i) & 1U) {
                    if (interrupt_table[i]) {
                        interrupt_table[i]();
                    }
                }
            }
        }
        return mepc;
    } else {
        // Synchronous exception (Illegal instruction, Load/Store fault, etc.)
        handle_trap_panic(mcause, mepc, sp);
        return mepc + 4;
    }
}

void systemInit(void)
{
    cycleCounterInit();
    thejas32_timer_init();
}

void systemReset(void)
{
    while (1);
}

void systemResetToBootloader(bootloaderRequestType_e requestType)
{
    (void)requestType;
    while (1);
}

void indicateFailure(failureMode_e mode, int repeatCount)
{
    (void)mode;
    (void)repeatCount;
}

void failureMode(failureMode_e mode)
{
    (void)mode;
    while (1);
}


