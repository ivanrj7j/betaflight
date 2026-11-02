/*
 * Unit tests for ARIES v3.0 External Interrupt (EXTI) Driver.
 * Phase 4 test suite corresponding to Module T4.
 */

#include <stdint.h>
#include <stdbool.h>

extern "C" {
    #include "aries/mock_mmio.h"
    #include "aries/mock_csr.h"
    #include "aries/mock_thejas32.h"
    #include "aries/test_helpers.h"
    #include "aries_gpio_testable.h"
    #include "aries_exti_testable.h"
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

class AriesExtiTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_mmio_reset();
        mock_thejas32_reset();
        test_IOInitGlobal();
        test_EXTIInit();
    }
};

// T4-01: EXTIInit() clears all 32 channels
TEST_F(AriesExtiTest, InitClearsAllChannels)
{
    for (uint16_t pin = 0; pin < 32; pin++) {
        EXPECT_FALSE(test_exti_is_channel_enabled(pin));
        EXPECT_EQ(test_exti_get_channel_handler(pin), nullptr);
    }
}

// T4-02: EXTIInit() registers Bank 0 (IRQ 10) and Bank 1 (IRQ 11) ISRs
TEST_F(AriesExtiTest, InitRegistersBankIsrs)
{
    EXPECT_NE(mock_irq_table[THEJAS32_IRQ_GPIO0], nullptr);
    EXPECT_NE(mock_irq_table[THEJAS32_IRQ_GPIO1], nullptr);
}

// T4-03: EXTIConfig() stores callback record for Bank 0 pin
TEST_F(AriesExtiTest, ConfigStoresCallbackBank0)
{
    test_extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, [](test_extiCallbackRec_t *) {});

    test_IO_t pa0 = test_IOGetByPin(0);
    test_EXTIConfig(pa0, &cb, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);

    EXPECT_TRUE(test_exti_is_channel_enabled(0));
    EXPECT_EQ(test_exti_get_channel_handler(0), &cb);
}

// T4-04: EXTIConfig() stores callback record for Bank 1 pin (PA22)
TEST_F(AriesExtiTest, ConfigStoresCallbackBank1)
{
    test_extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, [](test_extiCallbackRec_t *) {});

    test_IO_t pa22 = test_IOGetByPin(22);
    test_EXTIConfig(pa22, &cb, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);

    EXPECT_TRUE(test_exti_is_channel_enabled(22));
    EXPECT_EQ(test_exti_get_channel_handler(22), &cb);
}

// T4-05: EXTIConfig on Bank 0 enables IRQ 10 in interrupt controller
TEST_F(AriesExtiTest, ConfigBank0EnablesIrq10)
{
    test_extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, [](test_extiCallbackRec_t *) {});

    EXPECT_FALSE(mock_irq_is_enabled(THEJAS32_IRQ_GPIO0));
    test_EXTIConfig(test_IOGetByPin(5), &cb, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);
    EXPECT_TRUE(mock_irq_is_enabled(THEJAS32_IRQ_GPIO0));
}

// T4-06: EXTIConfig on Bank 1 enables IRQ 11 in interrupt controller
TEST_F(AriesExtiTest, ConfigBank1EnablesIrq11)
{
    test_extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, [](test_extiCallbackRec_t *) {});

    EXPECT_FALSE(mock_irq_is_enabled(THEJAS32_IRQ_GPIO1));
    test_EXTIConfig(test_IOGetByPin(22), &cb, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);
    EXPECT_TRUE(mock_irq_is_enabled(THEJAS32_IRQ_GPIO1));
}

