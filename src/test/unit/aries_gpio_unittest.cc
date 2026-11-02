/*
 * Unit tests for ARIES v3.0 Address-Decoded GPIO Driver.
 * Phase 4 test suite corresponding to Module T3.
 */

#include <stdint.h>
#include <stdbool.h>

extern "C" {
    #include "aries/mock_mmio.h"
    #include "aries/mock_csr.h"
    #include "aries/mock_thejas32.h"
    #include "aries/test_helpers.h"
    #include "aries_gpio_testable.h"
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

class AriesGpioTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_mmio_reset();
        test_IOInitGlobal();
    }
};

// T3-01: IOHi(PA0) Bank 0 Pin 0 address-decoded write
TEST_F(AriesGpioTest, Pin0Bank0WriteHigh)
{
    test_IO_t pa0 = test_IOGetByPin(0);
    ASSERT_NE(pa0, nullptr);

    test_IOHi(pa0);

    // Value written must be (1 << 0) = 1
    EXPECT_EQ(test_gpio_read_data_reg(0, 0), 1U);
}

// T3-02: IOHi(PA15) Bank 0 Pin 15 address-decoded write
TEST_F(AriesGpioTest, Pin15Bank0WriteHigh)
{
    test_IO_t pa15 = test_IOGetByPin(15);
    ASSERT_NE(pa15, nullptr);

    test_IOHi(pa15);

    // Value written must be (1 << 15) = 0x8000
    EXPECT_EQ(test_gpio_read_data_reg(0, 15), 0x8000U);
}

// T3-03: IOHi(PA16) Bank 1 Pin 0 address-decoded write
TEST_F(AriesGpioTest, Pin16Bank1WriteHigh)
{
    test_IO_t pa16 = test_IOGetByPin(16);
    ASSERT_NE(pa16, nullptr);

    test_IOHi(pa16);

    // Bank 1 pin 0 value written must be (1 << 0) = 1
    EXPECT_EQ(test_gpio_read_data_reg(1, 0), 1U);
}

// T3-04: IOHi(PA22) Bank 1 Pin 6 (Heartbeat LED LD4) address-decoded write
TEST_F(AriesGpioTest, Pin22HeartbeatLedWriteHigh)
{
    test_IO_t pa22 = test_IOGetByPin(22);
    ASSERT_NE(pa22, nullptr);

    test_IOHi(pa22);

    // Bank 1 pin 6 (22 - 16 = 6) -> (1 << 6) = 64 (0x40)
    EXPECT_EQ(test_gpio_read_data_reg(1, 6), 0x40U);
}

// T3-05: IOLo writes 0 to data register
TEST_F(AriesGpioTest, WriteLowZeroesDataReg)
{
    test_IO_t pa0 = test_IOGetByPin(0);
    test_IOHi(pa0);
    EXPECT_EQ(test_gpio_read_data_reg(0, 0), 1U);

    test_IOLo(pa0);
    EXPECT_EQ(test_gpio_read_data_reg(0, 0), 0U);
}

// T3-06: IOToggle transitions 0 -> (1 << pin) -> 0
TEST_F(AriesGpioTest, ToggleStateTransitions)
{
    test_IO_t pa5 = test_IOGetByPin(5);

    test_IOLo(pa5);
    EXPECT_EQ(test_gpio_read_data_reg(0, 5), 0U);

    test_IOToggle(pa5);
    EXPECT_EQ(test_gpio_read_data_reg(0, 5), (1U << 5));

    test_IOToggle(pa5);
    EXPECT_EQ(test_gpio_read_data_reg(0, 5), 0U);
}

// T3-07: IORead reflects active data bit
TEST_F(AriesGpioTest, ReadReflectsPinState)
{
    test_IO_t pa7 = test_IOGetByPin(7);

    test_IOLo(pa7);
    EXPECT_FALSE(test_IORead(pa7));

    test_IOHi(pa7);
    EXPECT_TRUE(test_IORead(pa7));
}

