/*
 * Unit tests for Betaflight ARIES v3.0 Non-Volatile Flash Configuration Storage.
 * Test Suite: Module T6 (Phase 5)
 */

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "aries_flash_testable.h"
}

class AriesFlashTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_flash_reset();
    }
};

// T6-01: Rejects address below __config_start
TEST_F(AriesFlashTest, RejectsAddressBelowConfigStart) {
    uint32_t buf[8] = {0};
    configUnlock();
    configStreamerResult_e result = configWriteWord(TEST_CONFIG_START_ADDR - 32, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_ADDRESS_INVALID);

    result = configWriteWord(TEST_CONFIG_START_ADDR - 4, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_ADDRESS_INVALID);

    result = configWriteWord(0x20000000UL, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_ADDRESS_INVALID);
}

// T6-02: Rejects address above __config_end
TEST_F(AriesFlashTest, RejectsAddressAboveConfigEnd) {
    uint32_t buf[8] = {0};
    configUnlock();
    configStreamerResult_e result = configWriteWord(TEST_CONFIG_END_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_ADDRESS_INVALID);

    // Buffer of 32 bytes starting at (END - 16) extends beyond END
    result = configWriteWord(TEST_CONFIG_END_ADDR - 16, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_ADDRESS_INVALID);

    // Address way beyond END
    result = configWriteWord(TEST_CONFIG_END_ADDR + 4096, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_ADDRESS_INVALID);
}

// T6-03: Erases sector on first write to new sector
TEST_F(AriesFlashTest, ErasesSectorOnFirstWrite) {
    uint32_t buf[8] = {0x11111111, 0x22222222, 0x33333333, 0x44444444,
                       0x55555555, 0x66666666, 0x77777777, 0x88888888};
    configUnlock();

    EXPECT_EQ(test_flash_get_erase_count(), 0U);

    configStreamerResult_e result = configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_SUCCESS);

    EXPECT_EQ(test_flash_get_erase_count(), 1U);
    EXPECT_EQ(test_flash_get_last_erased_offset(), 0x1F8000U);
    EXPECT_EQ(test_flash_get_last_erased_sector(), 0x1F8000U);
}

// T6-04: Does NOT re-erase same sector on consecutive writes
TEST_F(AriesFlashTest, DoesNotReEraseSameSectorOnConsecutiveWrites) {
    uint32_t buf1[8] = {0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD,
                        0xEEEEEEEE, 0xFFFFFFFF, 0x00000000, 0x11111111};
    uint32_t buf2[8] = {0x12345678, 0x23456789, 0x3456789A, 0x456789AB,
                        0x56789ABC, 0x6789ABCD, 0x789ABCDE, 0x89ABCDEF};
    configUnlock();

    configStreamerResult_e r1 = configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf1);
    EXPECT_EQ(r1, CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(test_flash_get_erase_count(), 1U);

    // Consecutive write within the same 4KB sector (offset 0x20)
    configStreamerResult_e r2 = configWriteWord(TEST_CONFIG_START_ADDR + 32, (config_streamer_buffer_type_t *)buf2);
    EXPECT_EQ(r2, CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(test_flash_get_erase_count(), 1U); // Must STILL be 1

    // Another write within same sector (offset 0x100)
    configStreamerResult_e r3 = configWriteWord(TEST_CONFIG_START_ADDR + 256, (config_streamer_buffer_type_t *)buf2);
    EXPECT_EQ(r3, CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(test_flash_get_erase_count(), 1U); // Still 1
}

// T6-05: Updates memory-mapped shadow after write
TEST_F(AriesFlashTest, ShadowUpdatedAfterWrite) {
    uint32_t buf[8] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x87654321,
                       0xFEEDFACE, 0x0BADF00D, 0x55AA55AA, 0xAA55AA55};
    configUnlock();

    configStreamerResult_e result = configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_SUCCESS);

    volatile uint32_t *shadow = (volatile uint32_t *)TEST_CONFIG_START_ADDR;
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(shadow[i], buf[i]);
    }
}

