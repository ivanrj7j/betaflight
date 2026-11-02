/*
 * Host-compilable unit test implementation for ARIES v3.0 Flash Configuration Storage.
 * Mirrors src/platform/ARIES/config_flash_aries.c.
 */

#include "aries_flash_testable.h"
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

// SPI registers mock
static test_thejas32_flash_spi_reg_t mock_spi3;

// State tracking
static uint32_t lastErasedSector = UINT32_MAX;
static uint8_t mock_flash_status = 0;
static bool wip_sticky = false;
static bool spi_busy_sticky = false;
static bool tx_hold_empty_sticky = false;

// Transaction logging
#define MAX_TX_LOG 8192
static uint8_t tx_log[MAX_TX_LOG];
static uint32_t tx_log_count = 0;

static uint8_t current_cmd = 0;
static uint32_t cmd_byte_idx = 0;
static uint32_t cmd_addr = 0;

static uint32_t total_erase_count = 0;
static uint32_t last_erased_offset = UINT32_MAX;

static uint32_t total_page_writes = 0;
static uint32_t last_write_offset = UINT32_MAX;
static uint32_t last_write_len = 0;
static uint8_t last_write_data[256];

static bool mmap_done = false;

// -----------------------------------------------------------------------------
// Test fixture initialisation & state inspection
// -----------------------------------------------------------------------------

