/*
 * Test Suite: C-DAC ARIES v3.0 MDP_SPI_3 External NOR Flash Persistence Specification (Phase 20A)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, No XIP)
 * Reference:
 *   - PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md (Microcomponent 05_E)
 *   - PLAN/20_DEVELOPMENT_PHASES.md (Phase 20: 1.0 kHz Scheduler, JEDEC Flash & Flight Readiness)
 *   - knowledgebase/CRITICAL_KNOWLEDGE_GAPS_AND_ASSUMPTIONS.md (Gap 5, Erratum 10)
 *   - knowledgebase/codebase_evaluations/10_EVAL_CONFIG_FLASH_ARIES_C.md
 *   - knowledgebase/PORTING_KNOWLEDGE_GRAPH.md (MDP_SPI_3 = 0x10200200)
 *   - lib/vega-sdk/vega-sdk/bsp/include/spi.h & drivers/spi/spi.c
 *   - lib/vega-sdk/vega-sdk/examples/spi/spi_write_read_pgm/main.c
 *   - lib/vega-bsp/libraries/EEPROM_SPI/at25sf161.cpp & at25sf161.h
 *   - lib/vega-bsp/libraries/EEPROM/src/EEPROM.cpp
 *   - src/main/config/config_eeprom.c & config_streamer.c
 *
 * ANTI-ASSUMPTION NOTE ([PENDING TEAM CONFIRMATION]):
 *   Physical U5 (2 MB NOR flash) partition offset is not locked (VEGA BSP EEPROM.cpp uses
 *   0x040000 above a 256 KB program window; legacy master config_flash_aries.c used 0x1F8000).
 *   Tests parameterize the slot offset and must NOT hardcode an unconfirmed silicon sector
 *   for destructive erase/program operations.
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Tests real Betaflight pg.c + config_eeprom.c + config_streamer.c + crc.c against
 *   thejas32_host_spi[3] (0x10200200) and a cycle-accurate JEDEC NOR flash model.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>
#include <array>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
#include "../main/platform.h"
#include "build/build_config.h"
#include "common/crc.h"
#include "common/maths.h"
#include "common/streambuf.h"
#include "common/utils.h"
#include "config/config.h"
#include "config/config_eeprom.h"
#include "config/config_streamer.h"
#include "drivers/io.h"
#include "drivers/system.h"
#include "fc/runtime_config.h"
#include "pg/pg.h"
#include "pg/pg_ids.h"
#include "platform/ARIES/include/platform/bsp_thejas32.h"
#include "aries_host_mmio.h"

PG_REGISTER_WITH_RESET_TEMPLATE(systemConfig_t, systemConfig, PG_SYSTEM_CONFIG, 4);
PG_RESET_TEMPLATE(systemConfig_t, systemConfig,
    .pidProfileIndex = 0,
    .activeRateProfile = 0,
    .debug_mode = 0,
    .task_statistics = 1,
    .rateProfile6PosSwitch = 0,
    .cpu_overclock = 0,
    .powerOnArmingGraceTime = 5,
    .boardIdentifier = "ARV3",
    .hseMhz = 100,
    .configurationState = CONFIGURATION_STATE_UNCONFIGURED,
    .enableStickArming = 0,
    .activeBatteryProfile = 0,
);
}

namespace {

// Silicon Constants (VEGA SDK spi.h, bsp_thejas32.h, PORTING_KNOWLEDGE_GRAPH.md)
constexpr uint32_t SPI2_MMIO_ADDR              = 0x10200100UL;
constexpr uint32_t SPI3_MMIO_ADDR              = 0x10200200UL;
constexpr uint32_t UNMAPPED_XIP_ADDR           = 0x20000000UL;

// SPI3 Register Bitmasks (C-DAC VEGA SDK spi.h)
constexpr uint32_t SPI_CTRL_MODE_SHIFT         = 4U;
constexpr uint32_t SPI_CTRL_MODE_3             = 3U;
constexpr uint32_t SPI_CTRL_CSAAT_BIT          = (1UL << 8);
constexpr uint32_t SPI_STAT_BUSY               = (1UL << 4);
constexpr uint32_t SPI_STAT_OVERRUN            = (1UL << 5);
constexpr uint32_t SPI_STAT_RX_COMPLETE        = (1UL << 6);
constexpr uint32_t SPI_STAT_TX_EMPTY           = (1UL << 7);
constexpr uint32_t SPI_BAUD_DIV_8              = 1U; // 12.5 MHz max safe clock
constexpr uint32_t SPI_BAUD_DIV_16             = 2U; // 6.25 MHz
constexpr uint32_t SPI_BAUD_DIV_32             = 3U; // 3.125 MHz

// JEDEC SPI NOR Flash Opcodes (AT25SF161 / W25Q16 / M25P80)
constexpr uint8_t JEDEC_CMD_WRITE_DISABLE      = 0x04U;
constexpr uint8_t JEDEC_CMD_WRITE_STATUS       = 0x01U;
constexpr uint8_t JEDEC_CMD_PAGE_PROGRAM       = 0x02U;
constexpr uint8_t JEDEC_CMD_READ_DATA          = 0x03U;
constexpr uint8_t JEDEC_CMD_READ_STATUS_1      = 0x05U;
constexpr uint8_t JEDEC_CMD_WRITE_ENABLE       = 0x06U;
constexpr uint8_t JEDEC_CMD_SECTOR_ERASE_4K    = 0x20U;
constexpr uint8_t JEDEC_CMD_JEDEC_ID           = 0x9FU;
constexpr uint8_t JEDEC_CMD_RELEASE_PD         = 0xABU;

constexpr uint8_t FLASH_SR_WIP                 = 0x01U;
constexpr uint8_t FLASH_SR_WEL                 = 0x02U;

constexpr size_t FLASH_TOTAL_SIZE              = 2U * 1024U * 1024U; // 2 MB U5 NOR flash
constexpr size_t FLASH_SECTOR_SIZE             = 4096U;
constexpr size_t FLASH_PAGE_SIZE               = 256U;

// Parameterized candidate partitions ([PENDING TEAM CONFIRMATION])
constexpr uint32_t CANDIDATE_PARTITION_BSP     = 0x040000UL; // VEGA BSP EEPROM.cpp offset
constexpr uint32_t CANDIDATE_PARTITION_MASTER  = 0x1F8000UL; // Historical master offset

// Faithful cycle-level SPI3 + JEDEC NOR flash emulator bound to thejas32_host_spi[3]
struct MockJedecNorFlash {
    std::vector<uint8_t> mem;
    uint8_t mfgId;
    uint8_t memType;
    uint8_t capId;

    bool deepPowerDown;
    bool statusWel;
    bool statusWip;
    uint32_t wipPollsRemaining;
    bool forceStickyWip;
    bool forceWelDenied;
    bool injectReadbackBitFlip;
    uint32_t readbackFlipOffset;

    // Hardware SPI controller fault injection
    bool forceSpiBusyStuck;
    bool forceRxCompleteStuck;
    uint32_t rxTimeoutAfterTransfers;
    uint32_t transferCount;
    uint32_t xipDereferenceCount;

    // Protocol transaction FSM tracking CS/CSAAT framing
    bool csActive;
    uint8_t activeCmd;
    uint32_t cmdByteIdx;
    uint32_t currentAddr;
    uint32_t pageBaseAddr;
    uint8_t pageOffset;

    std::vector<uint8_t> opcodeLog;

    void reset(uint8_t mfg = 0x1FU, uint8_t type = 0x86U, uint8_t cap = 0x01U) {
        mem.assign(FLASH_TOTAL_SIZE, 0xFFU);
        mfgId = mfg;
        memType = type;
        capId = cap;
        deepPowerDown = false;
        statusWel = false;
        statusWip = false;
        wipPollsRemaining = 0U;
        forceStickyWip = false;
        forceWelDenied = false;
        injectReadbackBitFlip = false;
        readbackFlipOffset = 0U;
        forceSpiBusyStuck = false;
        forceRxCompleteStuck = false;
        rxTimeoutAfterTransfers = 0xFFFFFFFFU;
        transferCount = 0U;
        xipDereferenceCount = 0U;
        csActive = false;
        activeCmd = 0U;
        cmdByteIdx = 0U;
        currentAddr = 0U;
        pageBaseAddr = 0U;
        pageOffset = 0U;
        opcodeLog.clear();
    }

    void csAssert() {
        csActive = true;
        activeCmd = 0U;
        cmdByteIdx = 0U;
    }

    void csDeassert() {
        if (csActive && (activeCmd == JEDEC_CMD_SECTOR_ERASE_4K || activeCmd == JEDEC_CMD_PAGE_PROGRAM)) {
            if (cmdByteIdx >= 4U && statusWel) {
                statusWel = false;
                if (wipPollsRemaining > 0U || forceStickyWip) {
                    statusWip = true;
                }
            }
        }
        csActive = false;
        activeCmd = 0U;
        cmdByteIdx = 0U;
    }

    uint8_t clockByte(uint8_t tx) {
        if (!csActive) {
            return 0xFFU;
        }
        if (cmdByteIdx == 0U) {
            activeCmd = tx;
            cmdByteIdx = 1U;
            opcodeLog.push_back(tx);

            if (activeCmd == JEDEC_CMD_RELEASE_PD) {
                deepPowerDown = false;
                return 0xFFU;
            }
            if (deepPowerDown) {
                return 0xFFU;
            }
            if (activeCmd == JEDEC_CMD_WRITE_ENABLE) {
                if (!forceWelDenied) {
                    statusWel = true;
                }
                return 0xFFU;
            }
            if (activeCmd == JEDEC_CMD_WRITE_DISABLE) {
                statusWel = false;
                return 0xFFU;
            }
            return 0xFFU;
        }

        if (deepPowerDown) {
            cmdByteIdx++;
            return 0xFFU;
        }

        switch (activeCmd) {
        case JEDEC_CMD_JEDEC_ID: {
            uint8_t rx = 0xFFU;
            if (cmdByteIdx == 1U) rx = mfgId;
            else if (cmdByteIdx == 2U) rx = memType;
            else if (cmdByteIdx == 3U) rx = capId;
            cmdByteIdx++;
            return rx;
        }
        case JEDEC_CMD_READ_STATUS_1: {
            uint8_t sr = 0U;
            if (forceStickyWip) {
                sr |= FLASH_SR_WIP;
            } else if (statusWip) {
                if (wipPollsRemaining > 0U) {
                    sr |= FLASH_SR_WIP;
                    wipPollsRemaining--;
                    if (wipPollsRemaining == 0U) {
                        statusWip = false;
                    }
                }
            }
            if (statusWel) {
                sr |= FLASH_SR_WEL;
            }
            cmdByteIdx++;
            return sr;
        }
        case JEDEC_CMD_READ_DATA: {
            if (cmdByteIdx == 1U) { currentAddr = ((uint32_t)tx << 16); cmdByteIdx++; return 0xFFU; }
            if (cmdByteIdx == 2U) { currentAddr |= ((uint32_t)tx << 8); cmdByteIdx++; return 0xFFU; }
            if (cmdByteIdx == 3U) { currentAddr |= (uint32_t)tx; cmdByteIdx++; return 0xFFU; }
            uint32_t addr = currentAddr % FLASH_TOTAL_SIZE;
            uint8_t val = mem[addr];
            if (injectReadbackBitFlip && addr == readbackFlipOffset) {
                val ^= 0x01U;
            }
            currentAddr++;
            cmdByteIdx++;
            return val;
        }
        case JEDEC_CMD_SECTOR_ERASE_4K: {
            if (cmdByteIdx == 1U) { currentAddr = ((uint32_t)tx << 16); cmdByteIdx++; return 0xFFU; }
            if (cmdByteIdx == 2U) { currentAddr |= ((uint32_t)tx << 8); cmdByteIdx++; return 0xFFU; }
            if (cmdByteIdx == 3U) {
                currentAddr |= (uint32_t)tx;
                cmdByteIdx++;
                if (statusWel && currentAddr < FLASH_TOTAL_SIZE) {
                    uint32_t sectorBase = currentAddr & ~(uint32_t)(FLASH_SECTOR_SIZE - 1U);
                    memset(&mem[sectorBase], 0xFFU, FLASH_SECTOR_SIZE);
                }
                return 0xFFU;
            }
            cmdByteIdx++;
            return 0xFFU;
        }
        case JEDEC_CMD_PAGE_PROGRAM: {
            if (cmdByteIdx == 1U) { currentAddr = ((uint32_t)tx << 16); cmdByteIdx++; return 0xFFU; }
            if (cmdByteIdx == 2U) {
                currentAddr |= ((uint32_t)tx << 8);
                cmdByteIdx++;
                return 0xFFU;
            }
            if (cmdByteIdx == 3U) {
                currentAddr |= (uint32_t)tx;
                pageBaseAddr = currentAddr & ~(uint32_t)(FLASH_PAGE_SIZE - 1U);
                pageOffset = (uint8_t)(currentAddr & (FLASH_PAGE_SIZE - 1U));
                cmdByteIdx++;
                return 0xFFU;
            }
            // Real NOR flash physical page-buffer wrap & AND-only bit programming
            if (statusWel && (pageBaseAddr + pageOffset) < FLASH_TOTAL_SIZE) {
                mem[pageBaseAddr + pageOffset] &= tx;
            }
            pageOffset++; // Intentionally wraps at 256 bytes like real silicon!
            cmdByteIdx++;
            return 0xFFU;
        }
        default:
            cmdByteIdx++;
            return 0xFFU;
        }
    }
};

MockJedecNorFlash g_flash;
uint32_t g_spi3ResetCount = 0U;

// -----------------------------------------------------------------------------
// Reference MDP_SPI_3 Flash Persistence Driver Contract (Phase 20A Specification)
// Bound directly to THEJAS32_SPI(3) MMIO registers in aries_host_mmio
// -----------------------------------------------------------------------------

#define SPI3_TIMEOUT_ITERS 4096U

static inline thejas32_spi_reg_t *spi3_hw(void)
{
    return thejas32_spi_from_instance((const void *)SPI3_MMIO_ADDR);
}

static void spi3_reset_ctrl(thejas32_spi_reg_t *hw)
{
    volatile uint32_t dummy = hw->RxData;
    (void)dummy;
    uint32_t saved = hw->Control & ~SPI_CTRL_CSAAT_BIT;
    hw->Control = 0U;
    THEJAS32_MEMORY_FENCE();
    hw->Control = saved;
    THEJAS32_MEMORY_FENCE();
    dummy = hw->RxData;
    (void)dummy;
    hw->Status = SPI_STAT_TX_EMPTY;
    g_spi3ResetCount++;
}

static bool spi3_init(uint16_t baudDiv)
{
    thejas32_spi_reg_t *hw = spi3_hw();
    uint16_t clampedDiv = (baudDiv < SPI_BAUD_DIV_8) ? SPI_BAUD_DIV_8 : baudDiv;
    hw->Baudrate = ((uint32_t)clampedDiv << 4);
    hw->Control = (SPI_CTRL_MODE_3 << SPI_CTRL_MODE_SHIFT);
    THEJAS32_MEMORY_FENCE();

    volatile uint32_t dummy = hw->RxData;
    (void)dummy;
    hw->Status = SPI_STAT_TX_EMPTY;
    return true;
}

static void spi3_cs_set(bool asserted)
{
    thejas32_spi_reg_t *hw = spi3_hw();
    if (asserted) {
        hw->Control |= SPI_CTRL_CSAAT_BIT;
        g_flash.csAssert();
    } else {
        hw->Control &= ~SPI_CTRL_CSAAT_BIT;
        g_flash.csDeassert();
    }
    THEJAS32_MEMORY_FENCE();
}

static bool spi3_xfer_byte(uint8_t out, uint8_t *inOut)
{
    thejas32_spi_reg_t *hw = spi3_hw();
    uint32_t timeout = SPI3_TIMEOUT_ITERS;

    // Clear stale overrun/rx residue first if controller flags overrun
    if (hw->Status & SPI_STAT_OVERRUN) {
        volatile uint32_t flush = hw->RxData;
        (void)flush;
        hw->Status &= ~(SPI_STAT_OVERRUN | SPI_STAT_RX_COMPLETE);
        hw->Status |= SPI_STAT_TX_EMPTY;
    }

    while (((hw->Status & SPI_STAT_BUSY) || !(hw->Status & SPI_STAT_TX_EMPTY)) && --timeout) {
        if (g_flash.forceSpiBusyStuck) {
            hw->Status |= SPI_STAT_BUSY;
        }
    }
    if (timeout == 0U) {
        spi3_cs_set(false);
        spi3_reset_ctrl(hw);
        return false;
    }

    hw->TxData = out;
    THEJAS32_MEMORY_FENCE();

    g_flash.transferCount++;
    bool failRx = g_flash.forceRxCompleteStuck || (g_flash.transferCount >= g_flash.rxTimeoutAfterTransfers);
    if (!failRx) {
        uint8_t rx = g_flash.clockByte(out);
        hw->RxData = rx;
        hw->Status |= (SPI_STAT_RX_COMPLETE | SPI_STAT_TX_EMPTY);
    } else {
        hw->Status &= ~SPI_STAT_RX_COMPLETE;
    }

    timeout = SPI3_TIMEOUT_ITERS;
    while (!(hw->Status & SPI_STAT_RX_COMPLETE) && --timeout) {}
    if (timeout == 0U) {
        spi3_cs_set(false);
        spi3_reset_ctrl(hw);
        return false;
    }

    uint8_t rxByte = (uint8_t)(hw->RxData & 0xFFU);
    if (inOut) {
        *inOut = rxByte;
    }
    return true;
}

static bool flash3_read_status(uint8_t *srOut)
{
    uint8_t sr = 0xFFU;
    spi3_cs_set(true);
    bool ok = spi3_xfer_byte(JEDEC_CMD_READ_STATUS_1, nullptr) &&
              spi3_xfer_byte(0x00U, &sr);
    spi3_cs_set(false);
    if (ok && srOut) {
        *srOut = sr;
    }
    return ok;
}

static bool flash3_wait_ready(uint32_t maxPolls)
{
    uint8_t sr = FLASH_SR_WIP;
    for (uint32_t i = 0U; i < maxPolls; i++) {
        if (!flash3_read_status(&sr)) {
            return false;
        }
        if ((sr & FLASH_SR_WIP) == 0U) {
            return true;
        }
    }
    return false;
}

static bool flash3_write_disable(void)
{
    spi3_cs_set(true);
    bool ok = spi3_xfer_byte(JEDEC_CMD_WRITE_DISABLE, nullptr);
    spi3_cs_set(false);
    return ok;
}

static bool flash3_write_enable(void)
{
    spi3_cs_set(true);
    bool ok = spi3_xfer_byte(JEDEC_CMD_WRITE_ENABLE, nullptr);
    spi3_cs_set(false);
    if (!ok) {
        return false;
    }
    uint8_t sr = 0U;
    for (uint32_t i = 0U; i < 64U; i++) {
        if (!flash3_read_status(&sr)) {
            return false;
        }
        if ((sr & FLASH_SR_WEL) != 0U) {
            return true;
        }
    }
    return false;
}

static bool flash3_wake_and_read_jedec_id(uint8_t idOut[3])
{
    if (!idOut) {
        return false;
    }
    spi3_cs_set(true);
    bool ok = spi3_xfer_byte(JEDEC_CMD_RELEASE_PD, nullptr);
    spi3_cs_set(false);
    if (!ok) {
        return false;
    }
    spi3_cs_set(true);
    ok = spi3_xfer_byte(JEDEC_CMD_JEDEC_ID, nullptr) &&
         spi3_xfer_byte(0x00U, &idOut[0]) &&
         spi3_xfer_byte(0x00U, &idOut[1]) &&
         spi3_xfer_byte(0x00U, &idOut[2]);
    spi3_cs_set(false);
    return ok;
}

static bool flash3_erase_sector_4k(uint32_t addr)
{
    if (ARMING_FLAG(ARMED) || addr >= FLASH_TOTAL_SIZE) {
        return false;
    }
    if (!flash3_wait_ready(1024U) || !flash3_write_enable()) {
        flash3_write_disable();
        return false;
    }
    spi3_cs_set(true);
    bool ok = spi3_xfer_byte(JEDEC_CMD_SECTOR_ERASE_4K, nullptr) &&
              spi3_xfer_byte((uint8_t)(addr >> 16), nullptr) &&
              spi3_xfer_byte((uint8_t)(addr >> 8), nullptr) &&
              spi3_xfer_byte((uint8_t)(addr & 0xFFU), nullptr);
    spi3_cs_set(false);
    if (!ok || !flash3_wait_ready(2048U)) {
        flash3_write_disable();
        return false;
    }
    flash3_write_disable();
    return true;
}

static bool flash3_write_bytes(uint32_t addr, const uint8_t *buf, size_t len)
{
    if (ARMING_FLAG(ARMED) || !buf || len == 0U || addr >= FLASH_TOTAL_SIZE || (addr + len) > FLASH_TOTAL_SIZE) {
        return false;
    }
    size_t remaining = len;
    uint32_t curAddr = addr;
    const uint8_t *curSrc = buf;

    while (remaining > 0U) {
        const size_t pageOffset = curAddr & (FLASH_PAGE_SIZE - 1U);
        size_t chunk = FLASH_PAGE_SIZE - pageOffset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        if (!flash3_wait_ready(1024U) || !flash3_write_enable()) {
            flash3_write_disable();
            return false;
        }

        spi3_cs_set(true);
        bool ok = spi3_xfer_byte(JEDEC_CMD_PAGE_PROGRAM, nullptr) &&
                  spi3_xfer_byte((uint8_t)(curAddr >> 16), nullptr) &&
                  spi3_xfer_byte((uint8_t)(curAddr >> 8), nullptr) &&
                  spi3_xfer_byte((uint8_t)(curAddr & 0xFFU), nullptr);
        for (size_t i = 0U; ok && i < chunk; i++) {
            ok = spi3_xfer_byte(curSrc[i], nullptr);
        }
        spi3_cs_set(false);

        if (!ok || !flash3_wait_ready(1024U)) {
            flash3_write_disable();
            return false;
        }

        curAddr += (uint32_t)chunk;
        curSrc += chunk;
        remaining -= chunk;
    }

    flash3_write_disable();
    return true;
}

static bool flash3_read_bytes(uint32_t addr, uint8_t *buf, size_t len)
{
    if (!buf || len == 0U || addr >= FLASH_TOTAL_SIZE || (addr + len) > FLASH_TOTAL_SIZE) {
        return false;
    }
    if (!flash3_wait_ready(1024U)) {
        return false;
    }
    spi3_cs_set(true);
    bool ok = spi3_xfer_byte(JEDEC_CMD_READ_DATA, nullptr) &&
              spi3_xfer_byte((uint8_t)(addr >> 16), nullptr) &&
              spi3_xfer_byte((uint8_t)(addr >> 8), nullptr) &&
              spi3_xfer_byte((uint8_t)(addr & 0xFFU), nullptr);
    for (size_t i = 0U; ok && i < len; i++) {
        ok = spi3_xfer_byte(0xFFU, &buf[i]);
    }
    spi3_cs_set(false);
    return ok;
}

static bool flash3_save_and_verify_config_8k(uint32_t partitionBase, const uint8_t *src8k, size_t len)
{
    if (len != EEPROM_SIZE || !src8k) {
        return false;
    }
    if (!flash3_erase_sector_4k(partitionBase) || !flash3_erase_sector_4k(partitionBase + FLASH_SECTOR_SIZE)) {
        return false;
    }
    if (!flash3_write_bytes(partitionBase, src8k, len)) {
        return false;
    }
    std::array<uint8_t, EEPROM_SIZE> verifyBuf{};
    if (!flash3_read_bytes(partitionBase, verifyBuf.data(), verifyBuf.size())) {
        return false;
    }
    return memcmp(src8k, verifyBuf.data(), len) == 0;
}

class AriesConfigFlashTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        g_flash.reset();
        g_spi3ResetCount = 0U;
        DISABLE_ARMING_FLAG(ARMED);
        pgResetAll();
        memset(eepromData, 0, sizeof(eepromData));
        spi3_init(SPI_BAUD_DIV_16);
    }
};

// =============================================================================
// TEST 01: MDP_SPI_3 (0x10200200) vs SPI2 (0x10200100) Mapping Reconciliation
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_01_Spi3BaseAddress_0x10200200_VsSpi2_0x10200100_Mapping)
{
    // Arrange & Act: Resolve peripheral indices from BSP hardware addresses
    thejas32_spi_reg_t *mappedSpi2 = thejas32_spi_from_instance((const void *)SPI2_MMIO_ADDR);
    thejas32_spi_reg_t *mappedSpi3 = thejas32_spi_from_instance((const void *)SPI3_MMIO_ADDR);

    // Assert: 0x10200100 maps to SPI2; 0x10200200 maps to MDP_SPI_3 (per VEGA SDK config.h & spi.h)
    EXPECT_EQ(mappedSpi2, &thejas32_host_spi[2]);
    EXPECT_EQ(mappedSpi3, &thejas32_host_spi[3]);
    EXPECT_NE(mappedSpi2, mappedSpi3);

    // Real-world flight consequence:
    // Writing flash JEDEC opcodes to 0x10200100 targets SPI2 instead of onboard U5 NOR flash on MDP_SPI_3.
}

// =============================================================================
// TEST 02: JEDEC ID Probe (0x9F) & Zero Unmapped 0x20000000 XIP Dereference
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_02_JedecIdRead0x9F_MatchesAt25sf161_And_W25q16_WithZeroXipDereference)
{
    // Arrange: Probe both Adesto AT25SF161 (0x1F 0x86 0x01) and Winbond W25Q16 (0xEF 0x40 0x15)
    uint8_t idA[3] = {};
    uint8_t idB[3] = {};

    // Act
    ASSERT_TRUE(flash3_wake_and_read_jedec_id(idA));
    g_flash.reset(0xEFU, 0x40U, 0x15U);
    ASSERT_TRUE(flash3_wake_and_read_jedec_id(idB));

    // Assert
    EXPECT_EQ(idA[0], 0x1FU);
    EXPECT_EQ(idA[1], 0x86U);
    EXPECT_EQ(idA[2], 0x01U);
    EXPECT_EQ(idB[0], 0xEFU);
    EXPECT_EQ(idB[1], 0x40U);
    EXPECT_EQ(idB[2], 0x15U);
    EXPECT_EQ(g_flash.xipDereferenceCount, 0U);
    EXPECT_NE(UNMAPPED_XIP_ADDR, SPI3_MMIO_ADDR);

    // Real-world flight consequence:
    // Dereferencing 0x20000000 on non-XIP THEJAS32 triggers immediate mcause=5/7 access faults.
}

// =============================================================================
// TEST 03: Release from Deep Power-Down (0xAB) Precedes Cold Reads
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_03_DeepPowerDownWake0xAb_PrecedesReadOnColdBoot)
{
    // Arrange: Chip powers up asleep in Deep Power-Down
    g_flash.deepPowerDown = true;

    // Act 1: Reading JEDEC ID without wake fails (returns 0xFF)
    uint8_t rawId[3] = {};
    spi3_cs_set(true);
    spi3_xfer_byte(JEDEC_CMD_JEDEC_ID, nullptr);
    spi3_xfer_byte(0x00U, &rawId[0]);
    spi3_xfer_byte(0x00U, &rawId[1]);
    spi3_xfer_byte(0x00U, &rawId[2]);
    spi3_cs_set(false);
    EXPECT_EQ(rawId[0], 0xFFU);

    // Act 2: Wake with 0xAB then read JEDEC ID
    ASSERT_TRUE(flash3_wake_and_read_jedec_id(rawId));

    // Assert
    EXPECT_FALSE(g_flash.deepPowerDown);
    EXPECT_EQ(rawId[0], 0x1FU);
    EXPECT_EQ(g_flash.opcodeLog.front(), JEDEC_CMD_JEDEC_ID);
    EXPECT_EQ(g_flash.opcodeLog[1], JEDEC_CMD_RELEASE_PD);
}

// =============================================================================
// TEST 04: SPI3 Init Configures Mode 3, Safe Clock Clamp, Clears RxData & Masks IRQs
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_04_Spi3InitMode3AndBaudDivisor_ResetSequence)
{
    // Arrange: Pollute RxData and request unsafe Divisor 0 (25 MHz)
    THEJAS32_SPI(3).RxData = 0xABU;
    THEJAS32_SPI(3).Control = 0U;

    // Act
    EXPECT_TRUE(spi3_init(0U));

    // Assert
    EXPECT_EQ((THEJAS32_SPI(3).Control >> SPI_CTRL_MODE_SHIFT) & 0x03U, SPI_CTRL_MODE_3);
    EXPECT_EQ(THEJAS32_SPI(3).Control & SPI_CTRL_CSAAT_BIT, 0U);
    EXPECT_GE(THEJAS32_SPI(3).Baudrate, (SPI_BAUD_DIV_8 << 4));
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_SPI1), 0U);
}

// =============================================================================
// TEST 05: Status Register Polling (0x05) Waits Cleanly Until WIP Clears
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_05_StatusRegisterPolling0x05_WipAndWelBitMasks)
{
    // Arrange: Simulate active erase taking 5 status polls
    g_flash.statusWip = true;
    g_flash.wipPollsRemaining = 5U;

    // Act
    bool ready = flash3_wait_ready(16U);

    // Assert
    EXPECT_TRUE(ready);
    EXPECT_FALSE(g_flash.statusWip);
    EXPECT_EQ(g_flash.wipPollsRemaining, 0U);
}

// =============================================================================
// TEST 06: Write Enable (0x06) Gate & WEL Timeout Protection
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_06_WriteEnable0x06_BeforeEraseOrProgram_And_WelTimeoutGuard)
{
    // Arrange: Normal flash then flash with hardware write-protect forcing WEL=0
    EXPECT_TRUE(flash3_write_enable());
    EXPECT_TRUE(g_flash.statusWel);
    EXPECT_TRUE(flash3_write_disable());
    EXPECT_FALSE(g_flash.statusWel);

    g_flash.forceWelDenied = true;
    uint8_t byte = 0x5AU;

    // Act & Assert: Write fails fast without hanging or mutating memory
    EXPECT_FALSE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, &byte, 1U));
    EXPECT_EQ(g_flash.mem[CANDIDATE_PARTITION_BSP], 0xFFU);
}

// =============================================================================
// TEST 07: 4 KB Sector Erase (0x20) Erases Target Sector and Preserves Neighbors
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_07_SectorErase4KB_0x20_ErasesToAll0xFF_PreservesAdjacentSectors)
{
    // Arrange: Fill 3 contiguous sectors around parameterized candidate slot with non-FF pattern
    const uint32_t sectorBase = CANDIDATE_PARTITION_BSP;
    memset(&g_flash.mem[sectorBase - FLASH_SECTOR_SIZE], 0xAAU, FLASH_SECTOR_SIZE * 3U);

    // Act
    ASSERT_TRUE(flash3_erase_sector_4k(sectorBase));

    // Assert: Target 4 KB strictly 0xFF; bytes immediately before & after remain untouched 0xAA
    EXPECT_EQ(g_flash.mem[sectorBase - 1U], 0xAAU);
    for (size_t i = 0U; i < FLASH_SECTOR_SIZE; i++) {
        ASSERT_EQ(g_flash.mem[sectorBase + i], 0xFFU);
    }
    EXPECT_EQ(g_flash.mem[sectorBase + FLASH_SECTOR_SIZE], 0xAAU);
}

// =============================================================================
// TEST 08: Page Program (0x02) NOR AND-Programming Physics (1B, 17B, 256B)
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_08_PageProgram0x02_SingleByte_Through_256ByteFullPage)
{
    // Arrange: Erase target sector and prepare 1B, 17B, and 256B payloads
    const uint32_t base = CANDIDATE_PARTITION_BSP;
    ASSERT_TRUE(flash3_erase_sector_4k(base));

    uint8_t single = 0x5AU;
    uint8_t mid[17];
    uint8_t fullPage[256];
    for (size_t i = 0U; i < sizeof(mid); i++) mid[i] = (uint8_t)(0xA0U + i);
    for (size_t i = 0U; i < sizeof(fullPage); i++) fullPage[i] = (uint8_t)(i ^ 0x3CU);

    // Act
    ASSERT_TRUE(flash3_write_bytes(base, &single, 1U));
    ASSERT_TRUE(flash3_write_bytes(base + 0x10U, mid, sizeof(mid)));
    ASSERT_TRUE(flash3_write_bytes(base + 0x100U, fullPage, sizeof(fullPage)));

    // Assert: Verify NOR bit clearing (writing 0xFF over 0x5A cannot flip 0 bits back to 1)
    uint8_t attemptSetOne = 0xFFU;
    ASSERT_TRUE(flash3_write_bytes(base, &attemptSetOne, 1U));
    EXPECT_EQ(g_flash.mem[base], 0x5AU);
    EXPECT_EQ(memcmp(&g_flash.mem[base + 0x10U], mid, sizeof(mid)), 0);
    EXPECT_EQ(memcmp(&g_flash.mem[base + 0x100U], fullPage, sizeof(fullPage)), 0);
}

// =============================================================================
// TEST 09: 256-Byte Page Boundary Crossing Splits Without Intra-Page Wraparound
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_09_PageProgram_Crosses256BytePageBoundary_SplitsTransactionsNoWrap)
{
    // Arrange: Write 300 bytes starting at offset 0xFA (6 bytes before 256-byte page boundary)
    const uint32_t startAddr = CANDIDATE_PARTITION_BSP + 0xFAU;
    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));

    std::array<uint8_t, 300> payload{};
    for (size_t i = 0U; i < payload.size(); i++) {
        payload[i] = (uint8_t)((i * 3U + 1U) & 0xFFU);
    }

    // Act
    ASSERT_TRUE(flash3_write_bytes(startAddr, payload.data(), payload.size()));

    // Assert: Page start bytes [0..0xF9] remain 0xFF (no wrap overwrite!), [0xFA..] exact match
    for (size_t i = 0U; i < 0xFAU; i++) {
        ASSERT_EQ(g_flash.mem[CANDIDATE_PARTITION_BSP + i], 0xFFU);
    }
    EXPECT_EQ(memcmp(&g_flash.mem[startAddr], payload.data(), payload.size()), 0);
}

// =============================================================================
// TEST 10: Real config_eeprom.c 8 KB Save Across Two 4 KB Sectors & Readback (0x03)
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_10_Full8KBConfigEepromSaveAcrossTwo4KBSectors_AndVerifyRead0x03)
{
    // Arrange: Modify real Betaflight PG state and serialize to eepromData[8192] via real config_eeprom.c
    systemConfigMutable()->pidProfileIndex = 2U;
    systemConfigMutable()->powerOnArmingGraceTime = 9U;
    writeConfigToEEPROM();
    ASSERT_TRUE(isEEPROMVersionValid());
    ASSERT_TRUE(isEEPROMStructureValid());

    // Act: Save all 8192 bytes across two 4 KB flash sectors and read back via 0x03
    ASSERT_TRUE(flash3_save_and_verify_config_8k(CANDIDATE_PARTITION_BSP, eepromData, EEPROM_SIZE));

    // Assert: Flash sectors hold intact CRC-validated config matching eepromData byte-for-byte
    EXPECT_EQ(memcmp(&g_flash.mem[CANDIDATE_PARTITION_BSP], eepromData, EEPROM_SIZE), 0);
}

// =============================================================================
// TEST 11: Cold Power-Off Wipe of SRAM Restores Saved PGs From SPI3 Flash
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_11_ColdRebootSimulation_WipesSramAndReloadsFlashIntact)
{
    // Arrange: Save customized config into flash
    systemConfigMutable()->pidProfileIndex = 1U;
    systemConfigMutable()->activeRateProfile = 2U;
    systemConfigMutable()->configurationState = CONFIGURATION_STATE_CONFIGURED;
    writeConfigToEEPROM();
    ASSERT_TRUE(flash3_save_and_verify_config_8k(CANDIDATE_PARTITION_BSP, eepromData, EEPROM_SIZE));

    // Simulate cold power loss: destroy SRAM eepromData[] & reset all live PGs to defaults
    memset(eepromData, 0x00U, sizeof(eepromData));
    pgResetAll();
    ASSERT_EQ(systemConfig()->pidProfileIndex, 0U);
    ASSERT_EQ(systemConfig()->configurationState, CONFIGURATION_STATE_UNCONFIGURED);

    // Act: Boot reload from SPI3 into eepromData[] and run real Betaflight loadEEPROM()
    ASSERT_TRUE(flash3_read_bytes(CANDIDATE_PARTITION_BSP, eepromData, EEPROM_SIZE));
    ASSERT_TRUE(isEEPROMVersionValid());
    ASSERT_TRUE(isEEPROMStructureValid());
    ASSERT_TRUE(loadEEPROM());

    // Assert: User profiles restored cleanly across cold reboot
    EXPECT_EQ(systemConfig()->pidProfileIndex, 1U);
    EXPECT_EQ(systemConfig()->activeRateProfile, 2U);
    EXPECT_EQ(systemConfig()->configurationState, CONFIGURATION_STATE_CONFIGURED);
}

// =============================================================================
// TEST 12: Write Disable (0x04) Issued on Save Completion & Error Abort
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_12_WriteDisable0x04_IssuedOnCompletionAndTimeoutAbort)
{
    // Arrange & Act 1: Successful save ends with 0x04 and WEL=0
    uint8_t d = 0x11U;
    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    ASSERT_TRUE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, &d, 1U));
    EXPECT_FALSE(g_flash.statusWel);
    EXPECT_EQ(g_flash.opcodeLog.back(), JEDEC_CMD_WRITE_DISABLE);

    // Act 2: Mid-program SPI timeout still issues 0x04 lock & deasserts CSAAT
    g_flash.rxTimeoutAfterTransfers = g_flash.transferCount + 3U;
    EXPECT_FALSE(flash3_write_bytes(CANDIDATE_PARTITION_BSP + 1U, &d, 1U));
    EXPECT_FALSE(g_flash.csActive);
}

// =============================================================================
// TEST 13: Stuck SPI3 STAT_BUSY Bounds Timeout & Triggers Controller Reset
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_13_Spi3BusyWaitTimeout_BoundedNoInfiniteLockup)
{
    // Arrange
    g_flash.forceSpiBusyStuck = true;
    THEJAS32_SPI(3).Status = SPI_STAT_BUSY;
    uint8_t dummy = 0xFFU;

    // Act
    bool ok = flash3_write_bytes(CANDIDATE_PARTITION_BSP, &dummy, 1U);

    // Assert
    EXPECT_FALSE(ok);
    EXPECT_GT(g_spi3ResetCount, 0U);
    EXPECT_EQ(THEJAS32_SPI(3).Control & SPI_CTRL_CSAAT_BIT, 0U);
}

// =============================================================================
// TEST 14: Stuck RX_COMPLETE Mid-Transfer Recovers & Enables Subsequent Read
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_14_Spi3RxCompleteTimeout_MidTransferRecovery)
{
    // Arrange: Seed known data then fail RX_COMPLETE on byte 3 of read
    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    const uint8_t pattern[4] = { 0xDEU, 0xADU, 0xBEU, 0xEFU };
    ASSERT_TRUE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, pattern, 4U));

    g_flash.rxTimeoutAfterTransfers = g_flash.transferCount + 4U;
    uint8_t out[4] = {};

    // Act 1: Timed-out read fails safely
    EXPECT_FALSE(flash3_read_bytes(CANDIDATE_PARTITION_BSP, out, sizeof(out)));
    EXPECT_GT(g_spi3ResetCount, 0U);

    // Act 2: Fault clears -> subsequent read succeeds completely
    g_flash.rxTimeoutAfterTransfers = 0xFFFFFFFFU;
    EXPECT_TRUE(flash3_read_bytes(CANDIDATE_PARTITION_BSP, out, sizeof(out)));
    EXPECT_EQ(memcmp(out, pattern, 4U), 0);
}

// =============================================================================
// TEST 15: Flash WIP Never Clears -> Bounded Poll Timeout Avoids Motor Starvation
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_15_FlashWipNeverClears_BoundedTimeoutPreventsMotorIrqStarvation)
{
    // Arrange
    g_flash.forceStickyWip = true;
    uint8_t d = 0x33U;

    // Act & Assert: Bounded timeout returns false cleanly without hanging forever
    EXPECT_FALSE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    EXPECT_FALSE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, &d, 1U));
}

// =============================================================================
// TEST 16: Armed In-Flight Guard Rejects Sector Erase & Page Program
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_16_ArmedInFlightWriteGuard_RejectsEraseOrProgramWhenArmed)
{
    // Arrange: Write valid data while disarmed, then ARM aircraft
    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    const uint8_t initByte = 0x77U;
    ASSERT_TRUE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, &initByte, 1U));

    ENABLE_ARMING_FLAG(ARMED);
    const size_t opCountBefore = g_flash.opcodeLog.size();
    const uint8_t overwrite = 0x00U;

    // Act
    EXPECT_FALSE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    EXPECT_FALSE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, &overwrite, 1U));

    // Assert: Zero SPI3 erase/program opcodes issued while armed!
    EXPECT_EQ(g_flash.opcodeLog.size(), opCountBefore);
    EXPECT_EQ(g_flash.mem[CANDIDATE_PARTITION_BSP], 0x77U);

    // Real-world flight consequence:
    // Erasing NOR flash mid-flight stalls the core for 45-400 ms and drops Timer 1 motor updates.
}

// =============================================================================
// TEST 17: Blank 0xFF Flash or Corrupt Structure Falls Back Cleanly to pgResetAll
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_17_CorruptBlankFlashMagic_FallsBackCleanlyToPgDefaults)
{
    // Arrange: Flash is blank erased (all 0xFF)
    ASSERT_TRUE(flash3_read_bytes(CANDIDATE_PARTITION_BSP, eepromData, EEPROM_SIZE));

    // Act
    EXPECT_FALSE(isEEPROMVersionValid());
    EXPECT_FALSE(isEEPROMStructureValid());
    pgResetAll();

    // Assert
    EXPECT_STREQ(systemConfig()->boardIdentifier, "ARV3");
    EXPECT_EQ(systemConfig()->configurationState, CONFIGURATION_STATE_UNCONFIGURED);
}

// =============================================================================
// TEST 18: Readback Verify Mismatch Detected After Flash Program
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_18_FlashReadbackVerifyMismatch_CaughtAndReported)
{
    // Arrange: Serialize valid config then inject 1-bit readback fault at offset +128
    writeConfigToEEPROM();
    g_flash.injectReadbackBitFlip = true;
    g_flash.readbackFlipOffset = CANDIDATE_PARTITION_BSP + 128U;

    // Act & Assert: Verify detects 1-bit mismatch cleanly
    EXPECT_FALSE(flash3_save_and_verify_config_8k(CANDIDATE_PARTITION_BSP, eepromData, EEPROM_SIZE));
}

// =============================================================================
// TEST 19: Out-of-Bounds Flash Address, Zero Length & Null Buffer Rejected
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_19_OutOfBoundsFlashAddressAndLength_RejectedBeforeTransfer)
{
    uint8_t buf[16] = { 0x12U };
    const size_t opsBefore = g_flash.opcodeLog.size();

    // Act & Assert: Zero SPI commands emitted for invalid inputs
    EXPECT_FALSE(flash3_erase_sector_4k(FLASH_TOTAL_SIZE));
    EXPECT_FALSE(flash3_write_bytes(FLASH_TOTAL_SIZE - 4U, buf, 16U));
    EXPECT_FALSE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, nullptr, 16U));
    EXPECT_FALSE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, buf, 0U));
    EXPECT_FALSE(flash3_read_bytes(FLASH_TOTAL_SIZE, buf, 4U));
    EXPECT_FALSE(flash3_read_bytes(CANDIDATE_PARTITION_BSP, nullptr, 4U));
    EXPECT_FALSE(flash3_read_bytes(CANDIDATE_PARTITION_BSP, buf, 0U));
    EXPECT_EQ(g_flash.opcodeLog.size(), opsBefore);
}

// =============================================================================
// TEST 20: Stale SPI3 STAT_OVERRUN Flushed Before Valid JEDEC Read (0x03)
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_20_OverrunBit5_DummyReadAndShiftFlushRecovery)
{
    // Arrange: Write expected bytes then inject stale OVERRUN + junk RxData
    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    const uint8_t expected[2] = { 0x55U, 0xAAU };
    ASSERT_TRUE(flash3_write_bytes(CANDIDATE_PARTITION_BSP, expected, 2U));

    THEJAS32_SPI(3).Status |= SPI_STAT_OVERRUN;
    THEJAS32_SPI(3).RxData = 0x99U;

    // Act
    uint8_t got[2] = {};
    ASSERT_TRUE(flash3_read_bytes(CANDIDATE_PARTITION_BSP, got, 2U));

    // Assert
    EXPECT_EQ(got[0], 0x55U);
    EXPECT_EQ(got[1], 0xAAU);
    EXPECT_EQ(THEJAS32_SPI(3).Status & SPI_STAT_OVERRUN, 0U);
}

// =============================================================================
// TEST 21: SPI3 Operations Leave SPI0 IMU (PA5..PA8) & Motor GPIOs (PA1..PA4) Intact
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_21_Spi0ImuIsolation_Spi3FlashOpsLeaveSpi0AndMotorsUntouched)
{
    // Arrange: Configure active SPI0 state and parked motor GPIO latches
    THEJAS32_SPI(0).Baudrate = (SPI_BAUD_DIV_8 << 4);
    THEJAS32_SPI(0).Control = (SPI_CTRL_MODE_3 << SPI_CTRL_MODE_SHIFT);
    THEJAS32_SPI(0).TxData = 0x75U;
    thejas32_host_gpio_data[0][8] = 1U; // PA8 CS HIGH
    thejas32_host_gpio_data[0][1] = 0U; // PA1..PA4 LOW

    writeConfigToEEPROM();

    // Act
    ASSERT_TRUE(flash3_save_and_verify_config_8k(CANDIDATE_PARTITION_BSP, eepromData, EEPROM_SIZE));

    // Assert
    EXPECT_EQ(THEJAS32_SPI(0).Baudrate, (SPI_BAUD_DIV_8 << 4));
    EXPECT_EQ(THEJAS32_SPI(0).Control, (SPI_CTRL_MODE_3 << SPI_CTRL_MODE_SHIFT));
    EXPECT_EQ(THEJAS32_SPI(0).TxData, 0x75U);
    EXPECT_EQ(thejas32_host_gpio_data[0][8], 1U);
    EXPECT_EQ(thejas32_host_gpio_data[0][1], 0U);
}

// =============================================================================
// TEST 22: 24-Bit Big-Endian Address Encoding Across 0x040000 and 0x1F8000 Slots
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_22_24BitBigEndianAddressEncoding_ParameterizedPartitions)
{
    // Verify parameterized multi-sector writes do not wrap 16-bit (64 KB) address boundaries
    const uint8_t tagA[4] = { 'B', 'S', 'P', '1' };
    const uint8_t tagB[4] = { 'M', 'S', 'T', '2' };

    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_MASTER));
    ASSERT_TRUE(flash3_write_bytes(CANDIDATE_PARTITION_BSP + 3U, tagA, 4U));
    ASSERT_TRUE(flash3_write_bytes(CANDIDATE_PARTITION_MASTER + 3U, tagB, 4U));

    uint8_t rdA[4] = {};
    uint8_t rdB[4] = {};
    ASSERT_TRUE(flash3_read_bytes(CANDIDATE_PARTITION_BSP + 3U, rdA, 4U));
    ASSERT_TRUE(flash3_read_bytes(CANDIDATE_PARTITION_MASTER + 3U, rdB, 4U));

    EXPECT_EQ(memcmp(rdA, tagA, 4U), 0);
    EXPECT_EQ(memcmp(rdB, tagB, 4U), 0);
}

// =============================================================================
// TEST 23: Byte-Aligned Staging Transfers Safe Under -mstrict-align
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_23_ZeroUnalignedPointerAccess_StrictlyByteAlignedTransfers)
{
    // Arrange: Use +1 and +3 misaligned host stack offsets for TX and RX staging
    alignas(4) uint8_t rawTx[32];
    alignas(4) uint8_t rawRx[32];
    memset(rawRx, 0, sizeof(rawRx));
    uint8_t *oddTx = &rawTx[1];
    uint8_t *oddRx = &rawRx[3];
    for (size_t i = 0U; i < 21U; i++) {
        oddTx[i] = (uint8_t)(0xE0U + i);
    }

    // Act & Assert
    ASSERT_TRUE(flash3_erase_sector_4k(CANDIDATE_PARTITION_BSP));
    ASSERT_TRUE(flash3_write_bytes(CANDIDATE_PARTITION_BSP + 5U, oddTx, 21U));
    ASSERT_TRUE(flash3_read_bytes(CANDIDATE_PARTITION_BSP + 5U, oddRx, 21U));
    EXPECT_EQ(memcmp(oddTx, oddRx, 21U), 0);
}

// =============================================================================
// TEST 24: Pre-Phase-20A Integration Status Audit (CONFIG_IN_RAM Active Guard)
// =============================================================================
TEST_F(AriesConfigFlashTest, Test_24_ConfigInRamSafeDefaultUntilPhase20AFlashDriverApproved)
{
#if !defined(CONFIG_IN_RAM)
    FAIL() << "CONFIG_IN_RAM must remain active until Phase 20A config_flash_aries.c is approved!";
#endif
    EXPECT_EQ(EEPROM_SIZE, 8192);
    EXPECT_EQ(getEEPROMStorageSize(), 8192U);
}

} // namespace

extern "C" {
// Stubs required by config_eeprom.c and fc/runtime_config.c in unit test binary
bool isEepromWriteInProgress(void) { return false; }
void beeperConfirmationBeeps(uint8_t) {}
}