// T3-08: IOConfigGPIO(OUTPUT) sets direction bit at base + 0x40000
TEST_F(AriesGpioTest, ConfigOutputSetsDirectionBit)
{
    test_IO_t pa3 = test_IOGetByPin(3);

    test_IOConfigGPIO(pa3, THEJAS32_GPIO_MODE_OUTPUT);
    EXPECT_TRUE((test_gpio_read_dir_reg(0) & (1U << 3)) != 0);

    test_IO_t pa20 = test_IOGetByPin(20); // Bank 1, pin 4
    test_IOConfigGPIO(pa20, THEJAS32_GPIO_MODE_OUTPUT);
    EXPECT_TRUE((test_gpio_read_dir_reg(1) & (1U << 4)) != 0);
}

// T3-09: IOConfigGPIO(INPUT) clears direction bit
TEST_F(AriesGpioTest, ConfigInputClearsDirectionBit)
{
    test_IO_t pa3 = test_IOGetByPin(3);
    test_IOConfigGPIO(pa3, THEJAS32_GPIO_MODE_OUTPUT);
    EXPECT_TRUE((test_gpio_read_dir_reg(0) & (1U << 3)) != 0);

    test_IOConfigGPIO(pa3, THEJAS32_GPIO_MODE_INPUT);
    EXPECT_FALSE((test_gpio_read_dir_reg(0) & (1U << 3)) != 0);
}

// T3-10: IO_GPIOPortIdx returns bank index 0 or 1
TEST_F(AriesGpioTest, PortIndexCalculation)
{
    EXPECT_EQ(test_IO_GPIOPortIdx(test_IOGetByPin(0)), 0);
    EXPECT_EQ(test_IO_GPIOPortIdx(test_IOGetByPin(15)), 0);
    EXPECT_EQ(test_IO_GPIOPortIdx(test_IOGetByPin(16)), 1);
    EXPECT_EQ(test_IO_GPIOPortIdx(test_IOGetByPin(31)), 1);
    EXPECT_EQ(test_IO_GPIOPortIdx(NULL), -1);
}

// T3-11: IO_GPIOPinIdx returns pin within bank
TEST_F(AriesGpioTest, PinIndexCalculation)
{
    EXPECT_EQ(test_IO_GPIOPinIdx(test_IOGetByPin(0)), 0);
    EXPECT_EQ(test_IO_GPIOPinIdx(test_IOGetByPin(15)), 15);
    EXPECT_EQ(test_IO_GPIOPinIdx(test_IOGetByPin(16)), 0);
    EXPECT_EQ(test_IO_GPIOPinIdx(test_IOGetByPin(22)), 6);
    EXPECT_EQ(test_IO_GPIOPinIdx(test_IOGetByPin(31)), 15);
    EXPECT_EQ(test_IO_GPIOPinIdx(NULL), -1);
}

// T3-12: IOWrite delegates to IOHi and IOLo
TEST_F(AriesGpioTest, WriteDelegation)
{
    test_IO_t pa1 = test_IOGetByPin(1);

    test_IOWrite(pa1, true);
    EXPECT_EQ(test_gpio_read_data_reg(0, 1), (1U << 1));

    test_IOWrite(pa1, false);
    EXPECT_EQ(test_gpio_read_data_reg(0, 1), 0U);
}

// T3-13: IOInitGlobal correctly sets all 32 pin records
TEST_F(AriesGpioTest, InitGlobalInitializes32Pins)
{
    for (uint16_t pin = 0; pin < 32; pin++) {
        test_IO_t io = test_IOGetByPin(pin);
        ASSERT_NE(io, nullptr);
        EXPECT_EQ(io->pin, pin);
        if (pin < 16) {
            EXPECT_EQ(io->gpio_base, (uintptr_t)mock_gpio0_mem);
        } else {
            EXPECT_EQ(io->gpio_base, (uintptr_t)mock_gpio1_mem);
        }
    }
}

// T3-14: Bounds check protects against out of range pins
TEST_F(AriesGpioTest, BoundsProtection)
{
    EXPECT_EQ(test_IOGetByPin(32), nullptr);
    EXPECT_EQ(test_IOGetByPin(100), nullptr);

    test_ioRec_t bad_io = { .pin = 99, .gpio_base = 0 };
    test_IOHi(&bad_io);
    test_IOLo(&bad_io);
    test_IOToggle(&bad_io);
    EXPECT_FALSE(test_IORead(&bad_io));
}
