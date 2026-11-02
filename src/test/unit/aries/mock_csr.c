/*
 * Mock RISC-V CSR implementation for host unit testing.
 */

#include "mock_csr.h"

uint64_t mock_mcycle64 = 0;

void mock_csr_advance(uint64_t delta)
{
    mock_mcycle64 += delta;
}

void mock_csr_set(uint64_t value)
{
    mock_mcycle64 = value;
}

void mock_csr_reset(void)
{
    mock_mcycle64 = 0;
}

uint64_t getCycleCounter64(void)
{
    return mock_mcycle64;
}

uint32_t getCycleCounter(void)
{
    return (uint32_t)(mock_mcycle64 & 0xFFFFFFFFUL);
}