// T4-07: thejas32_gpio0_isr() invokes registered Bank 0 callbacks
TEST_F(AriesExtiTest, Gpio0IsrFiresBank0Callbacks)
{
    static int cb0_called = 0;
    static int cb1_called = 0;
    cb0_called = 0;
    cb1_called = 0;

    test_extiCallbackRec_t cb0, cb1;
    test_EXTIHandlerInit(&cb0, [](test_extiCallbackRec_t *) { cb0_called++; });
    test_EXTIHandlerInit(&cb1, [](test_extiCallbackRec_t *) { cb1_called++; });

    test_EXTIConfig(test_IOGetByPin(0), &cb0, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);
    test_EXTIConfig(test_IOGetByPin(1), &cb1, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);

    test_thejas32_gpio0_isr();

    EXPECT_EQ(cb0_called, 1);
    EXPECT_EQ(cb1_called, 1);
}

// T4-08: thejas32_gpio1_isr() invokes Bank 1 and isolates from Bank 0
TEST_F(AriesExtiTest, BankIsolationBetweenGpio0AndGpio1)
{
    static int bank0_called = 0;
    static int bank1_called = 0;
    bank0_called = 0;
    bank1_called = 0;

    test_extiCallbackRec_t cb0, cb1;
    test_EXTIHandlerInit(&cb0, [](test_extiCallbackRec_t *) { bank0_called++; });
    test_EXTIHandlerInit(&cb1, [](test_extiCallbackRec_t *) { bank1_called++; });

    test_EXTIConfig(test_IOGetByPin(0), &cb0, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);
    test_EXTIConfig(test_IOGetByPin(22), &cb1, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);

    // Fire Bank 1 ISR only
    test_thejas32_gpio1_isr();

    EXPECT_EQ(bank0_called, 0); // Bank 0 must NOT fire
    EXPECT_EQ(bank1_called, 1); // Bank 1 fired
}

// T4-09: EXTIDisable prevents callback execution
TEST_F(AriesExtiTest, DisableBlocksCallbackExecution)
{
    static int calls = 0;
    calls = 0;

    test_extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, [](test_extiCallbackRec_t *) { calls++; });
    test_IO_t pa0 = test_IOGetByPin(0);
    test_EXTIConfig(pa0, &cb, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);

    test_EXTIDisable(pa0);
    EXPECT_FALSE(test_exti_is_channel_enabled(0));

    test_thejas32_gpio0_isr();
    EXPECT_EQ(calls, 0);
}

// T4-10: EXTIEnable re-enables callback execution
TEST_F(AriesExtiTest, EnableRestoresCallbackExecution)
{
    static int calls = 0;
    calls = 0;

    test_extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, [](test_extiCallbackRec_t *) { calls++; });
    test_IO_t pa0 = test_IOGetByPin(0);
    test_EXTIConfig(pa0, &cb, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);
    test_EXTIDisable(pa0);

    test_EXTIEnable(pa0);
    EXPECT_TRUE(test_exti_is_channel_enabled(0));

    test_thejas32_gpio0_isr();
    EXPECT_EQ(calls, 1);
}

// T4-11: EXTIRelease clears handler and disables channel
TEST_F(AriesExtiTest, ReleaseClearsHandler)
{
    test_extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, [](test_extiCallbackRec_t *) {});
    test_IO_t pa0 = test_IOGetByPin(0);
    test_EXTIConfig(pa0, &cb, 0, THEJAS32_GPIO_MODE_INPUT, TEST_EXTI_TRIGGER_RISING);

    test_EXTIRelease(pa0);
    EXPECT_FALSE(test_exti_is_channel_enabled(0));
    EXPECT_EQ(test_exti_get_channel_handler(0), nullptr);
}

// T4-12: IO_EXTI_Line returns bitmask for pin
TEST_F(AriesExtiTest, ExtiLineBitmask)
{
    EXPECT_EQ(test_IO_EXTI_Line(test_IOGetByPin(0)), (1U << 0));
    EXPECT_EQ(test_IO_EXTI_Line(test_IOGetByPin(5)), (1U << 5));
    EXPECT_EQ(test_IO_EXTI_Line(test_IOGetByPin(22)), (1U << 22));
    EXPECT_EQ(test_IO_EXTI_Line(NULL), 0U);
}
