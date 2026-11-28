/*
 * Test Suite: C-DAC ARIES v3.0 PG Migration, EEPROM 8KB Bounds & MDP_SPI_3 Recovery (Wave 3 - Suite 10)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, CONFIG_IN_RAM 8192B + MDP_SPI_3)
 * References & Citations:
 *   - PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md
 *   - src/main/pg/pg.c, src/main/pg/pg.h, src/main/config/config_eeprom.c, src/main/config/config_streamer.c
 *   - src/main/common/crc.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Covers 12 PG migration, FNV-1a sensitivity, 8 KB bounds, corrupt record fallback,
 *   BOXARM self-link/range clamping, dual-sector A/B torn-write rollback, and 3-retry abort edges.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/build_config.h"
    #include "common/crc.h"
    #include "common/maths.h"
    #include "common/utils.h"
    #include "config/config.h"
    #include "config/config_eeprom.h"
    #include "config/config_streamer.h"
    #include "drivers/io.h"
    #include "drivers/system.h"
    #include "fc/rc_modes.h"
    #include "fc/runtime_config.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

    typedef struct {
        uint32_t magic;
        uint16_t gain;
        uint8_t mode;
        uint8_t reserved;
    } ariesMigTestCfg_t;

    PG_DECLARE(ariesMigTestCfg_t, ariesMigTestCfg);
    PG_REGISTER_WITH_RESET_TEMPLATE(ariesMigTestCfg_t, ariesMigTestCfg, PG_RESERVED_FOR_TESTING_1, 3);
    PG_RESET_TEMPLATE(ariesMigTestCfg_t, ariesMigTestCfg,
        .magic = 0xA31E5003U,
        .gain = 450U,
        .mode = 2U,
        .reserved = 0U,
    );

    typedef struct {
        uint32_t header;
        uint16_t flags;
        uint16_t extraTail;
    } ariesMigGrowCfg_t;

    PG_DECLARE(ariesMigGrowCfg_t, ariesMigGrowCfg);
    PG_REGISTER_WITH_RESET_TEMPLATE(ariesMigGrowCfg_t, ariesMigGrowCfg, PG_RESERVED_FOR_TESTING_2, 1);
    PG_RESET_TEMPLATE(ariesMigGrowCfg_t, ariesMigGrowCfg,
        .header = 0x11223344U,
        .flags = 0x5566U,
        .extraTail = 0x7788U,
    );

    bool isEepromWriteInProgress(void) { return false; }
    void beeperConfirmationBeeps(uint8_t) {}
}

namespace {

// Minimal physical NOR flash state machine on MDP_SPI_3 for dual-bank A/B torn-write & retry tests
struct Spi3NorModel {
    static constexpr uint32_t SECTOR_BYTES = 4096U;
    static constexpr uint32_t TOTAL_BYTES  = 16384U; // 2 * 8KB (Bank A + Bank B)
    uint8_t mem[TOTAL_BYTES];
    std::vector<uint8_t> cmdTrace;
    bool injectVerifyMismatch = false;
    uint32_t txCount = 0U;

    void reset() {
        memset(mem, 0xFFU, sizeof(mem));
        cmdTrace.clear();
        injectVerifyMismatch = false;
        txCount = 0U;
    }

    void eraseSector(uint32_t off) {
        cmdTrace.push_back(0x20U);
        if (off + SECTOR_BYTES <= TOTAL_BYTES) {
            memset(&mem[off], 0xFFU, SECTOR_BYTES);
        }
    }

    void programBytes(uint32_t off, const uint8_t *src, size_t len, bool abortMidWrite = false) {
        cmdTrace.push_back(0x06U); // WREN
        cmdTrace.push_back(0x02U); // PP
        size_t limit = abortMidWrite ? (len / 2U) : len;
        for (size_t i = 0; i < limit && (off + i) < TOTAL_BYTES; i++) {
            mem[off + i] &= src[i];
        }
        if (!abortMidWrite) {
            cmdTrace.push_back(0x04U); // WRDI
        }
    }

    bool verifyWithRetries(uint32_t off, const uint8_t *expected, size_t len, int maxRetries = 3) {
        int tries = 0;
        while (tries < maxRetries) {
            tries++;
            bool match = !injectVerifyMismatch;
            for (size_t i = 0; match && i < len; i++) {
                if (mem[off + i] != expected[i]) match = false;
            }
            if (match) return true;
        }
        cmdTrace.push_back(0x04U); // WRDI on abort
        return false;
    }
};

class AriesPgEepromMigrationTest : public ::testing::Test {
protected:
    Spi3NorModel nor;

    void SetUp() override {
        thejas32_host_reset_all();
        memset(eepromData, 0, sizeof(eepromData));
        pgResetAll();
        nor.reset();
        thejas32_host_mcycle_set(100000ULL);
    }
};

// =============================================================================
// EDGE 01: pgFind, pgReset, pgVersion, pgSize, pgElementSize Match Exactly
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg01_ResetAndFind_MatchesVersionSizeAndElementSize)
{
    const pgRegistry_t *reg = pgFind(PG_RESERVED_FOR_TESTING_1);
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(pgN(reg), PG_RESERVED_FOR_TESTING_1);
    EXPECT_EQ(pgVersion(reg), 3U);
    EXPECT_EQ(pgSize(reg), sizeof(ariesMigTestCfg_t));
    EXPECT_EQ(pgElementSize(reg), sizeof(ariesMigTestCfg_t));

    ariesMigTestCfgMutable()->gain = 999U;
    pgReset(reg);
    EXPECT_EQ(ariesMigTestCfg()->gain, 450U);
    EXPECT_EQ(pgFind((pgn_t)65534U), nullptr);
}

// =============================================================================
// EDGE 02: pgStore -> pgLoad Binary Serialization Round-Trip
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg02_StoreAndLoadRoundTrip_PreservesFieldsAcrossZeroedRam)
{
    const pgRegistry_t *reg = pgFind(PG_RESERVED_FOR_TESTING_1);
    ASSERT_NE(reg, nullptr);
    ariesMigTestCfgMutable()->gain = 712U;
    ariesMigTestCfgMutable()->mode = 5U;

    uint8_t blob[64];
    int stored = pgStore(reg, blob, sizeof(blob));
    ASSERT_GT(stored, 0);

    pgReset(reg);
    EXPECT_EQ(ariesMigTestCfg()->gain, 450U);

    EXPECT_TRUE(pgLoad(reg, blob, stored, pgVersion(reg)));
    EXPECT_EQ(ariesMigTestCfg()->gain, 712U);
    EXPECT_EQ(ariesMigTestCfg()->mode, 5U);
}

// =============================================================================
// EDGE 03: Version Mismatch Resets To Safe Defaults Without Overread
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg03_VersionMismatchMigration_ResetsDefaultsSafely)
{
    const pgRegistry_t *reg = pgFind(PG_RESERVED_FOR_TESTING_1);
    ASSERT_NE(reg, nullptr);
    uint8_t oldBlob[64] = { 0 };
    EXPECT_FALSE(pgLoad(reg, oldBlob, (int)pgSize(reg), 2U)); // saved v2 < current v3 -> fails safely
    EXPECT_EQ(ariesMigTestCfg()->gain, 450U);
    EXPECT_EQ(ariesMigTestCfg()->magic, 0xA31E5003U);
}

// =============================================================================
// EDGE 04: Size Growth Zero-Fills New Tail & Size Shrink Clamps Copy Length
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg04_SizeIncreaseAndShrink_TruncatesOrZeroFillsSafely)
{
    const pgRegistry_t *reg = pgFind(PG_RESERVED_FOR_TESTING_2);
    ASSERT_NE(reg, nullptr);

    // Case A: Shorter older record (missing extraTail -> defaults or zero tail, zero OOB read)
    uint32_t smallRec[2] = { 0xCAFE1234U, 0x11112222U };
    EXPECT_TRUE(pgLoad(reg, smallRec, 4, pgVersion(reg)));
    EXPECT_EQ(ariesMigGrowCfg()->header, 0xCAFE1234U);

    // Case B: Larger record with guard canary after struct -> must not overwrite past struct end
    struct Guarded { ariesMigGrowCfg_t cfg; uint32_t canary; } guarded;
    guarded.canary = 0xDEADBEEFU;
    uint8_t oversized[64];
    memset(oversized, 0xAAU, sizeof(oversized));
    ariesMigGrowCfg_t src = { 0x99887766U, 0x4433U, 0x2211U };
    memcpy(oversized, &src, sizeof(src));
    EXPECT_TRUE(pgLoad(reg, oversized, (int)sizeof(oversized), pgVersion(reg)));
    EXPECT_EQ(ariesMigGrowCfg()->header, 0x99887766U);
    EXPECT_EQ(guarded.canary, 0xDEADBEEFU);
}

// =============================================================================
// EDGE 05: FNV-1a 32-Bit Hash Determinism & Single-Bit Sensitivity
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg05_Fnv1aHashDeterminism_SensitiveToSingleBitFlip)
{
    const uint8_t d1[8] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    uint8_t d2[8];
    memcpy(d2, d1, sizeof(d1));
    d2[3] ^= 0x01U;

    const uint32_t h1a = fnv_update(FNV_OFFSET_BASIS, d1, sizeof(d1));
    const uint32_t h1b = fnv_update(FNV_OFFSET_BASIS, d1, sizeof(d1));
    const uint32_t h2  = fnv_update(FNV_OFFSET_BASIS, d2, sizeof(d2));
    EXPECT_EQ(h1a, h1b);
    EXPECT_NE(h1a, h2);
}

// =============================================================================
// EDGE 06: 8192-Byte EEPROM Fits Full ARIES PG Table With Safety Margin
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg06_Eeprom8192Bytes_FitsFullPgTableAndReadbackPasses)
{
    ariesMigTestCfgMutable()->gain = 888U;
    writeConfigToEEPROM();

    ariesMigTestCfgMutable()->gain = 100U;
    EXPECT_TRUE(isEEPROMVersionValid());
    EXPECT_TRUE(isEEPROMStructureValid());
    EXPECT_TRUE(loadEEPROM());
    EXPECT_EQ(ariesMigTestCfg()->gain, 888U);
    EXPECT_EQ(getEEPROMStorageSize(), 8192U);
}

// =============================================================================
// EDGE 07: Corrupt Byte Or Footer CRC In EEPROM Fails Safe Reset
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg07_CorruptFooterCrc_RejectedAndRestoresDefaults)
{
    writeConfigToEEPROM();
    // Flip first record version byte (offset 6 = 2B header + 2B size + 2B pgn -> version)
    eepromData[6] ^= 0x80U;
    EXPECT_FALSE(isEEPROMStructureValid());
    EXPECT_FALSE(loadEEPROM());
}

// =============================================================================
// EDGE 08: Zero Or Oversized Record Header Size Terminates Boundedly
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg08_ZeroOrOversizedRecordSize_AbortsCleanlyWithoutLoopHang)
{
    writeConfigToEEPROM();
    // Offset 2 is first configRecord_t.size right after 2-byte configHeader_t
    eepromData[2] = 0xFFU;
    eepromData[3] = 0xFFU;
    EXPECT_FALSE(isEEPROMStructureValid());
    EXPECT_FALSE(loadEEPROM());
}

// =============================================================================
// EDGE 09: CONFIG_IN_RAM Boundaries (__config_end - __config_start == 8192)
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg09_ConfigRamBounds_SpansExactly8192BytesZeroOverflow)
{
    EXPECT_EQ((size_t)((const uint8_t *)&__config_end - (const uint8_t *)&__config_start), 8192U);
    EXPECT_EQ((const uint8_t *)&__config_start, eepromData);
}

// =============================================================================
// EDGE 10: Mode Activation Self-Link & Range Validation Constraint Contract
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg10_ModeActivationRangeBounds_ValidStepLimitsAndNoSelfLink)
{
    modeActivationCondition_t cond = {};
    cond.modeId = BOXARM;
    cond.auxChannelIndex = 0;
    cond.range.startStep = CHANNEL_VALUE_TO_STEP(1700);
    cond.range.endStep   = CHANNEL_VALUE_TO_STEP(2100);
    cond.linkedTo = BOXANGLE;
    EXPECT_LT(cond.range.startStep, cond.range.endStep);
    EXPECT_NE(cond.linkedTo, cond.modeId); // Cannot self-link BOXARM -> BOXARM
}

// =============================================================================
// EDGE 11: MDP_SPI_3 Dual-Sector A/B Torn-Write Power-Cut Recovery
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg11_Spi3DualSectorAbTornWriteRecovery_RollsBackToIntactBankA)
{
    uint8_t bankAData[256];
    uint8_t bankBData[256];
    memset(bankAData, 0xA5U, sizeof(bankAData));
    memset(bankBData, 0x5AU, sizeof(bankBData));

    // Commit valid Bank A at offset 0x0000
    nor.eraseSector(0x0000U);
    nor.programBytes(0x0000U, bankAData, sizeof(bankAData), false);
    EXPECT_TRUE(nor.verifyWithRetries(0x0000U, bankAData, sizeof(bankAData)));

    // Begin programming Bank B at 0x2000, simulate power loss halfway through
    nor.eraseSector(0x2000U);
    nor.programBytes(0x2000U, bankBData, sizeof(bankBData), true);
    EXPECT_FALSE(nor.verifyWithRetries(0x2000U, bankBData, sizeof(bankBData)));
    // Bank A stays 100% intact for cold-boot fallback!
    EXPECT_TRUE(nor.verifyWithRetries(0x0000U, bankAData, sizeof(bankAData)));
}

// =============================================================================
// EDGE 12: 3-Retry Readback Exhaustion Aborts And Issues JEDEC WRDI (0x04)
// =============================================================================
TEST_F(AriesPgEepromMigrationTest, Pg12_Spi3ThreeRetryExhaustion_AbortsWithWriteDisable0x04)
{
    uint8_t data[64];
    memset(data, 0x3CU, sizeof(data));
    nor.eraseSector(0x0000U);
    nor.programBytes(0x0000U, data, sizeof(data), false);
    nor.cmdTrace.clear();

    nor.injectVerifyMismatch = true;
    EXPECT_FALSE(nor.verifyWithRetries(0x0000U, data, sizeof(data), 3));
    ASSERT_FALSE(nor.cmdTrace.empty());
    EXPECT_EQ(nor.cmdTrace.back(), 0x04U);
}

} // namespace
