/*
 * This file is part of Betaflight.
 *
 * Platform definitions and hardware primitives for C-DAC ARIES v3.0
 * (THEJAS32 SoC / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Citations & Evaluations:
 *   - knowledgebase/codebase_evaluations/04_EVAL_PLATFORM_H.md
 *   - knowledgebase/codebase_evaluations/12_EVAL_STUB_DRIVERS_C.md
 *   - PLAN/01_BOOT_LINKER_AND_CORE_PRIMITIVES.md (Microcomponent 01_C)
 *   - src/main/build/atomic.h
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

#include "platform/bsp_thejas32.h"

#ifndef __ASM
#define __ASM __asm__
#endif

#ifndef __NOP
#define __NOP() __asm__ volatile ("nop")
#endif

#ifndef __WFI
#define __WFI() __asm__ volatile ("wfi")
#endif

#define __REV(x)   __builtin_bswap32(x)
#define __REV16(x) __builtin_bswap16(x)

typedef enum { ARIES_IRQ_0 = 0 } IRQn_Type;

// Basic NVIC priority emulation macros for non-ARM core
#define NVIC_PriorityGroup_2 0x500
#define NVIC_PRIORITY_GROUPING NVIC_PriorityGroup_2
#define NVIC_BUILD_PRIORITY(base,sub) (((((base)<<(4-(7-(NVIC_PRIORITY_GROUPING>>8))))|((sub)&(0x0f>>(7-(NVIC_PRIORITY_GROUPING>>8)))))<<4)&0xf0)
#define NVIC_PRIORITY_BASE(prio) (((prio)>>(4-(7-(NVIC_PRIORITY_GROUPING>>8))))>>4)
#define NVIC_PRIORITY_SUB(prio) (((prio)>>4)&(0x0f>>(7-(NVIC_PRIORITY_GROUPING>>8))))

/*
 * =============================================================================
 * State-Preserving RISC-V RV32IM Atomics & Critical Sections
 * Silicon Ground Truth 5: Atomics via CSR mstatus.MIE (Bit 3 = 0x08)
 * Solves 04_EVAL_PLATFORM_H.md (Score 1.0/10 for dummy stubs).
 * =============================================================================
 */

// Opt out of ARM MSR basepri assembly in src/main/build/atomic.h
#define PLATFORM_CUSTOM_BASEPRI_NB

__attribute__((always_inline)) static inline uint8_t __get_BASEPRI(void)
{
    uint32_t mstatus_val;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(mstatus_val));
    // If MIE (bit 3) is set, interrupts are enabled -> return 0 (unmasked).
    // If MIE is 0, interrupts are already disabled -> return 1 (masked).
    return (mstatus_val & 8U) ? 0U : 1U;
}

__attribute__((always_inline)) static inline void __set_BASEPRI(uint32_t basePri)
{
    // If restoring to 0, re-enable interrupts.
    // If restoring to non-zero, keep interrupts disabled.
    if (basePri == 0U) {
        __asm__ volatile ("csrs mstatus, 8" ::: "memory");
    } else {
        __asm__ volatile ("csrc mstatus, 8" ::: "memory");
    }
}

__attribute__((always_inline)) static inline void __set_BASEPRI_nb(uint32_t basePri)
{
    if (basePri == 0U) {
        __asm__ volatile ("csrs mstatus, 8");
    } else {
        __asm__ volatile ("csrc mstatus, 8");
    }
}

__attribute__((always_inline)) static inline void __set_BASEPRI_MAX(uint32_t basePri)
{
    (void)basePri;
    // Elevate priority (mask interrupts) with memory barrier
    __asm__ volatile ("csrc mstatus, 8" ::: "memory");
}

__attribute__((always_inline)) static inline void __set_BASEPRI_MAX_nb(uint32_t basePri)
{
    (void)basePri;
    // Elevate priority (mask interrupts) without memory barrier
    __asm__ volatile ("csrc mstatus, 8");
}

/* Global interrupt control for RISC-V M-Mode */
__attribute__((always_inline)) static inline void __disable_irq(void)
{
    __asm__ volatile ("csrc mstatus, 8" ::: "memory");
}

__attribute__((always_inline)) static inline void __enable_irq(void)
{
    // Enable MEIE (Machine External Interrupt Enable, bit 11 = 0x800) in mie
    __asm__ volatile ("csrs mie, %0" : : "r"(0x800) : "memory");
    // Enable MIE (Machine Interrupt Enable, bit 3 = 8) in mstatus
    __asm__ volatile ("csrs mstatus, 8" ::: "memory");
}

/*
 * Dedicated Fast 1-Instruction Atomic Enter / Conditional Exit Primitives
 */
