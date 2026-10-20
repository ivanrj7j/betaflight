/*
 * This file is part of Betaflight.
 * Non-volatile flash configuration storage for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 * Interfaces Betaflight parameter groups (PG) to the 32 KB reserved config region
 * in memory-mapped NOR flash (0x201F8000 - 0x201FFFFF) via the onboard SPI3 controller (0x10200200).
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "platform.h"
#include "platform/bsp_thejas32.h"
#include "drivers/system.h"
#include "config/config_streamer.h"
#include "config/config_streamer_impl.h"

#if defined(CONFIG_IN_FLASH)

// -----------------------------------------------------------------------------
// SPI NOR Flash Hardware Definitions (Winbond W25Q128 / Micron M25P80 on SPI3)
// -----------------------------------------------------------------------------
#define SPI_FLASH_CMD_WREN          0x06 // Write Enable
#define SPI_FLASH_CMD_WRDI          0x04 // Write Disable
#define SPI_FLASH_CMD_RDSR          0x05 // Read Status Register
#define SPI_FLASH_CMD_PAGE_PROG     0x02 // Page Program (up to 256 bytes)
#define SPI_FLASH_CMD_SECTOR_ERASE  0x20 // 4 KB Sector Erase
#define SPI_FLASH_CMD_READ          0x03 // Read Data Bytes

#define SPI_FLASH_STATUS_WIP        0x01 // Write In Progress bit
#define SPI_FLASH_STATUS_WEL        0x02 // Write Enable Latch bit

#define THEJAS32_SPI3_INDEX         3

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE           4096
#endif

static uint32_t lastErasedSector = UINT32_MAX;

// -----------------------------------------------------------------------------
// Low-Level SPI3 Controller Routines
// -----------------------------------------------------------------------------

static void thejas32_flash_spi_init(void)
{
    volatile thejas32_spi_reg_t *spi = &THEJAS32_SPI(THEJAS32_SPI3_INDEX);

    // Baud divisor: CFD 16 (100 MHz / 16 = 6.25 MHz)
    spi->Baudrate = 0x20;

    // 8-bit transfers, Mode 0 (CPOL=0, CPHA=0), MSB first, CSAAT=0
    spi->Control = 0x0000;
    __asm__ volatile ("fence" ::: "memory");
}

static bool thejas32_flash_wait_idle(void)
{
    volatile thejas32_spi_reg_t *spi = &THEJAS32_SPI(THEJAS32_SPI3_INDEX);
    uint32_t timeout = 1000;
    while ((spi->Status & (1 << 4)) && --timeout) { // Busy bit
        __asm__ volatile ("nop");
    }
    return timeout > 0;
}

static void thejas32_flash_set_cs(bool active)
{
    volatile thejas32_spi_reg_t *spi = &THEJAS32_SPI(THEJAS32_SPI3_INDEX);
    if (active) {
        spi->Control |= (1 << 8); // CSAAT = 1 (Assert CS)
    } else {
        spi->Control &= ~(1 << 8); // CSAAT = 0 (Deassert CS)
    }
    __asm__ volatile ("fence" ::: "memory");
}

static bool thejas32_flash_tx_byte(uint8_t byte)
{
    volatile thejas32_spi_reg_t *spi = &THEJAS32_SPI(THEJAS32_SPI3_INDEX);
    uint32_t timeout = 1000;
    while (!(spi->Status & (1 << 7)) && --timeout) { // TX_HOLD_EMPTY
        __asm__ volatile ("nop");
    }
    if (!timeout) {
        return false;
    }
    spi->TxData = byte;
    __asm__ volatile ("fence" ::: "memory");
    return true;
}

static uint8_t thejas32_flash_rx_byte(void)
{
    volatile thejas32_spi_reg_t *spi = &THEJAS32_SPI(THEJAS32_SPI3_INDEX);
    uint32_t timeout = 1000;
    while (!(spi->Status & (1 << 6)) && --timeout) { // RX_COMPLETE
        __asm__ volatile ("nop");
    }
    return (uint8_t)(spi->RxData & 0xFF);
}

static uint8_t thejas32_flash_read_status(void)
{
    thejas32_flash_wait_idle();
    thejas32_flash_set_cs(true);
    thejas32_flash_tx_byte(SPI_FLASH_CMD_RDSR);
    thejas32_flash_tx_byte(0x00); // Clock dummy byte
    thejas32_flash_set_cs(false);
    thejas32_flash_wait_idle();
    return thejas32_flash_rx_byte();
}

static bool thejas32_flash_wait_wip(uint32_t max_iterations)
{
    while (max_iterations--) {
        uint8_t status = thejas32_flash_read_status();
        if (!(status & SPI_FLASH_STATUS_WIP)) {
            return true;
        }
    }
    return false;
}

static bool thejas32_flash_write_enable(void)
{
    thejas32_flash_wait_idle();
    thejas32_flash_set_cs(false);
    thejas32_flash_tx_byte(SPI_FLASH_CMD_WREN);
    thejas32_flash_wait_idle();

    uint32_t timeout = 100;
    while (--timeout) {
        if (thejas32_flash_read_status() & SPI_FLASH_STATUS_WEL) {
            return true;
        }
    }
    return false;
}

static void thejas32_flash_erase_sector(uint32_t offset)
{
    thejas32_flash_write_enable();
    thejas32_flash_wait_idle();

    thejas32_flash_set_cs(true);
    thejas32_flash_tx_byte(SPI_FLASH_CMD_SECTOR_ERASE);
    thejas32_flash_tx_byte((offset >> 16) & 0xFF);
    thejas32_flash_tx_byte((offset >> 8) & 0xFF);
    thejas32_flash_tx_byte(offset & 0xFF);
    thejas32_flash_set_cs(false);

    thejas32_flash_wait_wip(1000000);
}

static void thejas32_flash_write_page(uint32_t offset, const uint8_t *data, uint32_t len)
{
    thejas32_flash_write_enable();
    thejas32_flash_wait_idle();

    thejas32_flash_set_cs(true);
    thejas32_flash_tx_byte(SPI_FLASH_CMD_PAGE_PROG);
    thejas32_flash_tx_byte((offset >> 16) & 0xFF);
    thejas32_flash_tx_byte((offset >> 8) & 0xFF);
    thejas32_flash_tx_byte(offset & 0xFF);
    for (uint32_t i = 0; i < len; i++) {
        thejas32_flash_tx_byte(data[i]);
    }
    thejas32_flash_set_cs(false);

    thejas32_flash_wait_wip(200000);
}

// -----------------------------------------------------------------------------
// Betaflight Config Streamer API Implementation
// -----------------------------------------------------------------------------

void configFlashInit(void)
{
    thejas32_flash_spi_init();
}

void configUnlock(void)
{
    lastErasedSector = UINT32_MAX;
}

void configLock(void)
{
    thejas32_flash_wait_idle();
    lastErasedSector = UINT32_MAX;
    __asm__ volatile ("fence" ::: "memory");
}

void configClearFlags(void)
{
    // No-op on THEJAS32
}

configStreamerResult_e configWriteWord(uintptr_t address, config_streamer_buffer_type_t *buffer)
{
    if (address < (uintptr_t)&__config_start ||
        (address + CONFIG_STREAMER_BUFFER_SIZE) > (uintptr_t)&__config_end) {
        return CONFIG_RESULT_ADDRESS_INVALID;
    }

    uint32_t flashOffset = (uint32_t)(address - 0x20000000UL);
    uint32_t sectorAddr = flashOffset & ~(FLASH_SECTOR_SIZE - 1);

    // Erase sector when crossing into a new sector
    if (sectorAddr != lastErasedSector) {
        thejas32_flash_erase_sector(sectorAddr);
        // Also update memory-mapped shadow for simulation / immediate coherent reads
        memset((void *)(0x20000000UL + sectorAddr), 0xFF, FLASH_SECTOR_SIZE);
        lastErasedSector = sectorAddr;
    }

    // Program data into physical SPI NOR flash
    thejas32_flash_write_page(flashOffset, (const uint8_t *)buffer, CONFIG_STREAMER_BUFFER_SIZE);

    // Synchronize memory-mapped shadow
    memcpy((void *)address, buffer, CONFIG_STREAMER_BUFFER_SIZE);
    __asm__ volatile ("fence" ::: "memory");

    return CONFIG_RESULT_SUCCESS;
}

#endif // CONFIG_IN_FLASH
