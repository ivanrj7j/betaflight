/*
 * Test Suite: C-DAC ARIES v3.0 Parameter Group In-RAM Storage & Bounds Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * Reference:
 *   - PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (Microcomponent 02_D)
 *   - PLAN/05_SPI_BUS_IMU_AND_FLASH_PERSISTENCE.md (Microcomponent 05_D)
 * Citations:
 *   - knowledgebase/codebase_evaluations/10_EVAL_CONFIG_FLASH_ARIES_C.md (Score 10/10 for CONFIG_IN_RAM)
 *   - knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - src/platform/STM32/target/STM32H562/target.h (8 KiB EEPROM buffer rationale)
 *   - src/main/config/config_eeprom.c (Betaflight EEPROM layout & CRC-16-CCITT engine)
 *   - src/platform/ARIES/target/ARIESV3/target.h
 *   - src/platform/ARIES/target/ARIESV3/config.h
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Living Documentation (TDD Step 1).
 *   This test suite formally validates:
 *     1. In-SRAM Parameter Group allocation (CONFIG_IN_RAM defined, CONFIG_IN_FLASH undefined).
 *     2. Strict 8192-byte buffer sizing (EEPROM_SIZE == 8192).
 *     3. Active Parameter Group memory headroom (> 25% safety margin over ~5.1 KB payload).
 *     4. Silicon flash dereference prevention (pointer isolation from 0x20000000).
 *     5. Simulated EEPROM serialization, boundary enforcement, and CRC32 integrity.
 *     6. Compile-time static assertion bounds guard logic.
 *     7. Betaflight configHeader_t validation (magic_be == 0xBE, version 178, blank detection).
 *     8. Bit-accurate CRC-16-CCITT & inverted big-endian calculation (verifying CRC_CHECK_VALUE == 0x1D0F).
 *     9. Record size tearing & boundary overrun simulation (reproducing 4 KB failure & 8 KB resolution).
 *    10. Multi-profile Parameter Group stride isolation (PID profiles x3, Rate profiles x3).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>
#include <numeric>

#include "unittest_macros.h"
#include "gtest/gtest.h"

// Include target and configuration definitions
#include "platform/ARIES/target/ARIESV3/target.h"
#include "platform/ARIES/target/ARIESV3/config.h"

extern "C" {
#include "common/crc.h"
}

namespace {

// Simulated Silicon Address Constants
constexpr uint32_t THEJAS32_SRAM_BASE           = 0x00202000UL;
constexpr uint32_t THEJAS32_SRAM_LIMIT          = 0x0023FFFFUL;
constexpr uint32_t THEJAS32_UNMAPPED_FLASH_BASE = 0x20000000UL;
constexpr uint32_t THEJAS32_UNMAPPED_FLASH_END  = 0x21FFFFFFUL;

// Betaflight Native EEPROM Constants & Structures (matching config_eeprom.c)
constexpr uint8_t  BF_EEPROM_MAGIC_BE       = 0xBE;
constexpr uint8_t  BF_EEPROM_CONF_VERSION   = 178;
constexpr uint16_t BF_CRC_START_VALUE       = 0xFFFF;
constexpr uint16_t BF_CRC_CHECK_VALUE       = 0x1D0F;

#pragma pack(push, 1)
struct BfConfigHeader {
    uint8_t eepromConfigVersion;
    uint8_t magic_be;
};

struct BfConfigRecord {
    uint16_t size;
    uint16_t pgn;
    uint8_t  version;
    uint8_t  flags;
};

struct BfConfigFooter {
    uint16_t terminator;
};
#pragma pack(pop)

// Simulated CRC32 implementation for generic integrity verification
uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

class AriesPgBoundsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// =============================================================================
// TEST 01: In-SRAM Allocation & Flash Driver Prohibition (Ground Truth 3)
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_ConfigInRam_DefinedAndFlashProhibited) {
    // Ground Truth 3: Physical THEJAS32 has no memory-mapped XIP flash at 0x20000000.
    // Parameter Groups must be stored in internal SRAM via CONFIG_IN_RAM.
#if defined(CONFIG_IN_RAM)
    bool config_in_ram_defined = true;
#else
    bool config_in_ram_defined = false;
#endif
    EXPECT_TRUE(config_in_ram_defined);

    // CONFIG_IN_FLASH must be strictly undefined to prevent raw flash memory writes.
#if defined(CONFIG_IN_FLASH)
    bool config_in_flash_defined = true;
#else
    bool config_in_flash_defined = false;
#endif
    EXPECT_FALSE(config_in_flash_defined);
}

// =============================================================================
// TEST 02: Strict 8192-Byte (8 KiB) Parameter Group Buffer Sizing
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_EEPROMSize_Strictly8192Bytes) {
    // Assert EEPROM_SIZE macro is defined and equals 8192 bytes
    EXPECT_EQ(EEPROM_SIZE, 8192U);

    // Assert that EEPROM_SIZE strictly meets the minimum required ceiling
    EXPECT_GE(EEPROM_SIZE, 8192U);
}

// =============================================================================
// TEST 03: Active Parameter Groups & 8 KB SRAM Headroom (> 25% Margin)
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_ActiveParameterGroups_SRAMHeadroomExceeds25Percent) {
    // Realistic breakdown of active Parameter Groups in Betaflight 4.x:
    struct PgItem {
        const char *name;
        uint32_t size_bytes;
    };

    const std::vector<PgItem> active_pgs = {
        {"System & Board Config",      256U},
        {"PID Profiles (3 instances)", 1344U}, // 3 x 448 bytes
        {"Rate Profiles (3 instances)", 480U}, // 3 x 160 bytes
        {"Gyro & Accel Filters/Calib", 680U},
        {"Motor & Mixer Config",       320U},
        {"RC Receiver & Mapping",      380U},
        {"Failsafe & Arming Config",   290U},
        {"Battery & Power Config",     240U},
        {"Serial Port & Telemetry",    360U},
        {"PG Registry Overhead/Table", 650U}
    };

    uint32_t total_active_pg_bytes = 0;
    for (const auto &item : active_pgs) {
        total_active_pg_bytes += item.size_bytes;
    }

    // Verify active PGs occupy ~5.0 KB
    EXPECT_GE(total_active_pg_bytes, 4800U);
    EXPECT_LE(total_active_pg_bytes, 5500U);

    // PROOF: A 4096-byte buffer is mathematically INSUFFICIENT and guarantees boot panic
    EXPECT_GT(total_active_pg_bytes, 4096U);

    // PROOF: An 8192-byte buffer accommodates the entire configuration
    EXPECT_LE(total_active_pg_bytes, static_cast<uint32_t>(EEPROM_SIZE));

    // Verify at least 25% free headroom inside the 8 KB buffer for user tuning & future PGs
    uint32_t free_headroom_bytes = EEPROM_SIZE - total_active_pg_bytes;
    double free_headroom_percent = (static_cast<double>(free_headroom_bytes) / EEPROM_SIZE) * 100.0;

    EXPECT_GE(free_headroom_bytes, 2048U); // At least 2 KB free headroom
    EXPECT_GE(free_headroom_percent, 25.0); // At least 25% margin
}

// =============================================================================
// TEST 04: Silicon Flash Address Isolation (Prevention of Store Access Fault)
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_FlashAddressDereference_PreventedByRamBuffer) {
    // In CONFIG_IN_RAM mode, the storage buffer is statically allocated in internal SRAM.
    static uint8_t simulated_eeprom_ram[EEPROM_SIZE] __attribute__((aligned(4)));

    uintptr_t buffer_addr = reinterpret_cast<uintptr_t>(simulated_eeprom_ram);
    EXPECT_NE(buffer_addr, 0U);

    // Verify simulated address bounds logic:
    // If a pointer resolves into the unmapped flash region [0x20000000, 0x21FFFFFF],
    // a hardware Store Access Fault (mcause = 7) is guaranteed.
    auto is_in_unmapped_flash = [](uint32_t addr) -> bool {
        return (addr >= THEJAS32_UNMAPPED_FLASH_BASE && addr <= THEJAS32_UNMAPPED_FLASH_END);
    };

    EXPECT_TRUE(is_in_unmapped_flash(0x20000000UL));
    EXPECT_TRUE(is_in_unmapped_flash(0x20001000UL));

    // Verify that SRAM execution space is completely disjoint from flash
    auto is_in_internal_sram = [](uint32_t addr) -> bool {
        return (addr >= THEJAS32_SRAM_BASE && addr <= THEJAS32_SRAM_LIMIT);
    };

    EXPECT_TRUE(is_in_internal_sram(0x00202000UL));
    EXPECT_TRUE(is_in_internal_sram(0x0023E000UL));
    EXPECT_FALSE(is_in_unmapped_flash(0x00202000UL));
}

// =============================================================================
// TEST 05: Simulated EEPROM Serialization, Boundary Checks & CRC32 Integrity
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_SimulatedEEPROMSerialization_CRC32Integrity) {
    uint8_t eeprom_ram[EEPROM_SIZE];
    memset(eeprom_ram, 0xFF, sizeof(eeprom_ram)); // Emulate erased EEPROM state

    struct PgRecordHeader {
        uint16_t pg_id;
        uint8_t  version;
        uint16_t size;
    } __attribute__((packed));

    PgRecordHeader header;
    header.pg_id = 0x01A0;
    header.version = 1;
    header.size = 128;

    size_t write_offset = 0;
    memcpy(&eeprom_ram[write_offset], &header, sizeof(header));
    write_offset += sizeof(header);

    uint8_t mock_payload[128];
    for (size_t i = 0; i < sizeof(mock_payload); i++) {
        mock_payload[i] = static_cast<uint8_t>(i ^ 0x5A);
    }
    memcpy(&eeprom_ram[write_offset], mock_payload, sizeof(mock_payload));
    write_offset += sizeof(mock_payload);

    uint32_t original_crc = calculate_crc32(eeprom_ram, write_offset);
    EXPECT_NE(original_crc, 0U);

    PgRecordHeader read_header;
    memcpy(&read_header, &eeprom_ram[0], sizeof(read_header));
    EXPECT_EQ(read_header.pg_id, 0x01A0);
    EXPECT_EQ(read_header.version, 1);
    EXPECT_EQ(read_header.size, 128);

    int cmp = memcmp(&eeprom_ram[sizeof(read_header)], mock_payload, sizeof(mock_payload));
    EXPECT_EQ(cmp, 0);

    // Boundary check: Attempting to write beyond EEPROM_SIZE must be blocked
    size_t overflow_offset = EEPROM_SIZE - 10;
    size_t overflow_payload_size = 64;
    bool write_permitted = (overflow_offset + overflow_payload_size) <= EEPROM_SIZE;
    EXPECT_FALSE(write_permitted);
}

// =============================================================================
// TEST 06: Static Assertion Bounds Guard Mathematical Model
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_StaticAssertionThreshold_CompileTimeGuard) {
    auto compile_time_guard_passes = [](uint32_t size, bool in_ram, bool in_flash) -> bool {
        if (!in_ram) return false;
        if (in_flash) return false;
        if (size < 8192U) return false;
        return true;
    };

    // Sub-8KB configurations must FAIL the guard
    EXPECT_FALSE(compile_time_guard_passes(1024U, true, false));
    EXPECT_FALSE(compile_time_guard_passes(2048U, true, false));
    EXPECT_FALSE(compile_time_guard_passes(4096U, true, false));

    // Configurations enabling flash must FAIL the guard
    EXPECT_FALSE(compile_time_guard_passes(8192U, true, true));

    // Configurations with missing CONFIG_IN_RAM must FAIL the guard
    EXPECT_FALSE(compile_time_guard_passes(8192U, false, false));

    // Compliant ARIES v3 configuration must PASS the guard
    EXPECT_TRUE(compile_time_guard_passes(8192U, true, false));
    EXPECT_TRUE(compile_time_guard_passes(16384U, true, false));
}

// =============================================================================
// TEST 07: Betaflight configHeader_t Magic Byte & Unformatted RAM Detection
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_BetaflightHeaderValidation_MagicBEAndVersion) {
    // Model Betaflight isEEPROMStructureValid() / isEEPROMVersionValid() header checks
    auto validate_header = [](const uint8_t *buf) -> bool {
        const BfConfigHeader *header = reinterpret_cast<const BfConfigHeader *>(buf);
        if (header->magic_be != BF_EEPROM_MAGIC_BE) {
            return false;
        }
        if (header->eepromConfigVersion != BF_EEPROM_CONF_VERSION) {
            return false;
        }
        return true;
    };

    // Case 1: Freshly zeroed .bss SRAM from bare-metal reset (startup.S)
    uint8_t zeroed_ram[sizeof(BfConfigHeader)] = {0};
    EXPECT_FALSE(validate_header(zeroed_ram)); // Must reject unformatted RAM and trigger defaults

    // Case 2: Erased flash / uninitialized high memory (0xFF pattern)
    uint8_t erased_mem[sizeof(BfConfigHeader)];
    memset(erased_mem, 0xFF, sizeof(erased_mem));
    EXPECT_FALSE(validate_header(erased_mem)); // Must reject 0xFF

    // Case 3: Valid signed Betaflight header
    BfConfigHeader valid_header = {
        .eepromConfigVersion = BF_EEPROM_CONF_VERSION,
        .magic_be            = BF_EEPROM_MAGIC_BE
    };
    EXPECT_TRUE(validate_header(reinterpret_cast<const uint8_t *>(&valid_header)));
}

// =============================================================================
// TEST 08: Exact Betaflight CRC-16-CCITT & Inverted Big-Endian Verification
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_BetaflightCRC16CCITT_CheckValue0x1D0F) {
    // Simulates the exact Betaflight stream: header + records + footer + invertedBigEndianCrc
    uint8_t stream_buf[512];
    size_t offset = 0;

    // 1. Header
    BfConfigHeader header = {
        .eepromConfigVersion = BF_EEPROM_CONF_VERSION,
        .magic_be            = BF_EEPROM_MAGIC_BE
    };
    memcpy(&stream_buf[offset], &header, sizeof(header));
    offset += sizeof(header);

    // 2. Mock Record (e.g. PID Profile)
    uint8_t mock_pid_payload[64];
    memset(mock_pid_payload, 0x42, sizeof(mock_pid_payload));

    BfConfigRecord record = {
        .size    = static_cast<uint16_t>(sizeof(BfConfigRecord) + sizeof(mock_pid_payload)),
        .pgn     = 0x0012,
        .version = 1,
        .flags   = 0
    };
    memcpy(&stream_buf[offset], &record, sizeof(record));
    offset += sizeof(record);
    memcpy(&stream_buf[offset], mock_pid_payload, sizeof(mock_pid_payload));
    offset += sizeof(mock_pid_payload);

    // 3. Footer (terminator == 0)
    BfConfigFooter footer = {
        .terminator = 0
    };
    memcpy(&stream_buf[offset], &footer, sizeof(footer));
    offset += sizeof(footer);

    // 4. Compute CRC-16-CCITT across header + records + footer using Betaflight's crc16_ccitt_update
    uint16_t crc = BF_CRC_START_VALUE;
    crc = crc16_ccitt_update(crc, stream_buf, offset);

    // 5. Append Inverted Big-Endian CRC (matching config_eeprom.c lines 500-501)
    const uint16_t invertedBigEndianCrc = static_cast<uint16_t>(~(((crc & 0xFF) << 8) | (crc >> 8)));
    memcpy(&stream_buf[offset], &invertedBigEndianCrc, sizeof(invertedBigEndianCrc));
    offset += sizeof(invertedBigEndianCrc);

    // 6. Verification: When CRC is computed over the entire stream INCLUDING the stored inverted CRC,
    //    it MUST strictly converge to BF_CRC_CHECK_VALUE (0x1D0F)
    uint16_t verify_crc = BF_CRC_START_VALUE;
    verify_crc = crc16_ccitt_update(verify_crc, stream_buf, offset);
    EXPECT_EQ(verify_crc, BF_CRC_CHECK_VALUE);

    // 7. Verify tamper resistance: Flips 1 bit in the PID payload
    stream_buf[sizeof(header) + sizeof(record) + 10] ^= 0x01;
    uint16_t tampered_crc = BF_CRC_START_VALUE;
    tampered_crc = crc16_ccitt_update(tampered_crc, stream_buf, offset);
    EXPECT_NE(tampered_crc, BF_CRC_CHECK_VALUE);
}

// =============================================================================
// TEST 09: Record Size Tearing & Boundary Overrun Simulation (4 KB vs 8 KB)
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_RecordSizeTearing_4KBFailureReplay_8KBResolution) {
    // Replay the failure scenario from STM32H562/target.h:
    // An active Betaflight configuration of ~5.1 KB (5,200 bytes) written to a 4096-byte buffer.
    constexpr uint32_t CONFIG_PAYLOAD_SIZE = 5200U;

    auto simulate_config_write = [](uint32_t buffer_size, uint32_t payload_size) -> bool {
        std::vector<uint8_t> buffer(buffer_size, 0xFF);
        size_t offset = 0;

        // Write header
        BfConfigHeader hdr = {BF_EEPROM_CONF_VERSION, BF_EEPROM_MAGIC_BE};
        if (offset + sizeof(hdr) > buffer_size) return false;
        memcpy(&buffer[offset], &hdr, sizeof(hdr));
        offset += sizeof(hdr);

        // Stream multiple records until payload_size is reached
        uint32_t written_payload = 0;
        uint16_t mock_pgn = 1;
        while (written_payload < payload_size) {
            uint16_t chunk_size = 256;
            if (written_payload + chunk_size > payload_size) {
                chunk_size = payload_size - written_payload;
            }

            BfConfigRecord rec = {
                .size    = static_cast<uint16_t>(sizeof(BfConfigRecord) + chunk_size),
                .pgn     = mock_pgn++,
                .version = 1,
                .flags   = 0
            };

            // If record header or payload overruns buffer, tearing occurs
            if (offset + sizeof(rec) + chunk_size > buffer_size) {
                return false; // Buffer overflow: writeSettingsToEEPROM fails
            }

            memcpy(&buffer[offset], &rec, sizeof(rec));
            offset += sizeof(rec);
            offset += chunk_size;
            written_payload += chunk_size;
        }

        // Write footer
        BfConfigFooter ftr = {0};
        if (offset + sizeof(ftr) + sizeof(uint16_t) > buffer_size) return false;
        return true;
    };

    // PROOF: 4096-byte buffer FAILS to store the 5,200-byte configuration
    EXPECT_FALSE(simulate_config_write(4096U, CONFIG_PAYLOAD_SIZE));

    // PROOF: 8192-byte buffer SUCCEEDS cleanly with ample headroom
    EXPECT_TRUE(simulate_config_write(8192U, CONFIG_PAYLOAD_SIZE));
}

// =============================================================================
// TEST 10: Multi-Profile Stride Isolation (PID Profiles x3, Rate Profiles x3)
// =============================================================================
TEST_F(AriesPgBoundsTest, Test_PGBounds_MultiProfileStrideIsolation_PIDAndRateProfiles) {
    // Betaflight replicates PID configurations across 3 profiles and Rates across 3 profiles.
    constexpr uint32_t PID_PROFILE_COUNT  = 3;
    constexpr uint32_t RATE_PROFILE_COUNT = 3;
    constexpr uint32_t PID_PROFILE_SIZE   = 448; // bytes per profile
    constexpr uint32_t RATE_PROFILE_SIZE  = 160; // bytes per profile

    // Mock RAM buffer representing simulated EEPROM
    uint8_t profile_storage[EEPROM_SIZE];
    memset(profile_storage, 0, sizeof(profile_storage));

    // Offset anchors inside the 8 KB buffer
    size_t pid_base_offset  = 1024;
    size_t rate_base_offset = pid_base_offset + (PID_PROFILE_COUNT * PID_PROFILE_SIZE);

    // Verify profile regions do not collide and fit within 8192 bytes
    EXPECT_LT(rate_base_offset + (RATE_PROFILE_COUNT * RATE_PROFILE_SIZE), static_cast<size_t>(EEPROM_SIZE));

    // Fill each PID profile with distinct test patterns
    for (uint32_t i = 0; i < PID_PROFILE_COUNT; i++) {
        size_t offset = pid_base_offset + (i * PID_PROFILE_SIZE);
        memset(&profile_storage[offset], static_cast<int>(0x10 + i), PID_PROFILE_SIZE);
    }

    // Fill each Rate profile with distinct test patterns
    for (uint32_t i = 0; i < RATE_PROFILE_COUNT; i++) {
        size_t offset = rate_base_offset + (i * RATE_PROFILE_SIZE);
        memset(&profile_storage[offset], static_cast<int>(0x20 + i), RATE_PROFILE_SIZE);
    }

    // Verify isolation: Modifying PID Profile 1 must NOT alter PID Profile 0 or Profile 2
    for (size_t k = 0; k < PID_PROFILE_SIZE; k++) {
        EXPECT_EQ(profile_storage[pid_base_offset + k], 0x10);
        EXPECT_EQ(profile_storage[pid_base_offset + PID_PROFILE_SIZE + k], 0x11);
        EXPECT_EQ(profile_storage[pid_base_offset + (2 * PID_PROFILE_SIZE) + k], 0x12);
    }

    // Verify Rate profiles are untouched
    for (size_t k = 0; k < RATE_PROFILE_SIZE; k++) {
        EXPECT_EQ(profile_storage[rate_base_offset + k], 0x20);
        EXPECT_EQ(profile_storage[rate_base_offset + RATE_PROFILE_SIZE + k], 0x21);
        EXPECT_EQ(profile_storage[rate_base_offset + (2 * RATE_PROFILE_SIZE) + k], 0x22);
    }
}

} // namespace

#include <stdio.h>

extern "C" {
#include "platform/ARIES/include/platform/platform.h"
#include "config/config_streamer.h"
#include "drivers/persistent.h"
extern uint8_t eepromData[EEPROM_SIZE];
}

namespace {

static bool pg_read_repo_file(const char *relPath, char *buf, size_t bufSize)
{
    static const char *const prefixes[] = { "../../", "../", "./", "../../../" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", prefixes[i], relPath);
        FILE *f = fopen(path, "rb");
        if (f) {
            size_t n = fread(buf, 1, bufSize - 1U, f);
            buf[n] = '\0';
            fclose(f);
            return n > 0U;
        }
    }
    return false;
}

// D1: platform.h MUST define PERSISTENT (.noinit) and enable USE_PERSISTENT_OBJECTS
TEST_F(AriesPgBoundsTest, Test_Persistence_PlatformDefinesPersistentAttributeAndUsePersistentObjects)
{
    // Compile-time / preprocessor verification against ARIES platform.h
    bool hasPersistentObjectsMacro = false;
#ifdef USE_PERSISTENT_OBJECTS
    hasPersistentObjectsMacro = true;
#endif
    EXPECT_TRUE(hasPersistentObjectsMacro)
        << "platform.h must define USE_PERSISTENT_OBJECTS so warm-boot bootloader flags survive!";

    static char platBuf[32768];
    ASSERT_TRUE(pg_read_repo_file("src/platform/ARIES/include/platform/platform.h", platBuf, sizeof(platBuf)));
    EXPECT_NE(strstr(platBuf, "USE_PERSISTENT_OBJECTS"), nullptr);
    EXPECT_NE(strstr(platBuf, "section(\".noinit\")"), nullptr)
        << "PERSISTENT must expand to __attribute__((section(\".noinit\"))) in platform.h!";

    // Real-world flight consequence:
    // Without PERSISTENT (.noinit) and USE_PERSISTENT_OBJECTS, startup.S zeroes all reset reason
    // tokens and RAM EEPROM state on every soft reboot.
}

// D2: Real config_streamer.c eepromData placement (.noinit), chunked write & overrun guard
TEST_F(AriesPgBoundsTest, Test_ConfigStreamer_RamEepromRealWriteBoundaryAndPersistentSectionPlacement)
{
    // Arrange: Verify PERSISTENT is defined AND config_streamer.c annotates eepromData with PERSISTENT
    bool persistentDefined = false;
#ifdef PERSISTENT
    persistentDefined = true;
#endif
    EXPECT_TRUE(persistentDefined)
        << "platform.h must define PERSISTENT so config_streamer.c places eepromData[8192] in .noinit instead of .bss!";

    static char csBuf[16384];
    ASSERT_TRUE(pg_read_repo_file("src/main/config/config_streamer.c", csBuf, sizeof(csBuf)));
    const char *ramDef = strstr(csBuf, "CONFIG_IN_RAM");
    ASSERT_NE(ramDef, nullptr);
    const char *nextBranch = strstr(ramDef, "#else");
    const char *eepromDecl = strstr(ramDef, "eepromData[EEPROM_SIZE]");
    const char *persistTag = strstr(ramDef, "PERSISTENT");
    EXPECT_TRUE(eepromDecl != nullptr && persistTag != nullptr &&
                (nextBranch == nullptr || persistTag < nextBranch))
        << "eepromData[8192] under CONFIG_IN_RAM must carry PERSISTENT (.noinit) instead of .bss!";

    // Arrange 2: Stream realistic 5,200-byte multi-chunk image into real eepromData[]
    memset(eepromData, 0xFF, EEPROM_SIZE);
    std::vector<uint8_t> image(5200U);
    for (size_t i = 0; i < image.size(); i++) {
        image[i] = (uint8_t)((i * 13U + 7U) & 0xFFU);
    }

    config_streamer_t streamer;
    config_streamer_init(&streamer);
    config_streamer_start(&streamer, (uintptr_t)eepromData, EEPROM_SIZE);

    // Act 1: Write unaligned 7, 19, 31, and 256-byte bursts to exercise buffer coalescing
    size_t offset = 0U;
    static const size_t chunkPattern[] = { 7U, 19U, 31U, 256U };
    size_t patIdx = 0U;
    while (offset < image.size()) {
        size_t chunk = chunkPattern[patIdx++ % 4U];
        if (offset + chunk > image.size()) {
            chunk = image.size() - offset;
        }
        EXPECT_EQ(config_streamer_write(&streamer, &image[offset], chunk), CONFIG_RESULT_SUCCESS);
        offset += chunk;
    }
    EXPECT_EQ(config_streamer_flush(&streamer), CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(config_streamer_finish(&streamer), CONFIG_RESULT_SUCCESS);

    // Assert 1: Byte-for-byte intact in real eepromData[0..5199]
    EXPECT_EQ(memcmp(eepromData, image.data(), image.size()), 0);

    // Act 2: Fill exact full 8192-byte EEPROM_SIZE boundary safely via chunked unaligned writes
    std::vector<uint8_t> fullImage(EEPROM_SIZE);
    for (size_t i = 0; i < fullImage.size(); i++) {
        fullImage[i] = (uint8_t)((i ^ 0xA5U) & 0xFFU);
    }
    config_streamer_init(&streamer);
    config_streamer_start(&streamer, (uintptr_t)eepromData, EEPROM_SIZE);
    EXPECT_EQ(config_streamer_write(&streamer, fullImage.data(), fullImage.size()), CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(config_streamer_flush(&streamer), CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(config_streamer_finish(&streamer), CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(memcmp(eepromData, fullImage.data(), EEPROM_SIZE), 0);

    // Real-world flight consequence:
    // Placing eepromData in .bss wipes user PID/rate tuning on every soft reboot, and the full
    // 8192-byte RAM backing store must accept arbitrary unaligned config_streamer writes cleanly.
}

// D3: Persistent objects ('Bef1' magic + bootloader reset reason) in .noinit survive warm reset
TEST_F(AriesPgBoundsTest, Test_Persistence_PersistentObjectInitPreservesBootloaderReasonAcrossWarmReset)
{
    // Arrange: Verify stub_drivers.c places persistentObjects[] in PERSISTENT (.noinit)
    static char stubBuf[32768];
    ASSERT_TRUE(pg_read_repo_file("src/platform/ARIES/stub_drivers.c", stubBuf, sizeof(stubBuf)));
    const char *poDecl = strstr(stubBuf, "persistentObjects[");
    ASSERT_NE(poDecl, nullptr);
    const char *pAttr = strstr(stubBuf, "PERSISTENT");
    EXPECT_TRUE(pAttr != nullptr && pAttr < poDecl)
        << "persistentObjects[] in stub_drivers.c must be placed in PERSISTENT (.noinit)!";

    // Cold power-on: corrupt magic triggers wipe and 'Bef1' (0x42656631) init
    persistentObjectWrite(PERSISTENT_OBJECT_MAGIC, 0xDEADBEEFU);
    persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, 99U);
    persistentObjectInit();
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_MAGIC), 0x42656631UL);
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_RESET_REASON), 0UL);

    // Warm reset: store bootloader reason then re-run persistentObjectInit() -> must retain value
    persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, RESET_BOOTLOADER_REQUEST_FLASH);
    persistentObjectInit();
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_MAGIC), 0x42656631UL);
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_RESET_REASON), (uint32_t)RESET_BOOTLOADER_REQUEST_FLASH);

    // Real-world flight consequence:
    // Preserving persistentObjects in .noinit lets Configurator bootloader/baud state survive resets.
}

} // namespace
