/*
 * This file is part of Betaflight.
 *
 * System timing, monotonic clocks, trap handling, and bare-metal primitives
 * for C-DAC ARIES v3.0 (THEJAS32 SoC / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Citations & Evaluations:
 *   - knowledgebase/codebase_evaluations/05_EVAL_SYSTEM_C.md
 *   - knowledgebase/components/01_SYSTEM_AND_CLOCKS.md
 *   - PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md (Microcomponent 01_D)
 *   - lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c
 */

#include "platform.h"
#include "platform/bsp_thejas32.h"
#include "platform/aries_probe.h" // [HARDWARE-DEBUG-PROBE: DBG-01]
#include "drivers/time.h"
#include "drivers/system.h"
#include "drivers/persistent.h"

uint32_t SystemCoreClock = 100000000U; // 100 MHz

/*
 * -----------------------------------------------------------------------------
 * 1. Monotonic 64-Bit Cycle Counter Reading Loop
 * -----------------------------------------------------------------------------
 * Reads 64-bit CSR mcycle and mcycleh in a lock-free loop verifying consistency.
 * Prevents 4.29-second time warp bug when lower 32 bits rollover during read.
 */
static inline void getCycleCounterWords(uint32_t *hi_out, uint32_t *lo_out)
{
#ifdef UNIT_TEST
    extern void thejas32_host_mcycle_read(uint32_t *hi_out, uint32_t *lo_out);
    thejas32_host_mcycle_read(hi_out, lo_out);
#else
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi));
        __asm__ volatile ("csrr %0, mcycle" : "=r"(lo));
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi2));
    } while (hi != hi2);
    *hi_out = hi;
    *lo_out = lo;
#endif
}

FAST_CODE uint64_t getCycleCounter64(void)
{
    uint32_t hi, lo;
    getCycleCounterWords(&hi, &lo);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

FAST_CODE uint32_t getCycleCounter(void)
{
#ifdef UNIT_TEST
    uint32_t hi, lo;
    getCycleCounterWords(&hi, &lo);
    return lo;
#else
    uint32_t val;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(val));
    return val;
#endif
}

void cycleCounterInit(void)
{
    // Hardware mcycle counts continuously upon core reset
}

/*
 * -----------------------------------------------------------------------------
 * 2. Hardware Timer 0 (Synopsys DW_apb_timers IP Block)
 * Base Address: 0x10000A00, IRQ: 7
 * -----------------------------------------------------------------------------
 */
#define THEJAS32_TIMER0_LOAD_VALUE 0xFFFFFFFFUL

static volatile uint32_t timer0_overflows = 0;

void thejas32_timer0_isr(void)
{
    // Reading EOI register clears the active interrupt in the Synopsys DW APB timer
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
    THEJAS32_MEMORY_FENCE();

    // 3. Set reload count to 0xFFFFFFFF
    THEJAS32_TIMER(0).LoadCount = THEJAS32_TIMER0_LOAD_VALUE;
    THEJAS32_MEMORY_FENCE();

    // 4. Enable timer in user-defined periodic reload mode with interrupt unmasked:
    //    Bit 0 = 1 (Timer enable)
    //    Bit 1 = 1 (User-defined reload mode)
    //    Bit 2 = 0 (Interrupt unmasked)
    THEJAS32_TIMER(0).Control = 0x03;
    THEJAS32_MEMORY_FENCE();
}

uint32_t timer0GetCurrentValue(void)
{
    return THEJAS32_TIMER(0).CurrentValue;
}

uint32_t timer0GetOverflows(void)
{
    return timer0_overflows;
}

/*
 * -----------------------------------------------------------------------------
 * 3. High-Performance Monotonic Microsecond and Millisecond Timebase
 * -----------------------------------------------------------------------------
 * Uses 32-bit modular reciprocal multiplication to compute cycles / 100
 * in < 25 cycles, completely bypassing GCC's 150-cycle __udivdi3 software division.
 */
