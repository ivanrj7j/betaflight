/*
 * Unit test for ARIES v3.0 modular test infrastructure.
 */

#include <stdint.h>
#include <stdbool.h>

extern "C" {
    #include "aries/mock_mmio.h"
    #include "aries/mock_csr.h"
    #include "aries/mock_thejas32.h"
    #include "aries/test_helpers.h"
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

class AriesInfraTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_mmio_reset();
        mock_csr_reset();
        mock_thejas32_reset();
        mock_mmio_log_reset();
    }
};

TEST_F(AriesInfraTest, MockMmioResetClearsMemoryRegions)
{
    mock_periph_mem[0] = 0xAA;
    mock_gpio0_mem[10] = 0x55;
    mock_gpio1_mem[20] = 0x12;
    mock_intctrl_mem[4] = 0x77;

    mock_mmio_reset();

    EXPECT_EQ(mock_periph_mem[0], 0);
    EXPECT_EQ(mock_gpio0_mem[10], 0);
    EXPECT_EQ(mock_gpio1_mem[20], 0);
    EXPECT_EQ(mock_intctrl_mem[4], 0);
    EXPECT_EQ(mock_flash_mem[0], 0xFF); // Flash default is erased 0xFF
}

TEST_F(AriesInfraTest, MockMmioPatternReset)
{
    uint8_t buffer[64];
    mock_mmio_reset_pattern(buffer, sizeof(buffer), 0x33);
    for (size_t i = 0; i < sizeof(buffer); i++) {
        EXPECT_EQ(buffer[i], 0x33);
    }
}

TEST_F(AriesInfraTest, MockMmioAccessLogging)
{
    mock_mmio_log_write(0x10000100, 0x54);
    mock_mmio_log_write(0x10000100, 0x12);
    mock_mmio_log_read(0x10000114, 0x20);

    EXPECT_EQ(mock_mmio_log_count, 3U);
    EXPECT_EQ(mock_mmio_count_writes_to(0x10000100), 2U);
    EXPECT_EQ(mock_mmio_count_writes_to(0x10000114), 0U);

    mmio_access_t *w = mock_mmio_find_write(0x10000100);
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->value, 0x54U);
}

TEST_F(AriesInfraTest, MockCsrBasicOperations)
{
    EXPECT_EQ(getCycleCounter64(), 0ULL);
    EXPECT_EQ(getCycleCounter(), 0U);

    mock_csr_advance(500);
    EXPECT_EQ(getCycleCounter64(), 500ULL);
    EXPECT_EQ(getCycleCounter(), 500U);

    mock_csr_set(0x100000005ULL);
    EXPECT_EQ(getCycleCounter64(), 0x100000005ULL);
    EXPECT_EQ(getCycleCounter(), 5U); // 32-bit truncation

    mock_csr_reset();
    EXPECT_EQ(getCycleCounter64(), 0ULL);
}

TEST_F(AriesInfraTest, MockThejas32InterruptManagement)
{
    static int irq7_calls = 0;
    static int irq10_calls = 0;
    irq7_calls = 0;
    irq10_calls = 0;

    thejas32_interrupt_register(7, []() { irq7_calls++; });
    thejas32_interrupt_register(10, []() { irq10_calls++; });

    // Neither is enabled yet
    mock_fire_irq(7);
    mock_fire_irq(10);
    EXPECT_EQ(irq7_calls, 0);
    EXPECT_EQ(irq10_calls, 0);

    thejas32_interrupt_enable(7);
    EXPECT_TRUE(mock_irq_is_enabled(7));
    EXPECT_FALSE(mock_irq_is_enabled(10));

    mock_fire_irq(7);
    EXPECT_EQ(irq7_calls, 1);
    EXPECT_EQ(irq10_calls, 0);

    thejas32_interrupt_disable(7);
    EXPECT_FALSE(mock_irq_is_enabled(7));
    mock_fire_irq(7);
    EXPECT_EQ(irq7_calls, 1); // Doesn't increment because disabled
}

TEST_F(AriesInfraTest, TestHelpersSequentialAndCanary)
{
    uint8_t seq[10];
    fill_sequential(seq, sizeof(seq));
    EXPECT_TRUE(verify_sequential(seq, sizeof(seq), 0));
    EXPECT_FALSE(verify_sequential(seq, sizeof(seq), 1));

    uint32_t stack_mock[16];
    fill_canary(stack_mock, 16);
    EXPECT_EQ(count_intact_canaries(stack_mock, 16), 16U);
    stack_mock[0] = 0x12345678;
    EXPECT_EQ(count_intact_canaries(stack_mock, 16), 15U);
}

TEST_F(AriesInfraTest, TestHelpersAddressFormulas)
{
    uintptr_t base0 = 0x10080000;
    uintptr_t base1 = 0x10180000;

    // Pin 0 Bank 0
    EXPECT_EQ(gpio_pin_address(base0, 0), base0 + 0x04);
    // Pin 15 Bank 0
    EXPECT_EQ(gpio_pin_address(base0, 15), base0 + 0x20000);
    // Pin 6 Bank 1 (LD4 LED / PA22)
    EXPECT_EQ(gpio_pin_address(base1, 6), base1 + 0x100);

    EXPECT_EQ(gpio_dir_address(base0), base0 + 0x40000);
    EXPECT_EQ(gpio_dir_address(base1), base1 + 0x40000);

    // Baud divisor at 100 MHz
    EXPECT_EQ(uart_baud_divisor(100000000, 115200), 54U);
    EXPECT_EQ(uart_baud_divisor(100000000, 9600), 651U);
    EXPECT_EQ(uart_baud_divisor(100000000, 0), 54U); // Fallback to 115200
}
