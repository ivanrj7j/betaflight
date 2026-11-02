/*
 * Testable interface for ARIES v3.0 Flash Configuration Storage.
 * Host unit test shim mirroring src/platform/ARIES/config_flash_aries.c.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// SPI NOR Flash opcodes
#define TEST_SPI_FLASH_CMD_WREN          0x06
#define TEST_SPI_FLASH_CMD_WRDI          0x04
#define TEST_SPI_FLASH_CMD_RDSR          0x05
#define TEST_SPI_FLASH_CMD_PAGE_PROG     0x02
#define TEST_SPI_FLASH_CMD_SECTOR_ERASE  0x20
#define TEST_SPI_FLASH_CMD_READ          0x03

#define TEST_SPI_FLASH_STATUS_WIP        0x01
#define TEST_SPI_FLASH_STATUS_WEL        0x02

#define TEST_FLASH_SECTOR_SIZE           4096
#define TEST_CONFIG_STREAMER_BUFFER_SIZE 32

typedef enum {
    CONFIG_RESULT_TIMEOUT           = -4,
    CONFIG_RESULT_ADDRESS_INVALID   = -3,
    CONFIG_RESULT_INCOMPLETE        = -2,
    CONFIG_RESULT_FAILURE           = -1,
    CONFIG_RESULT_SUCCESS           =  0
} configStreamerResult_e;

typedef uint32_t config_streamer_buffer_type_t;

// SPI register layout
typedef struct {
    volatile uint32_t Control;
    volatile uint32_t Status;
    volatile uint32_t Baudrate;
    volatile uint32_t TxData;
    volatile uint32_t RxData;
} test_thejas32_flash_spi_reg_t;

// Mock configuration boundaries
#define TEST_FLASH_BASE_ADDR   0x20000000UL
#define TEST_CONFIG_START_ADDR 0x201F8000UL
#define TEST_CONFIG_END_ADDR   0x20200000UL
#define TEST_CONFIG_SIZE       (TEST_CONFIG_END_ADDR - TEST_CONFIG_START_ADDR)

// Driver API functions under test
void thejas32_flash_spi_init(void);
bool thejas32_flash_wait_idle(void);
void thejas32_flash_set_cs(bool active);
bool thejas32_flash_tx_byte(uint8_t byte);
uint8_t thejas32_flash_rx_byte(void);
uint8_t thejas32_flash_read_status(void);
bool thejas32_flash_wait_wip(uint32_t max_iterations);
bool thejas32_flash_write_enable(void);
void thejas32_flash_erase_sector(uint32_t offset);
void thejas32_flash_write_page(uint32_t offset, const uint8_t *data, uint32_t len);

void configFlashInit(void);
void configUnlock(void);
void configLock(void);
void configClearFlags(void);
configStreamerResult_e configWriteWord(uintptr_t address, config_streamer_buffer_type_t *buffer);

// Test fixture & mock state management
void test_flash_init_mmap(void);
void test_flash_reset(void);
test_thejas32_flash_spi_reg_t *test_flash_get_spi(void);
uint32_t test_flash_get_last_erased_sector(void);
uint32_t test_flash_get_erase_count(void);
uint32_t test_flash_get_last_erased_offset(void);
uint32_t test_flash_get_write_count(void);
uint32_t test_flash_get_last_write_offset(void);
uint32_t test_flash_get_last_write_len(void);
const uint8_t *test_flash_get_last_write_data(void);

uint32_t test_flash_get_tx_log_count(void);
uint8_t test_flash_get_tx_log_byte(uint32_t index);
void test_flash_set_mock_status(uint8_t status);
void test_flash_set_wip_sticky(bool sticky);
void test_flash_set_spi_busy_sticky(bool sticky);
void test_flash_set_tx_hold_empty_sticky(bool empty);

uintptr_t test_get_config_start(void);
uintptr_t test_get_config_end(void);

#ifdef __cplusplus
}
#endif