FAST_CODE timeUs_t micros(void)
{
    uint32_t hi, lo;
    getCycleCounterWords(&hi, &lo);

    uint32_t r_hi = hi % 100U;
    uint32_t add = r_hi * 96U;
    uint32_t lo_sum = lo + add;
    uint32_t carry = (lo_sum < lo) ? 1U : 0U;
    uint32_t q = (carry * 42949672U) + ((carry * 96U + lo_sum) / 100U);
    uint32_t res = (r_hi * 42949672U) + q;
#if defined(ARIES_PROBE_DEBUG) && (ARIES_PROBE_DEBUG > 0) && !defined(UNIT_TEST)
    static volatile uint32_t lastUs = 0U;
    if (gProbeFastArmed && lastUs != 0U && res < lastUs) {
        uint32_t delta = (uint32_t)(lastUs - res);
        if (delta > 5000U && delta < 0x80000000U) {
            ariesProbeRecordTimeWarp(0U, lastUs, res);
        }
    }
    if (gProbeFastArmed) {
        lastUs = res;
    }
#endif
    ariesProbeCheckMieInline('u', __builtin_return_address(0));
    return res;
}

timeMs_t millis(void)
{
    uint32_t hi, lo;
    getCycleCounterWords(&hi, &lo);

    uint32_t r_hi = hi % 100000U;
    uint64_t add = (uint64_t)r_hi * 67296ULL;
    uint64_t rem = add + lo;
    uint32_t carry = (uint32_t)(rem >> 32);
    uint32_t lo_sum = (uint32_t)rem;
    uint32_t q_rem = (carry * 42949U) + (uint32_t)((carry * 67296U + lo_sum) / 100000U);
    uint32_t res = (r_hi * 42949U) + q_rem;
#if defined(ARIES_PROBE_DEBUG) && (ARIES_PROBE_DEBUG > 0) && !defined(UNIT_TEST)
    static volatile uint32_t lastMs = 0U;
    if (gProbeFastArmed && lastMs != 0U && res < lastMs) {
        uint32_t delta = (uint32_t)(lastMs - res);
        if (delta > 5U && delta < 0x80000000U) {
            ariesProbeRecordTimeWarp(1U, lastMs, res);
        }
    }
    if (gProbeFastArmed) {
        lastMs = res;
    }
#endif
    ariesProbeCheckMieInline('l', __builtin_return_address(0));
    return res;
}

FAST_CODE timeUs_t microsISR(void)
{
    return micros();
}

FAST_CODE uint32_t clockMicrosToCycles(uint32_t us)
{
    return us * (SystemCoreClock / 1000000U);
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
    // Finding 5: Measure elapsed monotonic time via millis() to eliminate accumulated loop drift
    timeMs_t start = millis();
    while ((timeMs_t)(millis() - start) < ms) {
        __NOP();
    }
}

/*
 * -----------------------------------------------------------------------------
 * 4. Early Polled UART0 Diagnostics (Non-blocking, minimal register access)
 * -----------------------------------------------------------------------------
 */
void thejas32_early_uart_init(void)
{
    // DLAB = 1 to set baud rate divisor
    THEJAS32_UART(0).UART_LCR = 0x83;
    THEJAS32_MEMORY_FENCE();
    THEJAS32_UART(0).UART_DR = 54;    // DLL: Divisor 54 for 115200 baud @ 100 MHz (+0.47% error)
    THEJAS32_UART(0).UART_IE = 0;     // DLM
    THEJAS32_MEMORY_FENCE();
    // Clear DLAB: 8 data bits, 1 stop bit, no parity (8N1)
    THEJAS32_UART(0).UART_LCR = 0x03;
    THEJAS32_MEMORY_FENCE();
    THEJAS32_UART(0).UART_IE = 0x00;  // Explicitly disable interrupts in early/polled mode
    THEJAS32_MEMORY_FENCE();
    THEJAS32_UART(0).UART_IIR_FCR = 0x00; // Disable hardware FIFOs matching official C-DAC VEGA SDK & BSP
    THEJAS32_MEMORY_FENCE();
}

void thejas32_early_uart_putc(char c)
{
    // Wait until Transmitter Holding Register is Empty (LSR Bit 5: THRE) with bounded timeout
    uint32_t timeout = 50000U;
    while (!(THEJAS32_UART(0).UART_LSR & 0x20) && timeout--) {
        __NOP();
    }
    THEJAS32_UART(0).UART_DR = (uint8_t)c;
    THEJAS32_MEMORY_FENCE();
}

void thejas32_early_uart_puts(const char *s)
{
    if (!s) return;
    static char prev = '\0';
    while (*s) {
        if (*s == '\n' && prev != '\r') {
            thejas32_early_uart_putc('\r');
        }
        thejas32_early_uart_putc(*s);
        prev = *s++;
    }
}

void thejas32_early_uart_print_hex(uint32_t val)
{
    char buf[9];
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = val & 0x0F;
        buf[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        val >>= 4;
    }
    buf[8] = '\0';
    thejas32_early_uart_puts(buf);
}