// T6-06: After sector erase, shadow region is all 0xFF
TEST_F(AriesFlashTest, ShadowInitializedToAll0xFFOnErase) {
    // Fill region with dirty bytes
    memset((void *)TEST_CONFIG_START_ADDR, 0x5A, TEST_FLASH_SECTOR_SIZE);

    uint32_t buf[8] = {0x01020304, 0x05060708, 0, 0, 0, 0, 0, 0};
    configUnlock();

    // First write triggers sector erase
    configStreamerResult_e result = configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(result, CONFIG_RESULT_SUCCESS);

    // Bytes in the rest of the erased sector beyond written 32 bytes must be 0xFF
    uint8_t *shadow = (uint8_t *)TEST_CONFIG_START_ADDR;
    for (int i = 32; i < TEST_FLASH_SECTOR_SIZE; i++) {
        EXPECT_EQ(shadow[i], 0xFFU);
    }
}

// T6-07: configUnlock() resets lastErasedSector to UINT32_MAX
TEST_F(AriesFlashTest, ConfigUnlockResetsLastErasedSector) {
    uint32_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    configUnlock();

    configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(test_flash_get_last_erased_sector(), 0x1F8000U);
    EXPECT_EQ(test_flash_get_erase_count(), 1U);

    configUnlock();
    EXPECT_EQ(test_flash_get_last_erased_sector(), UINT32_MAX);

    // Writing again to the same sector triggers a new erase
    configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(test_flash_get_erase_count(), 2U);
}

// T6-08: configLock() resets lastErasedSector to UINT32_MAX
TEST_F(AriesFlashTest, ConfigLockResetsLastErasedSector) {
    uint32_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    configUnlock();

    configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(test_flash_get_last_erased_sector(), 0x1F8000U);
    EXPECT_EQ(test_flash_get_erase_count(), 1U);

    configLock();
    EXPECT_EQ(test_flash_get_last_erased_sector(), UINT32_MAX);

    // Next write triggers erase again
    configWriteWord(TEST_CONFIG_START_ADDR, (config_streamer_buffer_type_t *)buf);
    EXPECT_EQ(test_flash_get_erase_count(), 2U);
}

// T6-09: Flash write enable sends opcode 0x06
TEST_F(AriesFlashTest, WriteEnableSendsOpcode0x06) {
    test_flash_reset();
    bool ok = thejas32_flash_write_enable();
    EXPECT_TRUE(ok);

    // Must have transmitted WREN (0x06)
    bool found_wren = false;
    for (uint32_t i = 0; i < test_flash_get_tx_log_count(); i++) {
        if (test_flash_get_tx_log_byte(i) == TEST_SPI_FLASH_CMD_WREN) {
            found_wren = true;
            break;
        }
    }
    EXPECT_TRUE(found_wren);
}

// T6-10: Sector erase sends opcode 0x20 + 24-bit address
TEST_F(AriesFlashTest, SectorEraseSendsOpcode0x20AndAddress) {
    test_flash_reset();
    thejas32_flash_erase_sector(0x1F8000);

    EXPECT_EQ(test_flash_get_erase_count(), 1U);
    EXPECT_EQ(test_flash_get_last_erased_offset(), 0x1F8000U);

    // Verify command sequence in tx log: WREN (0x06), RDSR (0x05), 0x00, then SECTOR_ERASE (0x20), 0x1F, 0x80, 0x00
    bool found_erase_seq = false;
    uint32_t total = test_flash_get_tx_log_count();
    for (uint32_t i = 0; i + 3 < total; i++) {
        if (test_flash_get_tx_log_byte(i) == TEST_SPI_FLASH_CMD_SECTOR_ERASE &&
            test_flash_get_tx_log_byte(i + 1) == 0x1F &&
            test_flash_get_tx_log_byte(i + 2) == 0x80 &&
            test_flash_get_tx_log_byte(i + 3) == 0x00) {
            found_erase_seq = true;
            break;
        }
    }
    EXPECT_TRUE(found_erase_seq);
}

