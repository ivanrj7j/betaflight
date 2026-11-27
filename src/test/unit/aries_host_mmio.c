/*
 * Host unit-test MMIO and RISC-V CSR backing storage for ARIES / THEJAS32 drivers.
 */

#include <string.h>
#include "aries_host_mmio.h"

thejas32_intr_ctrl_t thejas32_host_intr_ctrl;
thejas32_uart_reg_t  thejas32_host_uart[3];
thejas32_timer_reg_t thejas32_host_timer[3];
thejas32_spi_reg_t   thejas32_host_spi[4];
uint16_t            thejas32_host_gpio_dir[2];
uint16_t            thejas32_host_gpio_data[2][16];
uint32_t            thejas32_host_fence_count;

uint32_t thejas32_host_mstatus;
uint32_t thejas32_host_mie;
uint32_t thejas32_host_mcycle_hi;
uint32_t thejas32_host_mcycle_lo;
uint32_t thejas32_host_panic_count;
uint32_t thejas32_host_reset_count;

void thejas32_host_reset_all(void)
{
    memset(&thejas32_host_intr_ctrl, 0, sizeof(thejas32_host_intr_ctrl));
    memset(thejas32_host_uart, 0, sizeof(thejas32_host_uart));
    memset(thejas32_host_timer, 0, sizeof(thejas32_host_timer));
    memset(thejas32_host_spi, 0, sizeof(thejas32_host_spi));
    memset(thejas32_host_gpio_dir, 0, sizeof(thejas32_host_gpio_dir));
    memset(thejas32_host_gpio_data, 0, sizeof(thejas32_host_gpio_data));
    thejas32_host_fence_count = 0U;
    thejas32_host_mstatus = 0U;
    thejas32_host_mie = 0U;
    thejas32_host_mcycle_hi = 0U;
    thejas32_host_mcycle_lo = 0U;
    thejas32_host_panic_count = 0U;
    thejas32_host_reset_count = 0U;

    // Default UART THRE bits set (ready to transmit) matching idle 16550 hardware
    for (int i = 0; i < 3; i++) {
        thejas32_host_uart[i].UART_LSR = 0x60U;
        thejas32_host_uart[i].UART_IIR_FCR = 0x01U; // No interrupt pending
    }
}

void thejas32_host_mcycle_set(uint64_t cycles)
{
    thejas32_host_mcycle_hi = (uint32_t)(cycles >> 32);
    thejas32_host_mcycle_lo = (uint32_t)(cycles & 0xFFFFFFFFULL);
}

void thejas32_host_mcycle_advance(uint64_t delta)
{
    uint64_t cur = ((uint64_t)thejas32_host_mcycle_hi << 32) | (uint64_t)thejas32_host_mcycle_lo;
    thejas32_host_mcycle_set(cur + delta);
}

void thejas32_host_mcycle_read(uint32_t *hi_out, uint32_t *lo_out)
{
    if (hi_out) {
        *hi_out = thejas32_host_mcycle_hi;
    }
    if (lo_out) {
        *lo_out = thejas32_host_mcycle_lo;
    }
}