/*
 * -----------------------------------------------------------------------------
 * 5. Privileged Exception Trap Handler & Fast O(1) Interrupt Dispatch
 * -----------------------------------------------------------------------------
 */
__attribute__((weak)) void persistentObjectInit(void) { }

__attribute__((weak)) void ariesPwmEmergencyStop(void)
{
    THEJAS32_TIMER(1).Control = 0U;
    (void)THEJAS32_TIMER(1).EOI;
    thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);
    for (uint16_t pin = 1U; pin <= 4U; pin++) {
        *thejas32_gpio_data_ptr(pin) = 0U;
    }
    THEJAS32_MEMORY_FENCE();
}

static void handle_trap_panic(uint32_t mcause, uint32_t mepc, uint32_t *sp)
{
    ariesPwmEmergencyStop();
    thejas32_early_uart_puts("\r\n\r\n[FATAL TRAP PANIC]\r\n");
    thejas32_early_uart_puts("mcause=0x");
    thejas32_early_uart_print_hex(mcause);
    thejas32_early_uart_puts(" mepc=0x");
    thejas32_early_uart_print_hex(mepc);
    thejas32_early_uart_puts(" sp=0x");
    thejas32_early_uart_print_hex((uint32_t)(uintptr_t)sp);
    thejas32_early_uart_puts("\r\n");
    ariesProbeDumpReport("TRAP_PANIC", mcause); // [HARDWARE-DEBUG-PROBE: DBG-01]
#ifdef UNIT_TEST
    extern uint32_t thejas32_host_panic_count;
    thejas32_host_panic_count++;
#else
    while (1) {
        __WFI();
    }
#endif
}

// Peripheral interrupt registration table (32 entries for THEJAS32)
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
 * Software Count-Trailing-Zeros (__ctzsi2) Helper for RV32IM (no Bitmanip extension)
 */
__attribute__((used)) int __ctzsi2(int val)
{
    if (val == 0) return 32;
    int n = 0;
    uint32_t x = (uint32_t)val;
    if ((x & 0x0000FFFFU) == 0) { n += 16; x >>= 16; }
    if ((x & 0x000000FFU) == 0) { n += 8;  x >>= 8;  }
    if ((x & 0x0000000FU) == 0) { n += 4;  x >>= 4;  }
    if ((x & 0x00000003U) == 0) { n += 2;  x >>= 2;  }
    if ((x & 0x00000001U) == 0) { n += 1; }
    return n;
}

/*
 * C trap handler invoked from startup.S trap_entry:
 * mcause: Bit 31 is interrupt flag (1 = Interrupt, 0 = Synchronous exception)
 * mepc: Program counter where trap occurred
 * sp: Pointer to saved registers on stack
 */
uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp)
{
#if defined(ARIES_PROBE_DEBUG) && (ARIES_PROBE_DEBUG > 0) && !defined(UNIT_TEST)
    // [HARDWARE-DEBUG-PROBE: DBG-02]
    gProbeInTrap++;
    uint32_t probeMstatusIn = 0U;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(probeMstatusIn));
    // [/HARDWARE-DEBUG-PROBE: DBG-02]