void test_flash_init_mmap(void)
{
    if (!mmap_done) {
        void *ptr = mmap((void *)TEST_FLASH_BASE_ADDR, 0x200000,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (ptr == MAP_FAILED) {
            perror("mmap failed for test flash region");
        }
        mmap_done = true;
    }
    memset((void *)TEST_CONFIG_START_ADDR, 0xFF, TEST_CONFIG_SIZE);
}

void test_flash_reset(void)
{
    test_flash_init_mmap();

    mock_spi3.Control = 0;
    mock_spi3.Status = (1 << 7) | (1 << 6); // TX_HOLD_EMPTY | RX_COMPLETE
    mock_spi3.Baudrate = 0;
    mock_spi3.TxData = 0;
    mock_spi3.RxData = 0;

    lastErasedSector = UINT32_MAX;
    mock_flash_status = 0;
    wip_sticky = false;
    spi_busy_sticky = false;
    tx_hold_empty_sticky = false;

    tx_log_count = 0;
    current_cmd = 0;
    cmd_byte_idx = 0;
    cmd_addr = 0;

    total_erase_count = 0;
    last_erased_offset = UINT32_MAX;

    total_page_writes = 0;
    last_write_offset = UINT32_MAX;
    last_write_len = 0;
    memset(last_write_data, 0, sizeof(last_write_data));
}

test_thejas32_flash_spi_reg_t *test_flash_get_spi(void)
{
    return &mock_spi3;
}

uint32_t test_flash_get_last_erased_sector(void)
{
    return lastErasedSector;
}

uint32_t test_flash_get_erase_count(void)
{
    return total_erase_count;
}

uint32_t test_flash_get_last_erased_offset(void)
{
    return last_erased_offset;
}

uint32_t test_flash_get_write_count(void)
{
    return total_page_writes;
}

uint32_t test_flash_get_last_write_offset(void)
{
    return last_write_offset;
}

uint32_t test_flash_get_last_write_len(void)
{
    return last_write_len;
}

const uint8_t *test_flash_get_last_write_data(void)
{
    return last_write_data;
}

uint32_t test_flash_get_tx_log_count(void)
{
    return tx_log_count;
}

uint8_t test_flash_get_tx_log_byte(uint32_t index)
{
    if (index < tx_log_count) {
        return tx_log[index];
    }
    return 0;
}

void test_flash_set_mock_status(uint8_t status)
{
    mock_flash_status = status;
}

void test_flash_set_wip_sticky(bool sticky)
{
    wip_sticky = sticky;
    if (sticky) {
        mock_flash_status |= TEST_SPI_FLASH_STATUS_WIP;
    } else {
        mock_flash_status &= ~TEST_SPI_FLASH_STATUS_WIP;
    }
}

void test_flash_set_spi_busy_sticky(bool sticky)
{
    spi_busy_sticky = sticky;
    if (sticky) {
        mock_spi3.Status |= (1 << 4);
    } else {
        mock_spi3.Status &= ~(1 << 4);
    }
}

void test_flash_set_tx_hold_empty_sticky(bool empty)
{
    tx_hold_empty_sticky = empty;
    if (empty) {
        mock_spi3.Status |= (1 << 7);
    } else {
        mock_spi3.Status &= ~(1 << 7);
    }
}

uintptr_t test_get_config_start(void)
{
    return TEST_CONFIG_START_ADDR;
}

uintptr_t test_get_config_end(void)
{
    return TEST_CONFIG_END_ADDR;
}

// -----------------------------------------------------------------------------
// Low-Level SPI3 Controller Routines (Mirroring config_flash_aries.c)
// -----------------------------------------------------------------------------

void thejas32_flash_spi_init(void)
{
    mock_spi3.Baudrate = 0x20;
    mock_spi3.Control = 0x0000;
}

bool thejas32_flash_wait_idle(void)
{
    if (spi_busy_sticky) {
        return false;
    }
    uint32_t timeout = 1000;
    while ((mock_spi3.Status & (1 << 4)) && --timeout) {
        // busy wait
    }
    return timeout > 0;
}

void thejas32_flash_set_cs(bool active)
{
    if (active) {
        mock_spi3.Control |= (1 << 8); // CSAAT = 1
        current_cmd = 0;
        cmd_byte_idx = 0;
        cmd_addr = 0;
    } else {
        mock_spi3.Control &= ~(1 << 8); // CSAAT = 0
        if (current_cmd == TEST_SPI_FLASH_CMD_PAGE_PROG) {
            total_page_writes++;
        }
        current_cmd = 0;
        cmd_byte_idx = 0;
    }
}

bool thejas32_flash_tx_byte(uint8_t byte)
{
    if (!tx_hold_empty_sticky && !(mock_spi3.Status & (1 << 7))) {
        uint32_t timeout = 1000;
        while (!(mock_spi3.Status & (1 << 7)) && --timeout) {
            // wait
        }
        if (!timeout) {
            return false;
        }
    }

    mock_spi3.TxData = byte;

    if (tx_log_count < MAX_TX_LOG) {
        tx_log[tx_log_count++] = byte;
    }

    // Process byte in SPI flash state machine
    if (mock_spi3.Control & (1 << 8)) {
        // CS is active
        if (cmd_byte_idx == 0) {
            current_cmd = byte;
            cmd_byte_idx = 1;
            if (current_cmd == TEST_SPI_FLASH_CMD_RDSR) {
                mock_spi3.RxData = mock_flash_status;
            } else if (current_cmd == TEST_SPI_FLASH_CMD_WREN) {
                mock_flash_status |= TEST_SPI_FLASH_STATUS_WEL;
            } else if (current_cmd == TEST_SPI_FLASH_CMD_WRDI) {
                mock_flash_status &= ~TEST_SPI_FLASH_STATUS_WEL;
            }
        } else {
            if (current_cmd == TEST_SPI_FLASH_CMD_RDSR) {
                mock_spi3.RxData = mock_flash_status;
            } else if (current_cmd == TEST_SPI_FLASH_CMD_SECTOR_ERASE) {
                cmd_addr = (cmd_addr << 8) | byte;
                cmd_byte_idx++;
                if (cmd_byte_idx == 4) { // Opcode + 3 address bytes
                    total_erase_count++;
                    last_erased_offset = cmd_addr;
                    mock_flash_status &= ~TEST_SPI_FLASH_STATUS_WEL;
                }
            } else if (current_cmd == TEST_SPI_FLASH_CMD_PAGE_PROG) {
                if (cmd_byte_idx < 4) { // Opcode + 3 address bytes
                    cmd_addr = (cmd_addr << 8) | byte;
                    cmd_byte_idx++;
                    if (cmd_byte_idx == 4) {
                        last_write_offset = cmd_addr;
                        last_write_len = 0;
                    }
                } else {
                    if (last_write_len < sizeof(last_write_data)) {
                        last_write_data[last_write_len++] = byte;
                    }
                    mock_flash_status &= ~TEST_SPI_FLASH_STATUS_WEL;
                }
            }
        }
    } else {
        // CS inactive commands (such as WREN called before CS asserted)
        if (byte == TEST_SPI_FLASH_CMD_WREN) {
            mock_flash_status |= TEST_SPI_FLASH_STATUS_WEL;
        }
    }

    return true;
}

uint8_t thejas32_flash_rx_byte(void)
{
    uint32_t timeout = 1000;
    while (!(mock_spi3.Status & (1 << 6)) && --timeout) {
        // wait
    }
    return (uint8_t)(mock_spi3.RxData & 0xFF);
}

uint8_t thejas32_flash_read_status(void)
{
    thejas32_flash_wait_idle();
    thejas32_flash_set_cs(true);
    thejas32_flash_tx_byte(TEST_SPI_FLASH_CMD_RDSR);
    thejas32_flash_tx_byte(0x00); // Clock dummy byte
    thejas32_flash_set_cs(false);
    thejas32_flash_wait_idle();
    return thejas32_flash_rx_byte();
}

bool thejas32_flash_wait_wip(uint32_t max_iterations)
{
    while (max_iterations--) {
        uint8_t status = thejas32_flash_read_status();
        if (!(status & TEST_SPI_FLASH_STATUS_WIP)) {
            return true;
        }
    }
    return false;
}

bool thejas32_flash_write_enable(void)
{
    thejas32_flash_wait_idle();
    thejas32_flash_set_cs(false);
    thejas32_flash_tx_byte(TEST_SPI_FLASH_CMD_WREN);
    thejas32_flash_wait_idle();

    uint32_t timeout = 100;
    while (--timeout) {
        if (thejas32_flash_read_status() & TEST_SPI_FLASH_STATUS_WEL) {
            return true;
        }
    }
    return false;
}

void thejas32_flash_erase_sector(uint32_t offset)
{
    thejas32_flash_write_enable();
    thejas32_flash_wait_idle();

    thejas32_flash_set_cs(true);
    thejas32_flash_tx_byte(TEST_SPI_FLASH_CMD_SECTOR_ERASE);
    thejas32_flash_tx_byte((offset >> 16) & 0xFF);
    thejas32_flash_tx_byte((offset >> 8) & 0xFF);
    thejas32_flash_tx_byte(offset & 0xFF);
    thejas32_flash_set_cs(false);

    thejas32_flash_wait_wip(1000000);
}

void thejas32_flash_write_page(uint32_t offset, const uint8_t *data, uint32_t len)
{
    thejas32_flash_write_enable();
    thejas32_flash_wait_idle();

    thejas32_flash_set_cs(true);
    thejas32_flash_tx_byte(TEST_SPI_FLASH_CMD_PAGE_PROG);
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
// Config Streamer API Implementation (Mirroring config_flash_aries.c)
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
}

void configClearFlags(void)
{
    // No-op
}

configStreamerResult_e configWriteWord(uintptr_t address, config_streamer_buffer_type_t *buffer)
{
    if (address < TEST_CONFIG_START_ADDR ||
        (address + TEST_CONFIG_STREAMER_BUFFER_SIZE) > TEST_CONFIG_END_ADDR) {
        return CONFIG_RESULT_ADDRESS_INVALID;
    }

    uint32_t flashOffset = (uint32_t)(address - TEST_FLASH_BASE_ADDR);
    uint32_t sectorAddr = flashOffset & ~(TEST_FLASH_SECTOR_SIZE - 1);

    // Erase sector when crossing into a new sector
    if (sectorAddr != lastErasedSector) {
        thejas32_flash_erase_sector(sectorAddr);
        // Also update memory-mapped shadow for simulation / immediate coherent reads
        memset((void *)(TEST_FLASH_BASE_ADDR + sectorAddr), 0xFF, TEST_FLASH_SECTOR_SIZE);
        lastErasedSector = sectorAddr;
    }

    // Program data into physical SPI NOR flash
    thejas32_flash_write_page(flashOffset, (const uint8_t *)buffer, TEST_CONFIG_STREAMER_BUFFER_SIZE);

    // Synchronize memory-mapped shadow
    memcpy((void *)address, buffer, TEST_CONFIG_STREAMER_BUFFER_SIZE);

    return CONFIG_RESULT_SUCCESS;
}
