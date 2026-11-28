/*
 * Test Suite: C-DAC ARIES v3.0 Silicon Startup, .noinit, Flat INTR_CTRL & Alignment Edges (Wave 3 - Suite 14)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 0 RVC, -mstrict-align)
 * References & Citations:
 *   - src/platform/ARIES/startup.S, link/aries_v3.ld, system.c, stub_drivers.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Covers all 6 low-level startup, dirty .noinit recovery, multi-IRQ simultaneous dispatch,
 *   stub_drivers null safety, clean systemReset teardown, and strict alignment edges.
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
    #include "common/crc.h"
    #include "drivers/adc.h"
    #include "drivers/io.h"
    #include "drivers/persistent.h"
    #include "drivers/system.h"
    #include "msp/msp_protocol.h"
    #include "msp/msp_protocol_v2_common.h"
    #include "msp/msp_serial.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

    uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
    void thejas32_interrupt_register(uint8_t irq, thejas32_irq_handler_t handler);
}

namespace {

static bool runtime_read_repo_file(const char *relPath, char *buf, size_t bufSize)
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

static std::vector<uint8_t> s_irqDispatchOrder;
static void isrUart0Test(void)  { s_irqDispatchOrder.push_back(THEJAS32_IRQ_UART0); }
static void isrTimer1Test(void) { s_irqDispatchOrder.push_back(THEJAS32_IRQ_TIMER1); }
static void isrGpio1Test(void)  { s_irqDispatchOrder.push_back(THEJAS32_IRQ_GPIO1); }

class AriesSiliconRuntimeEdgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        s_irqDispatchOrder.clear();
        thejas32_host_mcycle_set(100000ULL);
    }
};

// =============================================================================
// EDGE 01: startup.S .data Copy & .bss Word-Loop Bounds (4-Byte Aligned)
// =============================================================================
TEST_F(AriesSiliconRuntimeEdgeTest, Runtime01_StartupWordLoopsAndBssBounds_4ByteAligned)
{
    static char sBuf[16384];
    static char ldBuf[16384];
    ASSERT_TRUE(runtime_read_repo_file("src/platform/ARIES/startup.S", sBuf, sizeof(sBuf)));
    ASSERT_TRUE(runtime_read_repo_file("src/platform/ARIES/link/aries_v3.ld", ldBuf, sizeof(ldBuf)));

    EXPECT_NE(strstr(sBuf, "beq t0, t1, copy_done"), nullptr); // In-SRAM LMA==VMA fast bypass
    EXPECT_NE(strstr(sBuf, "addi t0, t0, 4"), nullptr);
    EXPECT_NE(strstr(sBuf, "zero_bss_loop:"), nullptr);
    EXPECT_NE(strstr(ldBuf, "_sbss = .;"), nullptr);
    EXPECT_NE(strstr(ldBuf, ". = ALIGN(4);"), nullptr);
}

// =============================================================================
// EDGE 02: Dirty Uninitialized SRAM In .noinit Recovered On Cold Power-On
// =============================================================================
TEST_F(AriesSiliconRuntimeEdgeTest, Runtime02_DirtyNoinitColdSram_ReinitializesBadMagicCleanly)
{
    // Cold SRAM powers up with arbitrary bit pattern across all persistent objects
    for (int id = 0; id < PERSISTENT_OBJECT_COUNT; id++) {
        persistentObjectWrite((persistentObjectId_e)id, 0xA5A55A5AU);
    }
    systemInit();
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_MAGIC), 0x42656631U);
    EXPECT_EQ(persistentObjectRead(PERSISTENT_OBJECT_RESET_REASON), 0U);
}

// =============================================================================
// EDGE 03: Flat INTR_CTRL Simultaneous UART0 + TIMER1 + GPIO1 Single-Trap Dispatch
// =============================================================================
TEST_F(AriesSiliconRuntimeEdgeTest, Runtime03_SimultaneousMultiIrq_DispatchesAllPendingInSingleTrap)
{
    thejas32_interrupt_register(THEJAS32_IRQ_UART0,  isrUart0Test);
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER1, isrTimer1Test);
    thejas32_interrupt_register(THEJAS32_IRQ_GPIO1,  isrGpio1Test);

    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_UART0) |
                                     (1UL << THEJAS32_IRQ_TIMER1) |
                                     (1UL << THEJAS32_IRQ_GPIO1);

    handle_trap(0x8000000BUL, 0U, nullptr);

    ASSERT_EQ(s_irqDispatchOrder.size(), 3U);
    // __ctzsi2 dispatches lowest bit first: IRQ 0 (UART0), IRQ 8 (TIMER1), IRQ 11 (GPIO1)
    EXPECT_EQ(s_irqDispatchOrder[0], THEJAS32_IRQ_UART0);
    EXPECT_EQ(s_irqDispatchOrder[1], THEJAS32_IRQ_TIMER1);
    EXPECT_EQ(s_irqDispatchOrder[2], THEJAS32_IRQ_GPIO1);
}

// =============================================================================
// EDGE 04: stub_drivers.c Safe Null / Zero Defaults Prevent Null Pointer Panics
// =============================================================================
TEST_F(AriesSiliconRuntimeEdgeTest, Runtime04_StubDriversSafeNullReturns_NoPanicOnOptionalApis)
{
    EXPECT_EQ(adcGetValue(ADC_BATTERY), 0U);
    EXPECT_EQ(adcGetValue(ADC_CURRENT), 0U);
    EXPECT_EQ(adcGetValue(ADC_RSSI), 0U);
}

// =============================================================================
// EDGE 05: systemReset Full Teardown Silences Timer1, Motors & All IRQs First
// =============================================================================
TEST_F(AriesSiliconRuntimeEdgeTest, Runtime05_SystemResetFullShutdown_DisablesMotorsTimerAndIrqs)
{
    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    THEJAS32_TIMER(1).Control = 0x03U;
    *thejas32_gpio_data_ptr(1U) = 1U;
    *thejas32_gpio_data_ptr(4U) = 1U;
    thejas32_host_mstatus = 0x8U;

    systemReset();

    EXPECT_EQ(THEJAS32_TIMER(1).Control, 0U);
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN, 0U);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
    EXPECT_EQ(*thejas32_gpio_data_ptr(1U), 0U);
    EXPECT_EQ(*thejas32_gpio_data_ptr(4U), 0U);
    EXPECT_EQ(thejas32_host_reset_count, 1U);
}

// =============================================================================
// EDGE 06: -mstrict-align Packed Wire Protocol Header Alignment Safety
// =============================================================================
TEST_F(AriesSiliconRuntimeEdgeTest, Runtime06_StrictAlignPackedProtocolStructs_DecodedSafely)
{
    uint8_t rawUnaligned[16] = { 0x00, 0x01, 0x02, 0x2A, 0x08, 0x00 }; // Starts at offset +1 (odd address!)
    mspHeaderV2_t hdr;
    memcpy(&hdr, &rawUnaligned[1], sizeof(hdr));
    EXPECT_EQ(hdr.flags, 0x01U);
    EXPECT_EQ(hdr.cmd, 0x2A02U);
    EXPECT_EQ(hdr.size, 8U);
}

// =============================================================================
// EDGE 07: trap_entry x5 Save Order & Pure RV32I ISA Workaround (ET1031 Erratum)
// =============================================================================
TEST_F(AriesSiliconRuntimeEdgeTest, Runtime07_TrapEntryPreservesX5AndBuildUsesPureRv32i)
{
    static char sBuf[16384];
    static char mkBuf[16384];
    ASSERT_TRUE(runtime_read_repo_file("src/platform/ARIES/startup.S", sBuf, sizeof(sBuf)));
    ASSERT_TRUE(runtime_read_repo_file("src/platform/ARIES/mk/ARIES.mk", mkBuf, sizeof(mkBuf)));

    const char *saveX5 = strstr(sBuf, "SREG x5,  5*REGBYTES(sp)");
    const char *clobberT0 = strstr(sBuf, "addi t0, sp, 128");
    ASSERT_NE(saveX5, nullptr);
    ASSERT_NE(clobberT0, nullptr);
    EXPECT_LT(saveX5, clobberT0); // x5 (t0) must be saved before addi t0, sp, 128

    EXPECT_NE(strstr(mkBuf, "-march=rv32i_zicsr"), nullptr);
    EXPECT_EQ(strstr(mkBuf, "-march=rv32im_zicsr"), nullptr);
}

} // namespace