#endif
    if (mcause & 0x80000000UL) {
        uint32_t cause_num = mcause & 0x7FFFFFFFUL;
        // Machine External Interrupt (cause 11)
        if (cause_num == 11) {
            // Fast O(1) interrupt dispatching using robust binary search CTZ (__ctzsi2)
            uint32_t status = THEJAS32_INTR_CTRL.INTR_STATUS;
            while (status != 0U) {
                uint32_t i = (uint32_t)__ctzsi2((int)status);
                ariesProbeRecordIrq((uint8_t)i, status); // [HARDWARE-DEBUG-PROBE: DBG-01]
                if (i < 32 && interrupt_table[i]) {
                    interrupt_table[i]();
                } else {
                    // Finding 2: Defensively mask spurious/unhandled interrupt in INTR_EN
                    // to prevent an immediate infinite re-trap upon mret.
                    if (i < 32) {
                        THEJAS32_INTR_CTRL.INTR_EN &= ~(1UL << i);
                    }
                }
                status &= ~(1UL << i);
            }
        } else {
            thejas32_early_uart_puts("\r\n[TRAP INTR] Unexpected interrupt mcause=0x");
            thejas32_early_uart_print_hex(mcause);
            thejas32_early_uart_puts("\r\n");
#ifdef UNIT_TEST
            thejas32_host_mie &= ~(1UL << cause_num);
#else
            __asm__ volatile ("csrc mie, %0" : : "r"(1UL << cause_num));
#endif
        }
#if defined(ARIES_PROBE_DEBUG) && (ARIES_PROBE_DEBUG > 0) && !defined(UNIT_TEST)
        // [HARDWARE-DEBUG-PROBE: DBG-02]
        uint32_t probeMstatusOut = 0U;
        __asm__ volatile ("csrr %0, mstatus" : "=r"(probeMstatusOut));
        ariesProbeRecordTrap(mcause, mepc, probeMstatusIn, probeMstatusOut);
        gProbeInTrap--;
        // [/HARDWARE-DEBUG-PROBE: DBG-02]
#endif
        return mepc;
    } else {
        // Synchronous exception (Illegal instruction, Load/Store fault, etc.)
        handle_trap_panic(mcause, mepc, sp);
#if defined(ARIES_PROBE_DEBUG) && (ARIES_PROBE_DEBUG > 0) && !defined(UNIT_TEST)
        gProbeInTrap--;
#endif
        return mepc + 4;
    }
}

void targetPreInit(void)
{
    // RV32IM boots with mstatus.MIE=0 and mie.MEIE=0 (unlike ARM PRIMASK=0); arm CPU IRQs before peripheral init
    __enable_irq();
    ariesProbeRunSiliconLab(); // [HARDWARE-DEBUG-PROBE: DBG-03]
#if defined(ARIES_DEV_LOGGING) && (ARIES_DEV_LOGGING > 0)
    thejas32_early_uart_puts("[ARIES v3] EEPROM & Config Initialized Successfully! (IRQ Enabled)\r\n");
#endif
}

void systemInit(void)
{
    thejas32_early_uart_init();
#if defined(ARIES_DEV_LOGGING) && (ARIES_DEV_LOGGING > 0)
    thejas32_early_uart_puts("\r\n\r\n========================================\r\n");
    thejas32_early_uart_puts("[ARIES v3] Betaflight 4.5 Booting...\r\n");
#endif
    cycleCounterInit();
    thejas32_timer_init();
    persistentObjectInit();
#if defined(ARIES_DEV_LOGGING) && (ARIES_DEV_LOGGING > 0)
    thejas32_early_uart_puts("[ARIES v3] Clocks & Timer0 Initialized\r\n");
    thejas32_early_uart_puts("========================================\r\n\r\n");
#endif
}

/*
 * Vector-jump soft reboot (Solves 05_EVAL_SYSTEM_C.md Score 3.0/10 while(1) hang)
 */
void systemReset(void)
{
    ariesPwmEmergencyStop();
    __disable_irq();

    // Finding 3: Disable all peripheral interrupt lines in THEJAS32 interrupt controller
    // so no stale interrupt fires during startup before BSS and tables are initialized.
    THEJAS32_INTR_CTRL.INTR_EN = 0U;
    THEJAS32_MEMORY_FENCE();

#ifdef UNIT_TEST
    extern uint32_t thejas32_host_reset_count;
    thejas32_host_reset_count++;
#else
    extern uint32_t _estack;
    extern void _start(void);
    (void)_estack;
    (void)_start;
    __asm__ volatile (
        "la sp, _estack\n"
        "la t0, _start\n"
        "jr t0\n"
    );
    while (1);
#endif
}

void systemResetToBootloader(bootloaderRequestType_e requestType)
{
    ariesPwmEmergencyStop();
    __disable_irq();
    THEJAS32_INTR_CTRL.INTR_EN = 0U;
    THEJAS32_MEMORY_FENCE();

    if (requestType == BOOTLOADER_REQUEST_ROM) {
        // ROM XMODEM re-entry vector (0x00010000) is unverified on silicon; halt cleanly with outputs off
#ifdef UNIT_TEST
        return;
#else
        while (1) {
            __WFI();
        }
#endif
    }

    systemReset();
}

void indicateFailure(failureMode_e mode, int repeatCount)
{
    (void)mode;
    (void)repeatCount;
}

void failureMode(failureMode_e mode)
{
    ariesPwmEmergencyStop();
    __disable_irq();
    thejas32_early_uart_puts("\r\n[FAILURE MODE] Code: 0x");
    thejas32_early_uart_print_hex((uint32_t)mode);
    thejas32_early_uart_puts("\r\n");
#ifdef UNIT_TEST
    return;
#else
    while (1) {
        __WFI();
    }
#endif
}

