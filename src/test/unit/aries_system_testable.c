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
thejas32_irq_handler_t interrupt_table[32];

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
    uint64_t cycles = getCycleCounter64();
    uint32_t hi = (uint32_t)(cycles >> 32);
    uint32_t lo = (uint32_t)cycles;

    uint32_t r_hi = hi % 100U;
    uint32_t add = r_hi * 96U;
    uint32_t lo_sum = lo + add;
    uint32_t carry = (lo_sum < lo) ? 1U : 0U;
    uint32_t q = (carry * 42949672U) + ((carry * 96U + lo_sum) / 100U);
    return (r_hi * 42949672U) + q;
}

timeMs_t millis(void)
{
    uint64_t cycles = getCycleCounter64();
    uint32_t hi = (uint32_t)(cycles >> 32);
    uint32_t lo = (uint32_t)cycles;

    uint32_t r_hi = hi % 100000U;
    uint64_t add = (uint64_t)r_hi * 67296ULL;
    uint64_t rem = add + lo;
    uint32_t carry = (uint32_t)(rem >> 32);
    uint32_t lo_sum = (uint32_t)rem;
    uint32_t q_rem = (carry * 42949U) + (uint32_t)((carry * 67296U + lo_sum) / 100000U);
    return (r_hi * 42949U) + q_rem;
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

    uint32_t elapsed = THEJAS32_TIMER0_LOAD_VALUE - cur;
    uint32_t r_ovf = ovf % 100U;
    uint32_t add = r_ovf * 96U;
    uint32_t lo_sum = elapsed + add;
    uint32_t carry = (lo_sum < elapsed) ? 1U : 0U;
    uint32_t q = (carry * 42949672U) + ((carry * 96U + lo_sum) / 100U);
    return (r_ovf * 42949672U) + q;
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

void test_enable_irq(void)
{
    mock_mie |= 0x800;
    mock_mstatus |= 8;
}

void test_disable_irq(void)
{
    mock_mstatus &= ~8;
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

// -----------------------------------------------------------------------------
// Bare-Metal Memory Routines
// -----------------------------------------------------------------------------
void *thejas32_memset(void *dest, int c, size_t n)
{
    uint8_t *p = (uint8_t *)dest;
    uint8_t val = (uint8_t)c;
    while (n--) {
        *p++ = val;
    }
    return dest;
}

void *thejas32_memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *thejas32_memmove(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

void test_early_uart_putc_timeout(char c, uint32_t mock_lsr_val, uint32_t *cycles_spun)
{
    (void)c;
    uint32_t timeout = 50000;
    uint32_t count = 0;
    while (!(mock_lsr_val & 0x20) && --timeout) {
        count++;
    }
    if (cycles_spun) {
        *cycles_spun = count;
    }
}

void test_early_uart_print_hex(uint32_t val, char *out_str)
{
    for (int i = 7; i >= 0; i--) {
        uint8_t n = (val >> (i * 4)) & 0xF;
        out_str[7 - i] = n < 10 ? ('0' + n) : ('A' + n - 10);
    }
    out_str[8] = '\0';
}
