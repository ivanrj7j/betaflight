/*
 * Host-testable implementation of ARIES v3.0 System Core & Timebase logic.
 * Mirrors src/platform/ARIES/system.c using modular mock infrastructure.
 */

#include "aries_system_testable.h"
#include "aries/mock_mmio.h"
#include "aries/mock_csr.h"
#include "aries/mock_thejas32.h"
#include <string.h>

#define THEJAS32_IRQ_TIMER0 7
#define THEJAS32_TIMER0_LOAD_VALUE 0xFFFFFFFFUL

// Map hardware structs onto mock MMIO regions
typedef struct {
    volatile uint32_t LoadCount;
    volatile uint32_t CurrentValue;
    volatile uint32_t Control;
    volatile uint32_t EOI;
    volatile uint32_t IntrStatus;
} thejas32_timer_reg_t;

typedef struct {
    volatile uint32_t RAW_INTR;
    volatile uint32_t dummy0;
    volatile uint32_t INTR_EN;
    volatile uint32_t dummy1;
    volatile uint32_t INTR_STATUS;
} thejas32_intr_ctrl_t;

#define TIMER0_DEV    (*((thejas32_timer_reg_t *)&mock_periph_mem[0xA00]))
#define INTR_CTRL_DEV (*((thejas32_intr_ctrl_t *)mock_intctrl_mem))

uint32_t SystemCoreClock = 100000000;
static uint32_t usTicks = 100;
static float usTicksInv = 0.01f;

static volatile uint32_t timer0_overflows = 0;
static thejas32_irq_handler_t interrupt_table[32];

static int panic_count = 0;
static uint32_t last_panic_mcause = 0;

void test_system_reset_state(void)
{
    timer0_overflows = 0;
    memset(interrupt_table, 0, sizeof(interrupt_table));
    panic_count = 0;
    last_panic_mcause = 0;
    SystemCoreClock = 100000000;
    cycleCounterInit();
}

int test_system_get_panic_count(void)
{
    return panic_count;
}

uint32_t test_system_get_last_panic_mcause(void)
{
    return last_panic_mcause;
}

void test_system_reset_panic(void)
{
    panic_count = 0;
    last_panic_mcause = 0;
}

// -----------------------------------------------------------------------------
// Cycle Counter & Mathematical Conversions
// -----------------------------------------------------------------------------
void cycleCounterInit(void)
{
    usTicks = SystemCoreClock / 1000000;
    usTicksInv = 1e6f / SystemCoreClock;
}

timeUs_t micros(void)
{
    return (timeUs_t)(getCycleCounter64() / 100ULL);
}

timeMs_t millis(void)
{
    return (timeMs_t)(getCycleCounter64() / 100000ULL);
}

timeUs_t microsISR(void)
{
    return micros();
}

int32_t clockCyclesToMicros(int32_t clockCycles)
{
    return clockCycles / usTicks;
}

float clockCyclesToMicrosf(int32_t clockCycles)
{
    return clockCycles * usTicksInv;
}

int32_t clockCyclesTo10thMicros(int32_t clockCycles)
{
    return 10 * clockCycles / (int32_t)usTicks;
}

int32_t clockCyclesTo100thMicros(int32_t clockCycles)
{
    return 100 * clockCycles / (int32_t)usTicks;
}

uint32_t clockMicrosToCycles(uint32_t us)
{
    return us * usTicks;
}

// -----------------------------------------------------------------------------
// Hardware Timer 0
// -----------------------------------------------------------------------------
void thejas32_timer0_isr(void)
{
    (void)TIMER0_DEV.EOI;
    timer0_overflows++;
}

void thejas32_timer_init(void)
{
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER0, thejas32_timer0_isr);
    thejas32_interrupt_enable(THEJAS32_IRQ_TIMER0);

    TIMER0_DEV.Control = 0x0;
    TIMER0_DEV.LoadCount = THEJAS32_TIMER0_LOAD_VALUE;
    TIMER0_DEV.Control = 0x03;
}

uint32_t timer0GetCurrentValue(void)
{
    return TIMER0_DEV.CurrentValue;
}

uint32_t timer0GetOverflows(void)
{
    return timer0_overflows;
}

timeUs_t timer0Micros(void)
{
    uint32_t ovf, cur;
    do {
        ovf = timer0_overflows;
        cur = TIMER0_DEV.CurrentValue;
    } while (ovf != timer0_overflows);

    if ((TIMER0_DEV.IntrStatus & 1) && (cur > (THEJAS32_TIMER0_LOAD_VALUE / 2))) {
        ovf++;
    }

    uint64_t total_cycles = ((uint64_t)ovf * 0x100000000ULL) + (THEJAS32_TIMER0_LOAD_VALUE - cur);
    return (timeUs_t)(total_cycles / 100ULL);
}

// -----------------------------------------------------------------------------
// Interrupts & Trap Handling
// -----------------------------------------------------------------------------
void handle_trap_panic(uint32_t mcause, uint32_t mepc, uint32_t *sp)
{
    (void)mepc;
    (void)sp;
    panic_count++;
    last_panic_mcause = mcause;
}

void thejas32_interrupt_register(uint8_t irq_number, thejas32_irq_handler_t handler)
{
    if (irq_number < 32) {
        interrupt_table[irq_number] = handler;
    }
}

void thejas32_interrupt_enable(uint8_t irq_number)
{
    if (irq_number < 32) {
        INTR_CTRL_DEV.INTR_EN |= (1UL << irq_number);
    }
}

void thejas32_interrupt_disable(uint8_t irq_number)
{
    if (irq_number < 32) {
        INTR_CTRL_DEV.INTR_EN &= ~(1UL << irq_number);
    }
}

uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp)
{
    if (mcause & 0x80000000UL) {
        uint32_t cause_num = mcause & 0x7FFFFFFFUL;
        if (cause_num == 11) {
            uint32_t status = INTR_CTRL_DEV.INTR_STATUS;
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
        handle_trap_panic(mcause, mepc, sp);
        return mepc + 4;
    }
}