#ifndef UNIT_TEST
/*
 * -----------------------------------------------------------------------------
 * 6. Bare-Metal Non-RVC Memory Primitives (memset, memcpy, memmove)
 * -----------------------------------------------------------------------------
 * Overrides toolchain newlib routines to guarantee strictly zero 16-bit compressed
 * instructions on non-RVC THEJAS32 silicon (Ground Truth 1).
 */
__attribute__((used, optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dest, int c, size_t n)
{
    uint8_t *p = (uint8_t *)dest;
    uint8_t val = (uint8_t)c;

    // Align to 4-byte word boundary
    while (n && ((uintptr_t)p & 3)) {
        *p++ = val;
        n--;
        __asm__ volatile ("" : "+r"(p) : : "memory");
    }

    // 32-bit word transfer loop
    if (n >= 4) {
        uint32_t val32 = ((uint32_t)val << 24) | ((uint32_t)val << 16) | ((uint32_t)val << 8) | (uint32_t)val;
        uint32_t *p32 = (uint32_t *)p;
        size_t words = n >> 2;
        while (words--) {
            *p32++ = val32;
            __asm__ volatile ("" : "+r"(p32) : : "memory");
        }
        p = (uint8_t *)p32;
        n &= 3;
    }

    // Remaining trailing bytes
    while (n--) {
        *p++ = val;
        __asm__ volatile ("" : "+r"(p) : : "memory");
    }
    return dest;
}

__attribute__((used, optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    // If source and destination share the same 4-byte alignment
    if (((uintptr_t)d & 3) == ((uintptr_t)s & 3)) {
        while (n && ((uintptr_t)d & 3)) {
            *d++ = *s++;
            n--;
            __asm__ volatile ("" : "+r"(d), "+r"(s) : : "memory");
        }
        if (n >= 4) {
            uint32_t *d32 = (uint32_t *)d;
            const uint32_t *s32 = (const uint32_t *)s;
            size_t words = n >> 2;
            while (words--) {
                *d32++ = *s32++;
                __asm__ volatile ("" : "+r"(d32), "+r"(s32) : : "memory");
            }
            d = (uint8_t *)d32;
            s = (const uint8_t *)s32;
            n &= 3;
        }
    }

    while (n--) {
        *d++ = *s++;
        __asm__ volatile ("" : "+r"(d), "+r"(s) : : "memory");
    }
    return dest;
}

__attribute__((used, optimize("no-tree-loop-distribute-patterns")))
void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        if (((uintptr_t)d & 3) == ((uintptr_t)s & 3)) {
            while (n && ((uintptr_t)d & 3)) {
                *d++ = *s++;
                n--;
                __asm__ volatile ("" : "+r"(d), "+r"(s) : : "memory");
            }
            if (n >= 4) {
                uint32_t *d32 = (uint32_t *)d;
                const uint32_t *s32 = (const uint32_t *)s;
                size_t words = n >> 2;
                while (words--) {
                    *d32++ = *s32++;
                    __asm__ volatile ("" : "+r"(d32), "+r"(s32) : : "memory");
                }
                d = (uint8_t *)d32;
                s = (const uint8_t *)s32;
                n &= 3;
            }
        }
        while (n--) {
            *d++ = *s++;
            __asm__ volatile ("" : "+r"(d), "+r"(s) : : "memory");
        }
    } else if (d > s) {
        d += n;
        s += n;
        if (((uintptr_t)d & 3) == ((uintptr_t)s & 3)) {
            while (n && ((uintptr_t)d & 3)) {
                *--d = *--s;
                n--;
                __asm__ volatile ("" : "+r"(d), "+r"(s) : : "memory");
            }
            if (n >= 4) {
                uint32_t *d32 = (uint32_t *)d;
                const uint32_t *s32 = (const uint32_t *)s;
                size_t words = n >> 2;
                while (words--) {
                    *--d32 = *--s32;
                    __asm__ volatile ("" : "+r"(d32), "+r"(s32) : : "memory");
                }
                d = (uint8_t *)d32;
                s = (const uint8_t *)s32;
                n &= 3;
            }
        }
        while (n--) {
            *--d = *--s;
            __asm__ volatile ("" : "+r"(d), "+r"(s) : : "memory");
        }
    }
    return dest;
}
#endif // !UNIT_TEST
