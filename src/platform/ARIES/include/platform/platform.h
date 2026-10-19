/*
 * This file is part of Betaflight.
 * Platform definitions for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

#ifndef __ASM
#define __ASM __asm__
#endif

typedef enum { ARIES_IRQ_0 = 0 } IRQn_Type;

// Basic NVIC priority emulation macros for non-ARM core
#define NVIC_PriorityGroup_2 0x500
#define NVIC_PRIORITY_GROUPING NVIC_PriorityGroup_2
#define NVIC_BUILD_PRIORITY(base,sub) (((((base)<<(4-(7-(NVIC_PRIORITY_GROUPING>>8))))|((sub)&(0x0f>>(7-(NVIC_PRIORITY_GROUPING>>8)))))<<4)&0xf0)
#define NVIC_PRIORITY_BASE(prio) (((prio)>>(4-(7-(NVIC_PRIORITY_GROUPING>>8))))>>4)
#define NVIC_PRIORITY_SUB(prio) (((prio)>>4)&(0x0f>>(7-(NVIC_PRIORITY_GROUPING>>8))))

// Basepri critical section stubs
__attribute__((always_inline)) static inline void __set_BASEPRI(uint32_t basePri) { (void)basePri; }
__attribute__((always_inline)) static inline uint32_t __get_BASEPRI(void) { return 0; }
__attribute__((always_inline)) static inline void __set_BASEPRI_MAX(uint32_t basePri) { (void)basePri; }

// Memory section decorators
#define FAST_CODE
#define FAST_CODE_NOINLINE          __attribute__((noinline))
#define FAST_CODE_NOINLINE_CRITICAL __attribute__((noinline))
#define FAST_IRQ_HANDLER
#define FAST_DATA_ZERO_INIT
#define FAST_DATA

// GPIO Configuration macros
#define IOCFG_OUT_PP                0
#define IOCFG_OUT_OD                0
#define IOCFG_AF_PP                 0
#define IOCFG_AF_OD                 0
#define IOCFG_IPD                   0
#define IOCFG_IPU                   0
#define IOCFG_IN_FLOATING           0

// Bus and device counts
#define SPIDEV_COUNT                1
#define I2CDEV_COUNT                1
#define GYRO_COUNT                  1
typedef void* ADC_TypeDef;

#define TASK_GYROPID_DESIRED_PERIOD 1000 // 1 kHz loop period in us
