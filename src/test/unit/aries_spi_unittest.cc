/*
 * Unit tests for ARIES v3.0 SPI Master Bus Driver.
 * Phase 3 test suite corresponding to Module T5.
 */

#include <stdint.h>
#include <stdbool.h>

extern "C" {
    #include "aries/mock_mmio.h"
    #include "aries/mock_csr.h"
    #include "aries/mock_thejas32.h"
    #include "aries/test_helpers.h"
    #include "aries_spi_testable.h"
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

class AriesSpiTest : public ::testing::Test {
protected:
    test_thejas32_spi_reg_t *spi0;

    void SetUp() override {
        mock_mmio_reset();
        mock_csr_reset();
        mock_thejas32_reset();
        spi0 = test_spi_get_device(0);
    }
};

// T5-01: 25 MHz prescaler divisor
TEST_F(AriesSpiTest, PrescalerDivFor25MHz)
{
    EXPECT_EQ(spiCalculateDivider(25000000), 0);
    EXPECT_EQ(spiCalculateDivider(20000000), 0);
}

// T5-02: 12.5 MHz prescaler divisor
TEST_F(AriesSpiTest, PrescalerDivFor12MHz)
{
    EXPECT_EQ(spiCalculateDivider(12500000), 1);
    EXPECT_EQ(spiCalculateDivider(10000000), 1);
}

// T5-03: 6.25 MHz prescaler divisor
TEST_F(AriesSpiTest, PrescalerDivFor6MHz)
{
    EXPECT_EQ(spiCalculateDivider(6250000), 2);
    EXPECT_EQ(spiCalculateDivider(5000000), 2);
}

// T5-04: Slow clock prescaler divisor (195 kHz)
TEST_F(AriesSpiTest, PrescalerDivForSlowClock)
{
    EXPECT_EQ(spiCalculateDivider(100000), 7);
    EXPECT_EQ(spiCalculateDivider(0), 7);
}

// T5-05: Inverse clock calculation for divisor 0 (25 MHz)
TEST_F(AriesSpiTest, InverseClockForDivisor0)
{
    EXPECT_EQ(spiCalculateClock(0), 25000000U);
}

// T5-06: Inverse clock calculation for divisor 1 (12.5 MHz)
TEST_F(AriesSpiTest, InverseClockForDivisor1)
{
    EXPECT_EQ(spiCalculateClock(1), 12500000U);
}

// T5-07: Inverse clock calculation for divisor 7 (195 kHz)
TEST_F(AriesSpiTest, InverseClockForDivisor7)
{
    EXPECT_EQ(spiCalculateClock(7), 195312U);
}

// T5-08: Inverse clock default fallback
TEST_F(AriesSpiTest, InverseClockDefaultFallback)
{
    EXPECT_EQ(spiCalculateClock(99), 12500000U);
}

// T5-09: Hardware initialization sets default Baudrate and Control
TEST_F(AriesSpiTest, HardwareInitRegisters)
{
    test_spiInitDevice(0);
    EXPECT_EQ(spi0->Baudrate, (1U << 4)); // Div by 8 = 12.5 MHz
    EXPECT_EQ(spi0->Control, 0x0000U);    // Mode 0, MSB first
}

// T5-10: Polled transfer loops terminate safely via bounded timeouts
TEST_F(AriesSpiTest, PolledTransferBoundedTimeoutOnStuckLines)
{
    // Status stays 0 (TX_HOLD_EMPTY not asserted)
    spi0->Status = 0;

    // Must not hang indefinitely
    uint8_t rx = test_thejas32_spi_transfer_byte(spi0, 0x55);
    EXPECT_EQ(rx, 0U);
}

// T5-11: Polled buffer transfer with valid ready status
TEST_F(AriesSpiTest, PolledBufferTransfer)
{
    // Set Status bits: TX_HOLD_EMPTY (bit 7) and RX_COMPLETE (bit 6) ready
    spi0->Status = THEJAS32_SPI_TX_HOLD_EMPTY | THEJAS32_SPI_RX_COMPLETE;
    spi0->RxData = 0x47; // WHO_AM_I response for ICM42688P

    uint8_t tx[2] = { 0x75 | 0x80, 0x00 };
    uint8_t rx[2] = { 0 };

    bool success = test_spiInternalReadWriteBufPolled(spi0, tx, rx, 2);
    EXPECT_TRUE(success);
    EXPECT_EQ(rx[0], 0x47);
    EXPECT_EQ(rx[1], 0x47);

    // Test with NULL txData (dummy 0xFF transmit) and NULL rxData (discard)
    bool success_null = test_spiInternalReadWriteBufPolled(spi0, NULL, NULL, 4);
    EXPECT_TRUE(success_null);
    EXPECT_EQ(spi0->TxData, 0xFFU);
}

// T5-12: Sequence start sets Mode 0 vs Mode 3 CPOL/CPHA control bits
TEST_F(AriesSpiTest, SequenceStartMode0AndMode3)
{
    // Mode 0: leadingEdge = true -> Control = 0x0000
    test_spiSequenceStartMock(spi0, 2, true);
    EXPECT_EQ(spi0->Control, 0x0000U);
    EXPECT_EQ(spi0->Baudrate, (2U << 4));

    // Mode 3: leadingEdge = false -> Control = 0x0030 (CPOL=1, CPHA=1)
    test_spiSequenceStartMock(spi0, 3, false);
    EXPECT_EQ(spi0->Control, 0x0030U);
    EXPECT_EQ(spi0->Baudrate, (3U << 4));
}
