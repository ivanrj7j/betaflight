/*
 * Mock RISC-V CSR (mcycle, mcycleh, etc.) for host unit tests.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint64_t mock_mcycle64;
extern uint32_t mock_mstatus;
extern uint32_t mock_mie;

void mock_csr_advance(uint64_t delta);
void mock_csr_set(uint64_t value);
void mock_csr_reset(void);

uint64_t getCycleCounter64(void);
uint32_t getCycleCounter(void);

#ifdef __cplusplus
}
#endif