// T6-11: Page program sends opcode 0x02 + 24-bit address + data
TEST_F(AriesFlashTest, PageProgramSendsOpcode0x02AddressAndData) {
    test_flash_reset();
    uint8_t payload[32];
    for (int i = 0; i < 32; i++) {
        payload[i] = (uint8_t)(i + 0x10);
    }

    thejas32_flash_write_page(0x1F8020, payload, 32);

    EXPECT_EQ(test_flash_get_write_count(), 1U);
    EXPECT_EQ(test_flash_get_last_write_offset(), 0x1F8020U);
    EXPECT_EQ(test_flash_get_last_write_len(), 32U);

    const uint8_t *written = test_flash_get_last_write_data();
    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(written[i], payload[i]);
    }

    // Verify page program opcode in SPI stream
    bool found_prog_seq = false;
    uint32_t total = test_flash_get_tx_log_count();
    for (uint32_t i = 0; i + 4 < total; i++) {
        if (test_flash_get_tx_log_byte(i) == TEST_SPI_FLASH_CMD_PAGE_PROG &&
            test_flash_get_tx_log_byte(i + 1) == 0x1F &&
            test_flash_get_tx_log_byte(i + 2) == 0x80 &&
            test_flash_get_tx_log_byte(i + 3) == 0x20 &&
            test_flash_get_tx_log_byte(i + 4) == payload[0]) {
            found_prog_seq = true;
            break;
        }
    }
    EXPECT_TRUE(found_prog_seq);
}

// T6-12: WIP polling loop is bounded (max iterations)
TEST_F(AriesFlashTest, WipPollingLoopIsBounded) {
    test_flash_reset();

    // Normal WIP cleared
    test_flash_set_wip_sticky(false);
    EXPECT_TRUE(thejas32_flash_wait_wip(100));

    // Sticky WIP simulating stuck flash hardware
    test_flash_set_wip_sticky(true);
    EXPECT_FALSE(thejas32_flash_wait_wip(50));
}

// T6-13: Full 8 KB config write fills reserved sector correctly
TEST_F(AriesFlashTest, Full8KBConfigWriteCrossesSectorsCorrectly) {
    configUnlock();

    // 8192 bytes = 256 writes of 32 bytes each
    // Spans 2 sectors: sector 0 (0x1F8000) and sector 1 (0x1F9000)
    for (int chunk = 0; chunk < 256; chunk++) {
        uint32_t buf[8];
        for (int w = 0; w < 8; w++) {
            buf[w] = (uint32_t)((chunk << 16) | w);
        }
        uintptr_t addr = TEST_CONFIG_START_ADDR + (chunk * 32);
        configStreamerResult_e res = configWriteWord(addr, (config_streamer_buffer_type_t *)buf);
        ASSERT_EQ(res, CONFIG_RESULT_SUCCESS);
    }

    // Two 4KB sectors must have been erased exactly once each
    EXPECT_EQ(test_flash_get_erase_count(), 2U);
    EXPECT_EQ(test_flash_get_write_count(), 256U);

    // Verify coherent memory-mapped shadow reads across all 8192 bytes
    for (int chunk = 0; chunk < 256; chunk++) {
        volatile uint32_t *shadow = (volatile uint32_t *)(TEST_CONFIG_START_ADDR + (chunk * 32));
        for (int w = 0; w < 8; w++) {
            uint32_t expected = (uint32_t)((chunk << 16) | w);
            EXPECT_EQ(shadow[w], expected);
        }
    }
}

// Low-Level Controller Initialization & State Tests
TEST_F(AriesFlashTest, ControllerInitConfiguresBaudAndControl) {
    configFlashInit();
    test_thejas32_flash_spi_reg_t *spi = test_flash_get_spi();
    EXPECT_EQ(spi->Baudrate, 0x20U);
    EXPECT_EQ(spi->Control, 0x0000U);
}

TEST_F(AriesFlashTest, WaitIdleBoundedOnStickyBusy) {
    test_flash_set_spi_busy_sticky(true);
    EXPECT_FALSE(thejas32_flash_wait_idle());

    test_flash_set_spi_busy_sticky(false);
    EXPECT_TRUE(thejas32_flash_wait_idle());
}

TEST_F(AriesFlashTest, ConfigClearFlagsIsNoOp) {
    configClearFlags();
    // Verify no side-effects
    EXPECT_EQ(test_flash_get_erase_count(), 0U);
    EXPECT_EQ(test_flash_get_write_count(), 0U);
}
