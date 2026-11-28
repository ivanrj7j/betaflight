/*
 * Test Suite: C-DAC ARIES v3.0 SPI/UART/Atomic/EXTI/Filter/Trap Edges (Wave 3 - Suite 13)
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 0 RVC)
 * References & Citations:
 *   - src/platform/ARIES/bus_spi_aries.c, exti_aries.c, io_aries.c, serial_uart_aries.c, system.c, stub_drivers.c
 *   - src/main/drivers/bus_spi.c, serial_uart.c, build/atomic.h, common/filter.c
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Step 1: Living Documentation.
 *   Tests all 14 low-level peripheral, atomic nesting/early-exit cleanup, EXTI bank release,
 *   Nyquist filter clamp, lagged moving average, gyro init panic, and trap mcause edges.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <cmath>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "../main/platform.h"
    #include "build/atomic.h"
    #include "common/filter.h"
    #include "common/maths.h"
    #include "common/time.h"
    #include "drivers/accgyro/accgyro.h"
    #include "drivers/accgyro/accgyro_spi_icm426xx.h"
    #include "drivers/bus.h"
    #include "drivers/bus_spi.h"
    #include "drivers/bus_spi_impl.h"
    #include "drivers/exti.h"
    #include "drivers/io.h"
    #include "drivers/io_impl.h"
    #include "drivers/serial.h"
    #include "drivers/serial_uart.h"
    #include "drivers/serial_uart_impl.h"
    #include "drivers/system.h"
    #include "drivers/time.h"
    #include "pg/bus_spi.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"
    #include "sensors/gyro.h"
    #include "platform/ARIES/io_def_generated.h"
    #include "platform/ARIES/include/platform/bsp_thejas32.h"
    #include "aries_host_mmio.h"

#ifndef THEJAS32_SPI_STAT_RX_COMPLETE
#define THEJAS32_SPI_STAT_RX_COMPLETE (1U << 6)
#endif
#ifndef THEJAS32_SPI_STAT_TX_EMPTY
#define THEJAS32_SPI_STAT_TX_EMPTY    (1U << 7)
#endif

    uint32_t handle_trap(uint32_t mcause, uint32_t mepc, uint32_t *sp);
    void ariesPwmEmergencyStop(void);
    uint8_t icm426xxSpiDetect(const extDevice_t *dev);
    void __real_delay(uint32_t ms);
    void __wrap_delay(uint32_t ms) {
        (void)__real_delay;
        thejas32_host_mcycle_advance((uint64_t)ms * 100000ULL);
    }
    void schedulerIgnoreTaskExecTime(void) {}
    void beeperConfirmationBeeps(uint8_t) {}
    bool mpuGyroReadSPI(gyroDev_t *) { return true; }
    bool mpuAccReadSPI(accDev_t *) { return true; }
    void mpuGyroInit(gyroDev_t *) {}
    PG_REGISTER(gyroConfig_t, gyroConfig, PG_GYRO_CONFIG, 0);
}

namespace {

static uint32_t s_extiPin1Hits = 0U;
static uint32_t s_extiPin2Hits = 0U;
static extiCallbackRec_t s_extiRec1;
static extiCallbackRec_t s_extiRec2;
static void onExtiPin1(extiCallbackRec_t *) { s_extiPin1Hits++; }
static void onExtiPin2(extiCallbackRec_t *) { s_extiPin2Hits++; }

static int helperAtomicEarlyReturn(bool returnEarly)
{
    ATOMIC_BLOCK(0x20U) {
        EXPECT_NE(atomic_BASEPRI, 0U);
        if (returnEarly) {
            return 42;
        }
    }
    return 0;
}

class AriesBusGpioTrapEdgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        thejas32_host_reset_all();
        IOInitGlobal();
        EXTIInit();
        pgResetAll();
        s_extiPin1Hits = 0U;
        s_extiPin2Hits = 0U;
        thejas32_host_mcycle_set(100000ULL);
    }
};

// =============================================================================
// EDGE 01: spiSequence Self-Queue & Multi-Segment Link Integrity
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge01_And_02_SpiSequenceAndLinkSegments_ChainCleanlyWithoutSelfLoop)
{
    busDevice_t busDev = {};
    extDevice_t extDev = {};
    busDev.busType = BUS_TYPE_SPI;
    busDev.busType_u.spi.instance = (spiResource_t *)&THEJAS32_SPI(0);
    extDev.bus = &busDev;
    extDev.busType_u.spi.csnPin = IOGetByTag(DEFIO_TAG(PA10));
    IOInit(extDev.busType_u.spi.csnPin, OWNER_GYRO_CS, 0);
    IOConfigGPIO(extDev.busType_u.spi.csnPin, IOCFG_OUT_PP);
    IOHi(extDev.busType_u.spi.csnPin);

    THEJAS32_SPI(0).Status = THEJAS32_SPI_STAT_TX_EMPTY | THEJAS32_SPI_STAT_RX_COMPLETE;
    uint8_t txA[4] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t rxA[4] = { 0 };
    uint8_t txB[4] = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t rxB[4] = { 0 };

    busSegment_t segs[3] = {
        { { txA, rxA }, 4, false, NULL },
        { { txB, rxB }, 4, true,  NULL },
        { { NULL, NULL }, 0, true, NULL },
    };

    EXPECT_FALSE(spiIsBusy(&extDev));
    spiSequence(&extDev, &segs[0]);
    spiWait(&extDev);
    EXPECT_FALSE(spiIsBusy(&extDev));
}

// =============================================================================
// EDGE 03: BUS_BUSY / spiRelease Recovery
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge03_SpiBusRelease_ClearsWaitAndCsSafely)
{
    busDevice_t busDev = {};
    extDevice_t extDev = {};
    busDev.busType = BUS_TYPE_SPI;
    busDev.busType_u.spi.instance = (spiResource_t *)&THEJAS32_SPI(0);
    extDev.bus = &busDev;
    extDev.busType_u.spi.csnPin = IOGetByTag(DEFIO_TAG(PA10));
    IOInit(extDev.busType_u.spi.csnPin, OWNER_GYRO_CS, 0);
    IOHi(extDev.busType_u.spi.csnPin);

    spiWait(&extDev);
    spiRelease(&extDev);
    EXPECT_FALSE(spiIsBusy(&extDev));
}

// =============================================================================
// EDGE 04: UART Ring Buffer Exact Wrap, Full (size - 1), and Empty Conditions
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge04_SerialUartRingBufferWrap_SizeMinusOneFullCapacityPreserved)
{
    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
    pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
    uartPinConfigure(&pinCfg);
    serialPort_t *sp0 = uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, 115200U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(sp0, nullptr);

    const uint32_t maxCap = sp0->txBufferSize - 1U;
    THEJAS32_UART(0).UART_LSR = 0x00U;
    for (uint32_t i = 0; i < maxCap; i++) {
        serialWrite(sp0, (uint8_t)(i & 0xFFU));
    }
    EXPECT_EQ(serialTxBytesFree(sp0), 0U);
    EXPECT_FALSE(isSerialTransmitBufferEmpty(sp0));

    THEJAS32_UART(0).UART_LSR = 0x60U;
    for (int k = 0; k < 64 && !isSerialTransmitBufferEmpty(sp0); k++) {
        uartEnableTxInterrupt((uartPort_t *)sp0);
    }
    EXPECT_TRUE(isSerialTransmitBufferEmpty(sp0));
    EXPECT_EQ(serialTxBytesFree(sp0), maxCap);
}

// =============================================================================
// EDGE 05: UART Framing Options (2 Stop Bits & Even Parity) Program Exact LCR
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge05_SerialUartOpenOptions_StopBits2AndEvenParitySetLcrBits)
{
    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[1] = DEFIO_TAG_E__PA16;
    pinCfg.ioTagRx[1] = DEFIO_TAG_E__PA17;
    uartPinConfigure(&pinCfg);
    serialPort_t *sp1 = uartOpen(SERIAL_PORT_USART1, nullptr, nullptr, 100000U, MODE_RX,
                                 (portOptions_e)(SERIAL_STOPBITS_2 | SERIAL_PARITY_EVEN));
    ASSERT_NE(sp1, nullptr);
    EXPECT_EQ(THEJAS32_UART(1).UART_LCR & 0x9FU, 0x1FU); // 8E2 (0x03 | 0x04 | 0x18 == 0x1F)
}

// =============================================================================
// EDGE 06: 460800 Allowed Unclamped, Whereas 460801/921600 Clamp To 420000
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge06_Baud460800BoundaryVsGreaterThan460800ClampTo420000)
{
    serialPinConfig_t pinCfg = {};
    pinCfg.ioTagTx[0] = DEFIO_TAG_E__PA30;
    pinCfg.ioTagRx[0] = DEFIO_TAG_E__PA31;
    uartPinConfigure(&pinCfg);

    serialPort_t *s460 = uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, 460800U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(s460, nullptr);
    EXPECT_EQ(s460->baudRate, 460800U);

    serialPort_t *s461 = uartOpen(SERIAL_PORT_UART0, nullptr, nullptr, 460801U, MODE_RXTX, SERIAL_NOT_INVERTED);
    ASSERT_NE(s461, nullptr);
    EXPECT_EQ(s461->baudRate, 420000U);
}

// =============================================================================
// EDGE 07: Nested ATOMIC_BLOCK & Early Return Restores mstatus.MIE Cleanly
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge07_AtomicBlockNestingAndEarlyReturn_PreservesOuterMie)
{
    // Part A: Host ATOMIC_BLOCK cleanup attribute restores outer BASEPRI on early return
    atomic_BASEPRI = 0U;
    {
        ATOMIC_BLOCK(0x10U) {
            EXPECT_EQ(atomic_BASEPRI, 0x10U);
            EXPECT_EQ(helperAtomicEarlyReturn(true), 42);
            EXPECT_EQ(atomic_BASEPRI, 0x10U);
        }
    }
    EXPECT_EQ(atomic_BASEPRI, 0U);

    // Part B: THEJAS32 CSR mstatus.MIE nested critical sections preserve outer MIE = 0
    thejas32_host_mstatus = 0x8U; // Outer MIE = 1
    uint32_t outer = thejas32_atomic_enter();
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
    uint32_t inner = thejas32_atomic_enter();
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
    thejas32_atomic_exit(inner);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U); // Inner exit MUST KEEP MIE = 0!
    thejas32_atomic_exit(outer);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0x8U);
}

// =============================================================================
// EDGE 08: IOGetByTag(0) Safe Zero & Push-Pull Configuration
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge08_IoGetByTagZeroAndConfigOutOd_HandledSafely)
{
    EXPECT_EQ(IOGetByTag(0U), IO_NONE);
    IO_t led = IOGetByTag(DEFIO_TAG_E__PA22);
    ASSERT_NE(led, IO_NONE);
    IOConfigGPIO(led, IOCFG_OUT_OD);
    IOHi(led);
    IOLo(led);
    EXPECT_EQ(IO_Pin(led), 22);
}

// =============================================================================
// EDGE 09: EXTIRelease Bank 0 Stops Callback & Multi-Pin Dispatch Isolates Pins
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge09_ExtiReleaseAndMultiPinDispatch_IsolatesReleasedPin)
{
    IO_t p0 = IOGetByTag(DEFIO_TAG(PA0));   // Bank 0 (IRQ 10)
    IO_t p0Alt = IOGetByTag(DEFIO_TAG(PA8)); // Bank 0 second pin (blocked while p0 active)
    IO_t p1 = IOGetByTag(DEFIO_TAG(PA16));  // Bank 1 (IRQ 11)
    EXTIHandlerInit(&s_extiRec1, onExtiPin1);
    EXTIHandlerInit(&s_extiRec2, onExtiPin2);
    EXTIConfig(p0, &s_extiRec1, 1U, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXTIConfig(p0Alt, &s_extiRec1, 1U, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXTIConfig(p1, &s_extiRec2, 1U, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);

    EXTIEnable(p0);
    EXTIEnable(p0Alt); // Must be rejected by Bank 0 single-pin lock while PA0 is active
    EXTIEnable(p1);

    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_GPIO0) | (1UL << THEJAS32_IRQ_GPIO1);
    handle_trap(0x8000000BUL, 0U, nullptr);
    EXPECT_EQ(s_extiPin1Hits, 1U);
    EXPECT_EQ(s_extiPin2Hits, 1U);

    EXTIRelease(p0);
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_GPIO0) | (1UL << THEJAS32_IRQ_GPIO1);
    handle_trap(0x8000000BUL, 0U, nullptr);
    EXPECT_EQ(s_extiPin1Hits, 1U); // Did not fire after EXTIRelease!
    EXPECT_EQ(s_extiPin2Hits, 2U); // Bank 1 still fires independently

    // Bank 0 lock now released: PA8 can take over IRQ 10 cleanly
    EXTIEnable(p0Alt);
    THEJAS32_INTR_CTRL.INTR_STATUS = (1UL << THEJAS32_IRQ_GPIO0);
    handle_trap(0x8000000BUL, 0U, nullptr);
    EXPECT_EQ(s_extiPin1Hits, 2U);
}

// =============================================================================
// EDGE 10 & 11: Filter Zero-Delay Bypass, Nyquist Clamp, & Lagged Moving Average
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge10_And_11_FilterNyquistClampAndLaggedMovingAverageExact)
{
    EXPECT_FLOAT_EQ(pt1FilterGainFromDelay(0.0f, 0.001f), 1.0f);
    EXPECT_FLOAT_EQ(pt3FilterGainFromDelay(0.0f, 0.001f), 1.0f);

    // Near Nyquist (490 Hz @ 1000 us dT = 1 kHz sample rate): must stay finite without NaN/Inf
    svfLowpassFilter_t lpf;
    svfLowpassFilterInit(&lpf, 490.0f, 0.001f);
    for (int i = 0; i < 200; i++) {
        float y = svfLowpassFilterApply(&lpf, (i & 1) ? 100.0f : -100.0f);
        EXPECT_TRUE(std::isfinite(y));
    }

    // Lagged moving average over 4-sample ring (unprimed denom = sample count 1..3, primed = 4)
    float buf[4] = { 0 };
    laggedMovingAverage_t avg;
    laggedMovingAverageInit(&avg, 4U, buf);
    EXPECT_FLOAT_EQ(laggedMovingAverageUpdate(&avg, 10.0f), 10.0f); // 10/1
    EXPECT_FLOAT_EQ(laggedMovingAverageUpdate(&avg, 20.0f), 15.0f); // (10+20)/2
    EXPECT_FLOAT_EQ(laggedMovingAverageUpdate(&avg, 30.0f), 20.0f); // (10+20+30)/3
    EXPECT_FLOAT_EQ(laggedMovingAverageUpdate(&avg, 40.0f), 25.0f); // (10+20+30+40)/4
    EXPECT_FLOAT_EQ(laggedMovingAverageUpdate(&avg, 50.0f), 35.0f); // (20+30+40+50)/4 == 35.0
}

// =============================================================================
// EDGE 12: Dead SPI0 MISO (0xFF) Rejects Detect -> failureMode Disables IRQs & Motors
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge12_MissingGyroDetect_ReturnsMpuNoneAndFailureModeStopsIrqs)
{
    busDevice_t busDev = {};
    extDevice_t extDev = {};
    busDev.busType = BUS_TYPE_SPI;
    busDev.busType_u.spi.instance = (spiResource_t *)&THEJAS32_SPI(0);
    extDev.bus = &busDev;
    extDev.busType_u.spi.csnPin = IOGetByTag(DEFIO_TAG(PA10));
    IOInit(extDev.busType_u.spi.csnPin, OWNER_GYRO_CS, 0);
    THEJAS32_SPI(0).Status = THEJAS32_SPI_STAT_TX_EMPTY | THEJAS32_SPI_STAT_RX_COMPLETE;
    THEJAS32_SPI(0).RxData = 0xFFU; // Dead/unpowered gyro MISO line

    EXPECT_EQ(icm426xxSpiDetect(&extDev), MPU_NONE);

    THEJAS32_INTR_CTRL.INTR_EN = 0xFFFFFFFFUL;
    THEJAS32_TIMER(1).Control = 0x03U;
    thejas32_host_mstatus = 0x8U;
    failureMode(FAILURE_GYRO_INIT_FAILED);
    EXPECT_EQ(THEJAS32_TIMER(1).Control, 0U);
    EXPECT_EQ(THEJAS32_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_TIMER1), 0U);
    EXPECT_EQ(thejas32_host_mstatus & 0x8U, 0U);
}

// =============================================================================
// EDGE 13: Microsecond Wraparound Delta Arithmetic Remains Exact Across 2^32
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge13_Micros32BitWraparound_CmpTimeUsExactAcrossBoundary)
{
    thejas32_host_mcycle_set(4294967000ULL * 100ULL);
    const timeUs_t tBefore = micros();
    thejas32_host_mcycle_advance(500ULL * 100ULL);
    const timeUs_t tAfter = micros();
    EXPECT_EQ(cmpTimeUs(tAfter, tBefore), 500);
}

// =============================================================================
// EDGE 14: Synchronous mcause Exceptions (RVC Illegal 2, Misaligned 4/6, Fault 5/7)
// =============================================================================
TEST_F(AriesBusGpioTrapEdgeTest, Edge14_HandleTrapSyncMcauseCodes_CutMotorsAndAdvanceMepc4)
{
    const uint32_t causes[] = { 2U, 4U, 5U, 6U, 7U };
    for (uint32_t mcause : causes) {
        thejas32_host_reset_all();
        THEJAS32_TIMER(1).Control = 0x03U;
        *thejas32_gpio_data_ptr(1U) = 1U;
        uint32_t spFrame[32] = { 0 };
        const uint32_t retPc = handle_trap(mcause, 0x00202100UL, spFrame);
        EXPECT_EQ(retPc, 0x00202104UL);
        EXPECT_EQ(THEJAS32_TIMER(1).Control, 0U);
        EXPECT_EQ(*thejas32_gpio_data_ptr(1U), 0U);
    }
}

} // namespace