__attribute__((always_inline)) static inline uint32_t thejas32_atomic_enter(void)
{
    uint32_t prev_mstatus;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(prev_mstatus) : : "memory");
    return prev_mstatus;
}

__attribute__((always_inline)) static inline void thejas32_atomic_exit(uint32_t prev_mstatus)
{
    if (prev_mstatus & 8U) {
        __asm__ volatile ("csrs mstatus, 8" : : : "memory");
    }
}

// Memory section decorators for In-SRAM Execution (All code and data share uniform SRAM latency)
#define FAST_CODE
#define FAST_CODE_NOINLINE          __attribute__((noinline))
#define FAST_CODE_NOINLINE_CRITICAL __attribute__((noinline))
#define FAST_IRQ_HANDLER
#define FAST_DATA_ZERO_INIT
#define FAST_DATA

// GPIO Configuration mode constants
#define THEJAS32_GPIO_MODE_INPUT     0
#define THEJAS32_GPIO_MODE_OUTPUT    1
#define THEJAS32_GPIO_MODE_OUTPUT_OD 2
#define THEJAS32_GPIO_PULLUP         1
#define THEJAS32_GPIO_PULLDOWN       2

#define IO_CONFIG(mode, speed, pupd) ((mode) | ((speed) << 2) | ((pupd) << 5))

#define IOCFG_OUT_PP                IO_CONFIG(THEJAS32_GPIO_MODE_OUTPUT, 0, 0)
#define IOCFG_OUT_OD                IO_CONFIG(THEJAS32_GPIO_MODE_OUTPUT_OD, 0, 0)
#define IOCFG_AF_PP                 IO_CONFIG(THEJAS32_GPIO_MODE_OUTPUT, 0, 0)
#define IOCFG_AF_OD                 IO_CONFIG(THEJAS32_GPIO_MODE_OUTPUT_OD, 0, 0)
#define IOCFG_IPD                   IO_CONFIG(THEJAS32_GPIO_MODE_INPUT, 0, THEJAS32_GPIO_PULLDOWN)
#define IOCFG_IPU                   IO_CONFIG(THEJAS32_GPIO_MODE_INPUT, 0, THEJAS32_GPIO_PULLUP)
#define IOCFG_IN_FLOATING           IO_CONFIG(THEJAS32_GPIO_MODE_INPUT, 0, 0)

#define GPIO_PIN_RESET              0
#define GPIO_PIN_SET                1

#define DEFIO_PORT_PINS             64

#define SPI_IO_CS_CFG               IOCFG_OUT_PP

// Bus and device counts
#define SPIDEV_COUNT                1
#define MAX_SPI_PIN_SEL             3
#define I2CDEV_COUNT                1
#define GYRO_COUNT                  1
typedef void* ADC_TypeDef;

typedef struct {
    void* test;
} DMA_InitTypeDef;

/*
 * Flight Loop Timing Clamping (Silicon Ground Truth 4)
 * Clamped to 1.0 kHz (1000 us period) for soft-float stability with > 65% idle headroom.
 */
#define TASK_GYROPID_DESIRED_PERIOD 1000 // 1 kHz loop period in us

// FunctionalState (referenced by timer.h and shared drivers)
typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;

// Unique device ID stubs (no OTP on THEJAS32)
#define U_ID_0 0
#define U_ID_1 0
#define U_ID_2 0

#ifndef FLASH_CONFIG_BUFFER_TYPE
#define FLASH_CONFIG_BUFFER_TYPE uint32_t
#endif
#ifndef FLASH_CONFIG_STREAMER_BUFFER_SIZE
#define FLASH_CONFIG_STREAMER_BUFFER_SIZE 32
#endif

// System core clock (100 MHz on ARIES v3.0)
extern uint32_t SystemCoreClock;

#define SCHEDULER_DELAY_LIMIT           10

// UART Configuration
#define SERIAL_UART_FIRST_INDEX         0
#define UART_TRAIT_AF_PORT              1
#define UARTHARDWARE_MAX_PINS           4
#define UART_TX_BUFFER_ATTRIBUTE
#define UART_RX_BUFFER_ATTRIBUTE
#define SERIAL_TRAIT_PIN_CONFIG         1

// Early Hardware UART0 Diagnostic Telemetry (115200 baud, polled)
void thejas32_early_uart_init(void);
void thejas32_early_uart_putc(char c);
void thejas32_early_uart_puts(const char *s);
void thejas32_early_uart_print_hex(uint32_t val);

#if defined(ARIES_DEV_LOGGING) && (ARIES_DEV_LOGGING == 1)
#define THEJAS_LOG(s) thejas32_early_uart_puts(s)
#else
#define THEJAS_LOG(s) ((void)0)
#endif
