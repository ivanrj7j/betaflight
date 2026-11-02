/*
 * Common test helper functions and mathematical validation implementation.
 */

#include "test_helpers.h"

void fill_sequential(uint8_t *buf, size_t len)
{
    if (!buf) return;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i & 0xFF);
    }
}

bool verify_sequential(const uint8_t *buf, size_t len, uint8_t start)
{
    if (!buf) return false;
    for (size_t i = 0; i < len; i++) {
        uint8_t expected = (uint8_t)((start + i) & 0xFF);
        if (buf[i] != expected) {
            return false;
        }
    }
    return true;
}

void fill_canary(uint32_t *buf, size_t words)
{
    if (!buf) return;
    for (size_t i = 0; i < words; i++) {
        buf[i] = 0xA5A5A5A5UL;
    }
}

size_t count_intact_canaries(const uint32_t *buf, size_t words)
{
    if (!buf) return 0;
    size_t count = 0;
    for (size_t i = 0; i < words; i++) {
        if (buf[i] == 0xA5A5A5A5UL) {
            count++;
        }
    }
    return count;
}
