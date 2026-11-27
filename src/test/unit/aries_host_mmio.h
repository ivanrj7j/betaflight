/*
 * Host unit-test MMIO and RISC-V CSR backing storage for ARIES / THEJAS32 drivers.
 */

#pragma once

#include <stdint.h>
#include "platform/ARIES/include/platform/bsp_thejas32.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t thejas32_host_mstatus;
extern uint32_t thejas32_host_mie;
extern uint32_t thejas32_host_mcycle_hi;
extern uint32_t thejas32_host_mcycle_lo;
extern uint32_t thejas32_host_panic_count;
extern uint32_t thejas32_host_reset_count;

void thejas32_host_reset_all(void);
void thejas32_host_mcycle_set(uint64_t cycles);
void thejas32_host_mcycle_advance(uint64_t delta);
void thejas32_host_mcycle_read(uint32_t *hi_out, uint32_t *lo_out);

#ifdef __cplusplus
}
#endif
