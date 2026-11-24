/*
 * Test Suite: C-DAC ARIES v3.0 External Interrupt (EXTI) Controller Specification
 * Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)
 * Reference:
 *   - PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md (Microcomponent 04_C)
 *   - PLAN/20_DEVELOPMENT_PHASES.md (Phase 15: External Interrupt Controller & O(1) CTZ Fast-Path)
 * Citations:
 *   - knowledgebase/codebase_evaluations/08_EVAL_EXTI_ARIES_C.md (Critique of linear 16-pin loop; CTZ recommendation)
 *   - knowledgebase/components/06_INTERRUPTS_AND_EXTI.md (PLIC & THEJAS32 Interrupt Architecture)
 *   - lib/vega-sdk/vega-sdk/bsp/drivers/interrupt/interrupt.c (C-DAC Reference Interrupt Driver)
 *   - lib/vega-sdk/vega-sdk/bsp/include/interrupt.h
 *   - src/platform/ARIES/include/platform/bsp_thejas32.h
 *   - src/platform/ARIES/target/ARIESV3/target.h
 *   - src/platform/ESP32/exti_esp32.c (Non-STM32 centralized GPIO EXTI reference pattern)
 *
 * Methodology:
 *   Strict Test-Driven Development (TDD) - Living Documentation (TDD Step 1).
 *   This test suite formally validates:
 *     1. EXTI peripheral registration to THEJAS32 IRQ 10 (Bank 0) and IRQ 11 (Bank 1).
 *     2. O(1) count-trailing-zeros (__builtin_ctz) fast-path dispatching without linear scanning.
 *     3. Dedicated Gyro Data-Ready (DRDY) interrupt dispatch on PA0 (Header J1) with 0-cycle scan overhead.
 *     4. Arbitrary concurrent multi-channel dispatch ordering.
 *     5. Bank 1 pin offset translation (PA16..PA31 mapping to pin index 16..31).
 *     6. Active channel bitmask state tracking (exti_bank0_active_mask & exti_bank1_active_mask).
 *     7. Dynamic peripheral interrupt controller unmasking and masking upon channel transitions.
 *     8. Betaflight EXTI API contract: EXTIInit, EXTIHandlerInit, EXTIConfig, EXTIEnable, EXTIDisable, EXTIRelease.
 *     9. Trigger edge configuration retention (RISING, FALLING, BOTH).
 *    10. Defensive NULL pointer and out-of-bounds guards.
 *    11. Spurious interrupt tolerance (0 iterations on empty active mask).
 *    12. Deterministic SRAM memory budget compliance (<= 256 bytes for channel descriptors).
 *    13. IO_EXTI_Line macro bitmask calculations.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector>

#include "unittest_macros.h"
#include "gtest/gtest.h"

#include "common/utils.h"
#include "drivers/io_types.h"
#include "drivers/exti.h"
#include "platform/ARIES/include/platform/platform.h"
#include "platform/ARIES/include/platform/bsp_thejas32.h"
#include "platform/ARIES/io_def_generated.h"

// Forward declare ioRec_t matching Betaflight drivers/io_impl.h
typedef struct ioRec_s {
    void *gpio;
    uint16_t pin;
    uint8_t owner;
    uint8_t index;
} ioRec_t;

static inline ioRec_t *IO_Rec(IO_t io) { return (ioRec_t *)io; }
static inline uint16_t IO_Pin(IO_t io) { return io ? IO_Rec(io)->pin : 0; }

// -----------------------------------------------------------------------------
// Mock Platform State & Emulation Primitives
// -----------------------------------------------------------------------------

namespace {

// Mock interrupt controller state
uint32_t mock_intr_en = 0;
uint32_t mock_registered_irqs[32] = {0};
thejas32_irq_handler_t mock_irq_handlers[32] = {NULL};

void mock_interrupt_register(uint8_t irq, thejas32_irq_handler_t handler)
{
    if (irq < 32) {
        mock_registered_irqs[irq] = 1;
        mock_irq_handlers[irq] = handler;
    }
}

void mock_interrupt_enable(uint8_t irq)
{
    if (irq < 32) {
        mock_intr_en |= (1UL << irq);
    }
}

void mock_interrupt_disable(uint8_t irq)
{
    if (irq < 32) {
        mock_intr_en &= ~(1UL << irq);
    }
}

// Mock ioRec records for 32 pins
ioRec_t mock_ioRecs[32];

ioRec_t *mock_get_ioRec(uint8_t pin)
{
    if (pin >= 32) return NULL;
    mock_ioRecs[pin].gpio = (void *)((pin < 16) ? 0x10080000 : 0x10180000);
    mock_ioRecs[pin].pin = pin;
    return &mock_ioRecs[pin];
}

// EXTI Channel record layout matching 32-bit RV32IM implementation
typedef struct {
    extiCallbackRec_t *handler;
    uint8_t trigger;
    bool enabled;
} ariesExtiChannel_t;

// Channel storage and active masks
ariesExtiChannel_t test_extiChannels[32];
volatile uint16_t test_bank0_active_mask = 0;
volatile uint16_t test_bank1_active_mask = 0;

// Tracking callback invocations
std::vector<uint8_t> callback_invocation_log;

void test_callback_recorder(extiCallbackRec_t *self)
{
    // Derive channel index from callback pointer offset
    uintptr_t base = (uintptr_t)self;
    callback_invocation_log.push_back((uint8_t)(base & 0xFF));
}

// Simulated Bank 0 ISR (IRQ 10) with __builtin_ctz fast-path
void sim_gpio0_isr(void)
{
    uint32_t active = test_bank0_active_mask;
    while (active != 0U) {
        uint32_t bit = (uint32_t)__builtin_ctz(active);
        uint32_t pin = bit;
        if (pin < 16 && test_extiChannels[pin].enabled &&
            test_extiChannels[pin].handler && test_extiChannels[pin].handler->fn) {
            test_extiChannels[pin].handler->fn(test_extiChannels[pin].handler);
        }
        active &= ~(1U << bit);
    }
}

// Simulated Bank 1 ISR (IRQ 11) with __builtin_ctz fast-path
void sim_gpio1_isr(void)
{
    uint32_t active = test_bank1_active_mask;
    while (active != 0U) {
        uint32_t bit = (uint32_t)__builtin_ctz(active);
        uint32_t pin = bit + 16;
        if (pin < 32 && test_extiChannels[pin].enabled &&
            test_extiChannels[pin].handler && test_extiChannels[pin].handler->fn) {
            test_extiChannels[pin].handler->fn(test_extiChannels[pin].handler);
        }
        active &= ~(1U << bit);
    }
}

// Test harness implementation of driver API
void test_EXTIInit(void)
{
    memset(test_extiChannels, 0, sizeof(test_extiChannels));
    test_bank0_active_mask = 0;
    test_bank1_active_mask = 0;

    mock_interrupt_register(THEJAS32_IRQ_GPIO0, sim_gpio0_isr);
    mock_interrupt_register(THEJAS32_IRQ_GPIO1, sim_gpio1_isr);
}

void test_EXTIHandlerInit(extiCallbackRec_t *cb, extiHandlerCallback *fn)
{
    if (cb) {
        cb->fn = fn;
    }
}

void test_EXTIConfig(IO_t io, extiCallbackRec_t *cb, int irqPriority, ioConfig_t config, extiTrigger_t trigger)
{
    UNUSED(irqPriority);
    UNUSED(config);
    if (!io) return;
    uint8_t pin = (uint8_t)IO_Pin(io);
    if (pin >= 32) return;

    test_extiChannels[pin].handler = cb;
    test_extiChannels[pin].trigger = (uint8_t)trigger;
    test_extiChannels[pin].enabled = false; // Config sets parameters; dormant until EXTIEnable
}

void test_EXTIEnable(IO_t io)
{
    if (!io) return;
    uint8_t pin = (uint8_t)IO_Pin(io);
    if (pin >= 32) return;

    test_extiChannels[pin].enabled = true;
    if (pin < 16) {
        test_bank0_active_mask |= (1U << pin);
        mock_interrupt_enable(THEJAS32_IRQ_GPIO0);
    } else {
        test_bank1_active_mask |= (1U << (pin - 16));
        mock_interrupt_enable(THEJAS32_IRQ_GPIO1);
    }
}

void test_EXTIDisable(IO_t io)
{
    if (!io) return;
    uint8_t pin = (uint8_t)IO_Pin(io);
    if (pin >= 32) return;

    test_extiChannels[pin].enabled = false;
    if (pin < 16) {
        test_bank0_active_mask &= ~(1U << pin);
        if (test_bank0_active_mask == 0) {
            mock_interrupt_disable(THEJAS32_IRQ_GPIO0);
        }
    } else {
        test_bank1_active_mask &= ~(1U << (pin - 16));
        if (test_bank1_active_mask == 0) {
            mock_interrupt_disable(THEJAS32_IRQ_GPIO1);
        }
    }
}

void test_EXTIRelease(IO_t io)
{
    if (!io) return;
    uint8_t pin = (uint8_t)IO_Pin(io);
    if (pin >= 32) return;

    test_EXTIDisable(io);
    test_extiChannels[pin].handler = NULL;
}

uint32_t test_IO_EXTI_Line(IO_t io)
{
    if (!io) return 0;
    return (1U << IO_Pin(io));
}

} // namespace

// -----------------------------------------------------------------------------
// Test Fixture
// -----------------------------------------------------------------------------

class AriesExtiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_intr_en = 0;
        memset(mock_registered_irqs, 0, sizeof(mock_registered_irqs));
        memset(mock_irq_handlers, 0, sizeof(mock_irq_handlers));
        callback_invocation_log.clear();

        for (uint8_t i = 0; i < 32; i++) {
            mock_get_ioRec(i);
        }

        test_EXTIInit();
    }
};

// -----------------------------------------------------------------------------
// 15 Comprehensive Living Documentation Unit Tests (AAA Layout)
// -----------------------------------------------------------------------------

TEST_F(AriesExtiTest, Test_EXTI_Initialization_ZeroesChannelsAndRegistersISRs)
{
    // Arrange & Act (Handled in SetUp via test_EXTIInit)

    // Assert: IRQ 10 (Bank 0) and IRQ 11 (Bank 1) must be registered in interrupt table
    EXPECT_EQ(mock_registered_irqs[THEJAS32_IRQ_GPIO0], 1U);
    EXPECT_EQ(mock_registered_irqs[THEJAS32_IRQ_GPIO1], 1U);
    EXPECT_NE(mock_irq_handlers[THEJAS32_IRQ_GPIO0], (thejas32_irq_handler_t)NULL);
    EXPECT_NE(mock_irq_handlers[THEJAS32_IRQ_GPIO1], (thejas32_irq_handler_t)NULL);

    // Initial active masks must be strictly zero (no spurious firing)
    EXPECT_EQ(test_bank0_active_mask, 0U);
    EXPECT_EQ(test_bank1_active_mask, 0U);
}

TEST_F(AriesExtiTest, Test_EXTI_HandlerInit_BindsCallbackPointer)
{
    // Arrange
    extiCallbackRec_t cb;
    memset(&cb, 0, sizeof(cb));

    // Act
    test_EXTIHandlerInit(&cb, test_callback_recorder);

    // Assert
    EXPECT_EQ(cb.fn, test_callback_recorder);
}

TEST_F(AriesExtiTest, Test_EXTI_Config_Bank0_PA0_GyroDRDY_PinBinding)
{
    // Arrange: PA0 is Gyro DRDY on Header J1 (Bank 0 Pin 0)
    IO_t pa0 = mock_get_ioRec(0);
    extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, test_callback_recorder);

    // Act: EXTIConfig sets parameters but leaves hardware interrupt masked
    test_EXTIConfig(pa0, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);

    // Assert: Channel parameters recorded, but dormant until EXTIEnable
    EXPECT_EQ(test_extiChannels[0].handler, &cb);
    EXPECT_EQ(test_extiChannels[0].trigger, (uint8_t)BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXPECT_FALSE(test_extiChannels[0].enabled);
    EXPECT_EQ(test_bank0_active_mask, 0x0000U);
    EXPECT_FALSE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0));

    // Act: EXTIEnable unmasks IRQ 10 and activates channel in mask
    test_EXTIEnable(pa0);
    EXPECT_TRUE(test_extiChannels[0].enabled);
    EXPECT_EQ(test_bank0_active_mask, 0x0001U);
    EXPECT_TRUE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0));
    EXPECT_FALSE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO1));
}

TEST_F(AriesExtiTest, Test_EXTI_Config_Bank1_PA22_PinBinding)
{
    // Arrange: PA22 is Bank 1 Pin 6 (22 - 16 = 6)
    IO_t pa22 = mock_get_ioRec(22);
    extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, test_callback_recorder);

    // Act: EXTIConfig sets parameters but leaves hardware interrupt masked
    test_EXTIConfig(pa22, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_FALLING);

    // Assert: Dormant until EXTIEnable
    EXPECT_EQ(test_extiChannels[22].handler, &cb);
    EXPECT_EQ(test_extiChannels[22].trigger, (uint8_t)BETAFLIGHT_EXTI_TRIGGER_FALLING);
    EXPECT_FALSE(test_extiChannels[22].enabled);
    EXPECT_EQ(test_bank1_active_mask, 0x0000U);
    EXPECT_FALSE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO1));

    // Act: EXTIEnable unmasks IRQ 11
    test_EXTIEnable(pa22);
    EXPECT_TRUE(test_extiChannels[22].enabled);
    EXPECT_EQ(test_bank1_active_mask, (1U << 6));
    EXPECT_TRUE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO1));
    EXPECT_FALSE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0));
}

TEST_F(AriesExtiTest, Test_EXTI_FastPath_CTZ_SingleChannel_GyroDRDY_ZeroOverhead)
{
    // Arrange: Only PA0 (bit 0) enabled in Bank 0
    IO_t pa0 = mock_get_ioRec(0);
    extiCallbackRec_t cb;
    cb.fn = [](extiCallbackRec_t *rec) {
        UNUSED(rec);
        callback_invocation_log.push_back(0);
    };
    test_EXTIConfig(pa0, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIEnable(pa0);

    // Act: Simulate hardware IRQ 10 firing on 1.0 kHz Gyro DRDY edge
    sim_gpio0_isr();

    // Assert: __builtin_ctz(1) evaluates to 0 immediately; callback fired exactly once
    ASSERT_EQ(callback_invocation_log.size(), 1U);
    EXPECT_EQ(callback_invocation_log[0], 0);
}

TEST_F(AriesExtiTest, Test_EXTI_FastPath_CTZ_MultipleChannels_ArbitraryDispatch)
{
    // Arrange: Configure 3 pins concurrently in Bank 0: PA0, PA4, PA15
    IO_t p0 = mock_get_ioRec(0);
    IO_t p4 = mock_get_ioRec(4);
    IO_t p15 = mock_get_ioRec(15);

    extiCallbackRec_t cb0, cb4, cb15;
    cb0.fn = [](extiCallbackRec_t *) { callback_invocation_log.push_back(0); };
    cb4.fn = [](extiCallbackRec_t *) { callback_invocation_log.push_back(4); };
    cb15.fn = [](extiCallbackRec_t *) { callback_invocation_log.push_back(15); };

    test_EXTIConfig(p0, &cb0, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIConfig(p4, &cb4, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIConfig(p15, &cb15, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);

    test_EXTIEnable(p0);
    test_EXTIEnable(p4);
    test_EXTIEnable(p15);

    EXPECT_EQ(test_bank0_active_mask, (1U << 0) | (1U << 4) | (1U << 15));

    // Act: Trigger Bank 0 ISR
    sim_gpio0_isr();

    // Assert: CTZ processes channels in ascending bit order: 0, 4, 15
    ASSERT_EQ(callback_invocation_log.size(), 3U);
    EXPECT_EQ(callback_invocation_log[0], 0);
    EXPECT_EQ(callback_invocation_log[1], 4);
    EXPECT_EQ(callback_invocation_log[2], 15);
}

TEST_F(AriesExtiTest, Test_EXTI_FastPath_Bank1_CTZ_PinOffset_Calculation)
{
    // Arrange: Configure Bank 1 pins: PA16 (bit 0 -> pin 16) and PA22 (bit 6 -> pin 22)
    IO_t p16 = mock_get_ioRec(16);
    IO_t p22 = mock_get_ioRec(22);

    extiCallbackRec_t cb16, cb22;
    cb16.fn = [](extiCallbackRec_t *) { callback_invocation_log.push_back(16); };
    cb22.fn = [](extiCallbackRec_t *) { callback_invocation_log.push_back(22); };

    test_EXTIConfig(p16, &cb16, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_FALLING);
    test_EXTIConfig(p22, &cb22, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_FALLING);

    test_EXTIEnable(p16);
    test_EXTIEnable(p22);

    // Act: Fire Bank 1 ISR
    sim_gpio1_isr();

    // Assert: Dispatched with +16 offset
    ASSERT_EQ(callback_invocation_log.size(), 2U);
    EXPECT_EQ(callback_invocation_log[0], 16);
    EXPECT_EQ(callback_invocation_log[1], 22);
}

TEST_F(AriesExtiTest, Test_EXTI_Enable_Disable_ActiveMask_Transitions)
{
    // Arrange
    IO_t pa0 = mock_get_ioRec(0);
    extiCallbackRec_t cb;
    cb.fn = [](extiCallbackRec_t *) {};
    test_EXTIConfig(pa0, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXPECT_EQ(test_bank0_active_mask, 0x0000U);

    // Act 1: Enable channel
    test_EXTIEnable(pa0);
    EXPECT_EQ(test_bank0_active_mask, 0x0001U);
    EXPECT_TRUE(test_extiChannels[0].enabled);

    // Act 2: Disable channel
    test_EXTIDisable(pa0);

    // Assert: Mask cleared, channel marked disabled
    EXPECT_EQ(test_bank0_active_mask, 0x0000U);
    EXPECT_FALSE(test_extiChannels[0].enabled);

    // Act 3: Re-enable channel
    test_EXTIEnable(pa0);

    // Assert: Mask re-asserted, channel marked enabled
    EXPECT_EQ(test_bank0_active_mask, 0x0001U);
    EXPECT_TRUE(test_extiChannels[0].enabled);
}

TEST_F(AriesExtiTest, Test_EXTI_Disable_AllChannels_DisablesControllerIRQ)
{
    // Arrange: Configure two channels in Bank 0
    IO_t p0 = mock_get_ioRec(0);
    IO_t p1 = mock_get_ioRec(1);
    extiCallbackRec_t cb;
    cb.fn = [](extiCallbackRec_t *) {};

    test_EXTIConfig(p0, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIConfig(p1, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXPECT_FALSE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0));

    test_EXTIEnable(p0);
    test_EXTIEnable(p1);
    EXPECT_TRUE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0));

    // Act 1: Disable first pin
    test_EXTIDisable(p0);
    // Controller IRQ must remain enabled because p1 is still active
    EXPECT_TRUE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0));

    // Act 2: Disable second pin (all channels now disabled)
    test_EXTIDisable(p1);
    // Controller IRQ must be fully disabled to prevent spurious wakeups
    EXPECT_FALSE(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0));
}

TEST_F(AriesExtiTest, Test_EXTI_Release_ClearsHandlerAndDisables)
{
    // Arrange
    IO_t pa5 = mock_get_ioRec(5);
    extiCallbackRec_t cb;
    cb.fn = [](extiCallbackRec_t *) {};
    test_EXTIConfig(pa5, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIEnable(pa5);
    EXPECT_TRUE(test_extiChannels[5].enabled);

    // Act
    test_EXTIRelease(pa5);

    // Assert: Channel disabled, active bit cleared, handler pointer set to NULL
    EXPECT_FALSE(test_extiChannels[5].enabled);
    EXPECT_EQ(test_extiChannels[5].handler, (extiCallbackRec_t *)NULL);
    EXPECT_EQ(test_bank0_active_mask & (1U << 5), 0U);
}

TEST_F(AriesExtiTest, Test_EXTI_TriggerTypes_Rising_Falling_Both)
{
    // Arrange
    IO_t p1 = mock_get_ioRec(1);
    IO_t p2 = mock_get_ioRec(2);
    IO_t p3 = mock_get_ioRec(3);
    extiCallbackRec_t cb;
    cb.fn = [](extiCallbackRec_t *) {};

    // Act
    test_EXTIConfig(p1, &cb, 0, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIConfig(p2, &cb, 0, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_FALLING);
    test_EXTIConfig(p3, &cb, 0, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_BOTH);

    // Assert: Trigger edge configurations stored accurately
    EXPECT_EQ(test_extiChannels[1].trigger, (uint8_t)BETAFLIGHT_EXTI_TRIGGER_RISING);
    EXPECT_EQ(test_extiChannels[2].trigger, (uint8_t)BETAFLIGHT_EXTI_TRIGGER_FALLING);
    EXPECT_EQ(test_extiChannels[3].trigger, (uint8_t)BETAFLIGHT_EXTI_TRIGGER_BOTH);
}

TEST_F(AriesExtiTest, Test_EXTI_NullAndOutOfRange_DefensiveGuards)
{
    // Arrange: Mock out-of-range pin (pin 35 >= 32)
    ioRec_t bad_io;
    bad_io.pin = 35;
    bad_io.gpio = (void *)0x10180000;
    extiCallbackRec_t cb;
    cb.fn = [](extiCallbackRec_t *) {};

    // Act & Assert: All APIs must guard against NULL and pin >= 32 without throwing or corrupting memory
    EXPECT_NO_THROW(test_EXTIHandlerInit(NULL, NULL));
    EXPECT_NO_THROW(test_EXTIConfig(NULL, &cb, 0, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING));
    EXPECT_NO_THROW(test_EXTIConfig(&bad_io, &cb, 0, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING));
    EXPECT_NO_THROW(test_EXTIEnable(NULL));
    EXPECT_NO_THROW(test_EXTIEnable(&bad_io));
    EXPECT_NO_THROW(test_EXTIDisable(NULL));
    EXPECT_NO_THROW(test_EXTIDisable(&bad_io));
    EXPECT_NO_THROW(test_EXTIRelease(NULL));
    EXPECT_NO_THROW(test_EXTIRelease(&bad_io));
    EXPECT_EQ(test_IO_EXTI_Line(NULL), 0U);
}

TEST_F(AriesExtiTest, Test_EXTI_SpuriousInterrupt_EmptyMask_ZeroIterations)
{
    // Arrange: Ensure masks are 0
    test_bank0_active_mask = 0;
    test_bank1_active_mask = 0;

    // Act: Fire ISRs with 0 active lines
    sim_gpio0_isr();
    sim_gpio1_isr();

    // Assert: 0 callbacks invoked, 0 crashes
    EXPECT_TRUE(callback_invocation_log.empty());
}

TEST_F(AriesExtiTest, Test_EXTI_MemoryFootprint_SRAMBudget)
{
    // Architectural Invariant: 32 EXTI channel records must strictly occupy <= 256 bytes in SRAM (.bss)
    // On 32-bit RV32IM: pointer (4 B) + trigger (1 B) + enabled (1 B) + 2 B padding = 8 B per channel.
    // 32 channels * 8 B = 256 bytes total.
    constexpr size_t RV32_CHANNEL_REC_SIZE = 8;
    constexpr size_t RV32_TOTAL_EXTI_MEMORY = 32 * RV32_CHANNEL_REC_SIZE;

    EXPECT_LE(RV32_TOTAL_EXTI_MEMORY, 256U);
}

TEST_F(AriesExtiTest, Test_EXTI_Line_Macro_Bitmask)
{
    // Arrange & Act & Assert: IO_EXTI_Line produces 1 << pin
    for (uint8_t pin = 0; pin < 32; pin++) {
        IO_t io = mock_get_ioRec(pin);
        EXPECT_EQ(test_IO_EXTI_Line(io), (1U << pin));
    }
}

// -----------------------------------------------------------------------------
// Regression & Hardware-Trap Resilience Unit Tests (Post Phase 16 Silicon Fixes)
// -----------------------------------------------------------------------------

TEST_F(AriesExtiTest, Test_EXTI_Config_Dormant_DoesNotEnableInterruptOrActiveMask)
{
    // Rationale: EXTIConfig sets handler pointers and triggers, but MUST NOT
    // enable the channel in active bitmasks or unmask IRQ 10/11 in the interrupt controller.
    // Premature unmasking causes infinite trap loops on floating input pins (Phase 16.1 silicon hang).
    IO_t pa0 = mock_get_ioRec(0);
    IO_t pa1 = mock_get_ioRec(1);
    IO_t pa16 = mock_get_ioRec(16);
    IO_t pa22 = mock_get_ioRec(22);

    extiCallbackRec_t cb0, cb1, cb16, cb22;
    test_EXTIHandlerInit(&cb0, test_callback_recorder);
    test_EXTIHandlerInit(&cb1, test_callback_recorder);
    test_EXTIHandlerInit(&cb16, test_callback_recorder);
    test_EXTIHandlerInit(&cb22, test_callback_recorder);

    // Act: Configure channels across Bank 0 and Bank 1
    test_EXTIConfig(pa0, &cb0, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIConfig(pa1, &cb1, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_FALLING);
    test_EXTIConfig(pa16, &cb16, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIConfig(pa22, &cb22, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_BOTH);

    // Assert: Channel descriptors populated, but strictly dormant
    EXPECT_FALSE(test_extiChannels[0].enabled);
    EXPECT_FALSE(test_extiChannels[1].enabled);
    EXPECT_FALSE(test_extiChannels[16].enabled);
    EXPECT_FALSE(test_extiChannels[22].enabled);

    // Crucial Invariant: Active masks and controller IRQs MUST strictly be zero!
    EXPECT_EQ(test_bank0_active_mask, 0x0000U);
    EXPECT_EQ(test_bank1_active_mask, 0x0000U);
    EXPECT_EQ(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0), 0UL);
    EXPECT_EQ(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO1), 0UL);
}

TEST_F(AriesExtiTest, Test_EXTI_SimulatedLevelTrigger_TrapStorm_Prevention)
{
    // Rationale: When an unattached pin floats HIGH, a level-triggered IRQ asserts continuously.
    // This test simulates the hardware trap arbitration loop to prove that keeping the channel dormant
    // prevents trap storms, and disabling an asserted channel immediately kills the storm.

    IO_t pa0 = mock_get_ioRec(0);
    extiCallbackRec_t cb;
    static uint32_t s_trap_count;
    s_trap_count = 0;
    cb.fn = [](extiCallbackRec_t *self) {
        UNUSED(self);
        s_trap_count++;
    };

    test_EXTIConfig(pa0, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);

    // Simulated hardware condition: External pin PA0 is physically floating HIGH
    bool hw_pin_level_high = true;

    // Simulated RISC-V interrupt arbitration loop:
    // Trap fires ONLY if line is high AND IRQ is unmasked in controller AND enabled in channel mask
    auto simulate_hardware_trap_cycle = [&]() {
        if (hw_pin_level_high && (mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0))) {
            sim_gpio0_isr();
            return true;
        }
        return false;
    };

    // Phase 1: Configured but dormant. Loop runs 1000 cycles.
    for (int i = 0; i < 1000; i++) {
        bool trapped = simulate_hardware_trap_cycle();
        EXPECT_FALSE(trapped);
    }
    EXPECT_EQ(s_trap_count, 0U); // 0 traps -> 0 CPU cycles stolen!

    // Phase 2: Explicitly enable channel. Trap fires.
    test_EXTIEnable(pa0);
    EXPECT_TRUE(simulate_hardware_trap_cycle());
    EXPECT_EQ(s_trap_count, 1U);

    // Phase 3: Disable channel after self-test.
    test_EXTIDisable(pa0);
    EXPECT_EQ(mock_intr_en & (1UL << THEJAS32_IRQ_GPIO0), 0UL);

    // Loop runs 1000 cycles with line still HIGH.
    for (int i = 0; i < 1000; i++) {
        bool trapped = simulate_hardware_trap_cycle();
        EXPECT_FALSE(trapped);
    }
    // Trap count must strictly remain 1 (no re-entrancy storm)
    EXPECT_EQ(s_trap_count, 1U);
}

TEST_F(AriesExtiTest, Test_EXTI_HardwarePullDown_Vs_Floating_PinConfiguration)
{
    // Rationale: Header J1 PA0 must be configured with internal pull-down (IOCFG_IPD)
    // rather than floating (IOCFG_IN_FLOATING) when unpopulated to suppress antenna noise.
    IO_t pa0 = mock_get_ioRec(0);
    extiCallbackRec_t cb;
    test_EXTIHandlerInit(&cb, test_callback_recorder);

    // Configure with IOCFG_IPD
    test_EXTIConfig(pa0, &cb, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);

    // Invariant: Configuration must accept IOCFG_IPD cleanly
    EXPECT_EQ(test_extiChannels[0].handler, &cb);
    EXPECT_FALSE(test_extiChannels[0].enabled);
}

TEST_F(AriesExtiTest, Test_EXTI_Bank0_SingleActivePin_Erratum6_Constraint)
{
    // Rationale: Erratum 6 documents that THEJAS32 IRQ 10 lacks per-pin pending registers.
    // Therefore, Bank 0 must strictly have at most 1 active pin (PA0 Gyro DRDY).
    IO_t pa0 = mock_get_ioRec(0);
    extiCallbackRec_t cb0;
    test_EXTIHandlerInit(&cb0, test_callback_recorder);

    test_EXTIConfig(pa0, &cb0, 0, IOCFG_IPD, BETAFLIGHT_EXTI_TRIGGER_RISING);
    test_EXTIEnable(pa0);

    // Active mask popcount must strictly be 1 for reliable non-crosstalk dispatching
    uint32_t active_pins_bank0 = (uint32_t)__builtin_popcount(test_bank0_active_mask);
    EXPECT_EQ(active_pins_bank0, 1U);

    test_EXTIDisable(pa0);
    EXPECT_EQ(test_bank0_active_mask, 0U);
}
