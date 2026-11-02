/*
 * End-to-End System Integration and Safety Stress Tests for ARIES v3.0 (THEJAS32).
 * Test Suite: Module INT (Phase 8)
 */

#include "unittest_macros.h"
#include "gtest/gtest.h"

extern "C" {
    #include "aries/mock_mmio.h"
    #include "aries/mock_csr.h"
    #include "aries/mock_thejas32.h"
    #include "aries_system_testable.h"
    #include "aries_uart_testable.h"
    #include "aries_gpio_testable.h"
    #include "aries_exti_testable.h"
    #include "aries_flash_testable.h"
    #include "aries_motor_testable.h"
}

#define THEJAS32_IRQ_TIMER0 7
#define THEJAS32_IRQ_TIMER1 8
#define THEJAS32_IRQ_GPIO0  10
#define THEJAS32_IRQ_GPIO1  11

typedef struct {
    volatile uint32_t RAW_INTR;     // 0x00
    volatile uint32_t dummy0;       // 0x04
    volatile uint32_t INTR_EN;      // 0x08
    volatile uint32_t dummy1;       // 0x0C
    volatile uint32_t INTR_STATUS;  // 0x10
} test_intr_ctrl_t;

#define MOCK_INTR_CTRL (*((test_intr_ctrl_t *)mock_intctrl_mem))

typedef struct {
    volatile uint32_t LoadCount;
    volatile uint32_t CurrentValue;
    volatile uint32_t Control;
    volatile uint32_t EOI;
    volatile uint32_t IntrStatus;
} test_timer0_reg_t;

#define MOCK_TIMER0 (*((test_timer0_reg_t *)&mock_periph_mem[0x0A00]))

static int storm_counts[12] = {0};

template <int N>
void storm_handler() {
    storm_counts[N]++;
}

class AriesIntegrationTest : public ::testing::Test {
protected:
    uint8_t rxBuffer[256];
    uint8_t txBuffer[256];
    test_uart_port_t uart0;

    void SetUp() override {
        mock_mmio_reset();
        mock_csr_reset();
        mock_thejas32_reset();
        test_system_reset_state();

        SystemCoreClock = 100000000;
        cycleCounterInit();
        test_IOInitGlobal();
        test_flash_reset();
        test_motor_reset();

        MOCK_TIMER0.CurrentValue = 0xFFFFFFFFUL;
        MOCK_TIMER0.IntrStatus = 0;

        memset(rxBuffer, 0, sizeof(rxBuffer));
        memset(txBuffer, 0, sizeof(txBuffer));
        memset(&uart0, 0, sizeof(uart0));

        uart0.reg_base = 0x10000100; // UART0
        uart0.port.rxBuffer = rxBuffer;
        uart0.port.txBuffer = txBuffer;
        uart0.port.rxBufferSize = 256;
        uart0.port.txBufferSize = 256;
        uart0.port.rxBufferHead = 0;
        uart0.port.rxBufferTail = 0;
        uart0.port.txBufferHead = 0;
        uart0.port.txBufferTail = 0;
        uart0.port.baudRate = 115200;
        uart0.port.mode = TEST_MODE_RX | TEST_MODE_TX;

        memset(storm_counts, 0, sizeof(storm_counts));
    }
};

// INT-01: Full system initialization sequence
TEST_F(AriesIntegrationTest, FullSystemInitSequence) {
    cycleCounterInit();
    thejas32_timer_init();
    test_EXTIInit();
    test_motorPwmDevInit(4, MOTOR_PROTOCOL_PWM);

    // Verify interrupt controller registration
    EXPECT_NE(interrupt_table[THEJAS32_IRQ_TIMER0], nullptr);
    EXPECT_NE(interrupt_table[THEJAS32_IRQ_TIMER1], nullptr);
    EXPECT_NE(interrupt_table[THEJAS32_IRQ_GPIO0], nullptr);
    EXPECT_NE(interrupt_table[THEJAS32_IRQ_GPIO1], nullptr);

    // Timers are enabled on dev init
    EXPECT_TRUE((MOCK_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_TIMER0)) != 0);
    EXPECT_TRUE((MOCK_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_TIMER1)) != 0);

    // EXTI bank IRQs 10 & 11 are unmasked upon pin interrupt configuration
    test_extiCallbackRec_t cb0, cb1;
    test_EXTIHandlerInit(&cb0, [](test_extiCallbackRec_t *){});
    test_EXTIHandlerInit(&cb1, [](test_extiCallbackRec_t *){});
    test_EXTIConfig(test_IOGetByPin(0), &cb0, 0, 0, TEST_EXTI_TRIGGER_RISING);
    test_EXTIConfig(test_IOGetByPin(22), &cb1, 0, 0, TEST_EXTI_TRIGGER_RISING);

    EXPECT_TRUE((MOCK_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_GPIO0)) != 0);
    EXPECT_TRUE((MOCK_INTR_CTRL.INTR_EN & (1UL << THEJAS32_IRQ_GPIO1)) != 0);
}

// INT-02: Timer 0 ISR increments overflow, timer0Micros() reflects it
TEST_F(AriesIntegrationTest, Timer0OverflowTracksAccurately) {
    thejas32_timer_init();
    MOCK_TIMER0.CurrentValue = 0xFFFFFFFFUL;
    EXPECT_EQ(timer0GetOverflows(), 0U);

    timeUs_t t0 = timer0Micros();
    EXPECT_EQ(t0, 0U);

    // Simulate 3 timer overflow events
    thejas32_timer0_isr();
    EXPECT_EQ(timer0GetOverflows(), 1U);

    thejas32_timer0_isr();
    EXPECT_EQ(timer0GetOverflows(), 2U);

    thejas32_timer0_isr();
    EXPECT_EQ(timer0GetOverflows(), 3U);

    timeUs_t t3 = timer0Micros();
    // 3 overflows of 0x100000000 cycles / 100 cycles per µs = 3 * 42,949,672 µs
    EXPECT_GE(t3, 128849016U);
}

// INT-03: UART RX ISR -> ring buffer -> waiting byte count consistency
TEST_F(AriesIntegrationTest, UartRxStreamConsistency) {
    test_thejas32_uart_set_baudrate(0, 115200);

    // Stream 64 bytes into hardware UART FIFO and trigger ISR
    for (int i = 0; i < 64; i++) {
        test_uart_write_reg(0, 0x00, (uint8_t)(i + 0x20));
        test_uart_write_reg(0, 0x14, UART_LSR_DR);
        test_uartIrqHandler(&uart0);
    }

    uint32_t waiting = (uart0.port.rxBufferHead >= uart0.port.rxBufferTail) ?
                       (uart0.port.rxBufferHead - uart0.port.rxBufferTail) :
                       (uart0.port.rxBufferSize + uart0.port.rxBufferHead - uart0.port.rxBufferTail);
    EXPECT_EQ(waiting, 64U);

    // Drain and verify data integrity
    for (int i = 0; i < 64; i++) {
        uint8_t byte = uart0.port.rxBuffer[uart0.port.rxBufferTail];
        uart0.port.rxBufferTail = (uart0.port.rxBufferTail + 1) % uart0.port.rxBufferSize;
        EXPECT_EQ(byte, (uint8_t)(i + 0x20));
    }

    waiting = (uart0.port.rxBufferHead >= uart0.port.rxBufferTail) ?
              (uart0.port.rxBufferHead - uart0.port.rxBufferTail) :
              (uart0.port.rxBufferSize + uart0.port.rxBufferHead - uart0.port.rxBufferTail);
    EXPECT_EQ(waiting, 0U);
}

// INT-04: Motor write -> complete update -> ISR sequence drops all 4 pins
TEST_F(AriesIntegrationTest, MotorFullCycleStepByStep) {
    test_activeMotorProtocol = MOTOR_PROTOCOL_PWM;

    // Motor 0: 1100 µs (earliest)
    // Motor 1: 1300 µs
    // Motor 2: 1500 µs
    // Motor 3: 1800 µs (latest)
    test_pwmWriteMotor(0, 1100.0f);
    test_pwmWriteMotor(1, 1300.0f);
    test_pwmWriteMotor(2, 1500.0f);
    test_pwmWriteMotor(3, 1800.0f);

    test_pwmCompleteMotorUpdate();

    // All pins must be HIGH
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(test_motor_is_pin_high(i));
    }
    EXPECT_EQ(test_mock_timer1.LoadCount, 110000U); // Motor 0 deadline

    // Step 1: Motor 0 deadline expires
    test_thejas32_pwm_timer1_isr();
    EXPECT_FALSE(test_motor_is_pin_high(0)); // Dropped!
    EXPECT_TRUE(test_motor_is_pin_high(1));
    EXPECT_TRUE(test_motor_is_pin_high(2));
    EXPECT_TRUE(test_motor_is_pin_high(3));
    EXPECT_EQ(test_mock_timer1.LoadCount, 20000U); // Delta 130,000 - 110,000

    // Step 2: Motor 1 deadline expires
    test_thejas32_pwm_timer1_isr();
    EXPECT_FALSE(test_motor_is_pin_high(1)); // Dropped!
    EXPECT_TRUE(test_motor_is_pin_high(2));
    EXPECT_TRUE(test_motor_is_pin_high(3));
    EXPECT_EQ(test_mock_timer1.LoadCount, 20000U); // Delta 150,000 - 130,000

    // Step 3: Motor 2 deadline expires
    test_thejas32_pwm_timer1_isr();
    EXPECT_FALSE(test_motor_is_pin_high(2)); // Dropped!
    EXPECT_TRUE(test_motor_is_pin_high(3));
    EXPECT_EQ(test_mock_timer1.LoadCount, 30000U); // Delta 180,000 - 150,000

    // Step 4: Motor 3 deadline expires
    test_thejas32_pwm_timer1_isr();
    EXPECT_FALSE(test_motor_is_pin_high(3)); // All dropped!

    // Timer 1 must now be disabled
    EXPECT_EQ(test_mock_timer1.Control, 0x00U);
}

// INT-05: Config write across sector boundary triggers exactly 2 erases
TEST_F(AriesIntegrationTest, ConfigWriteCrossesSectorBoundary) {
    configUnlock();

    uint32_t buf1[8] = {0x11111111, 0x22222222, 0, 0, 0, 0, 0, 0};
    uint32_t buf2[8] = {0x33333333, 0x44444444, 0, 0, 0, 0, 0, 0};

    // Write to end of Sector 0 (0x201F8FE0)
    configStreamerResult_e r1 = configWriteWord(TEST_CONFIG_START_ADDR + 4064, (config_streamer_buffer_type_t *)buf1);
    EXPECT_EQ(r1, CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(test_flash_get_erase_count(), 1U);
    EXPECT_EQ(test_flash_get_last_erased_sector(), 0x1F8000U);

    // Write into Sector 1 (0x201F9000)
    configStreamerResult_e r2 = configWriteWord(TEST_CONFIG_START_ADDR + 4096, (config_streamer_buffer_type_t *)buf2);
    EXPECT_EQ(r2, CONFIG_RESULT_SUCCESS);
    EXPECT_EQ(test_flash_get_erase_count(), 2U);
    EXPECT_EQ(test_flash_get_last_erased_sector(), 0x1F9000U);

    // Verify coherent memory read back across boundary
    volatile uint32_t *p1 = (volatile uint32_t *)(TEST_CONFIG_START_ADDR + 4064);
    EXPECT_EQ(p1[0], 0x11111111U);

    volatile uint32_t *p2 = (volatile uint32_t *)(TEST_CONFIG_START_ADDR + 4096);
    EXPECT_EQ(p2[0], 0x33333333U);
}

// INT-06: Interrupt storm: fire IRQ 0–11 via handle_trap dispatcher
TEST_F(AriesIntegrationTest, InterruptStormDispatch) {
    thejas32_interrupt_register(0, storm_handler<0>);
    thejas32_interrupt_register(1, storm_handler<1>);
    thejas32_interrupt_register(2, storm_handler<2>);
    thejas32_interrupt_register(3, storm_handler<3>);
    thejas32_interrupt_register(4, storm_handler<4>);
    thejas32_interrupt_register(5, storm_handler<5>);
    thejas32_interrupt_register(6, storm_handler<6>);
    thejas32_interrupt_register(7, storm_handler<7>);
    thejas32_interrupt_register(8, storm_handler<8>);
    thejas32_interrupt_register(9, storm_handler<9>);
    thejas32_interrupt_register(10, storm_handler<10>);
    thejas32_interrupt_register(11, storm_handler<11>);

    for (uint8_t irq = 0; irq < 12; irq++) {
        thejas32_interrupt_enable(irq);
    }

    uint32_t sp[32];
    // Fire all IRQs 0..11 via handle_trap dispatcher
    for (uint8_t irq = 0; irq < 12; irq++) {
        MOCK_INTR_CTRL.INTR_STATUS = (1UL << irq);
        handle_trap(0x8000000BU, 0x20000100U, sp);
    }

    for (uint8_t irq = 0; irq < 12; irq++) {
        EXPECT_EQ(storm_counts[irq], 1) << "IRQ " << (int)irq << " did not fire exactly once";
    }
}

// INT-07: micros() monotonic during simulated ISR interleaving
TEST_F(AriesIntegrationTest, MicrosMonotonicDuringIsrInterleaving) {
    mock_csr_reset();
    timeUs_t prev = 0;

    for (int i = 0; i < 5000; i++) {
        mock_csr_advance(100); // 1 µs normal forward step
        timeUs_t t1 = micros();
        EXPECT_GE(t1, prev);

        // Simulate an interrupt stealing 40 cycles (0.4 µs) mid-stream
        mock_csr_advance(40);
        timeUs_t t2 = micros();
        EXPECT_GE(t2, t1);

        prev = t2;
    }
}

// INT-08: Motor shutdown zeroes all GPIO and disables Timer 1
TEST_F(AriesIntegrationTest, MotorShutdownSafety) {
    test_pwmWriteMotor(0, 1900.0f);
    test_pwmWriteMotor(1, 1900.0f);
    test_pwmWriteMotor(2, 1900.0f);
    test_pwmWriteMotor(3, 1900.0f);

    test_pwmCompleteMotorUpdate();
    EXPECT_TRUE(test_motor_is_pin_high(0));
    EXPECT_TRUE(test_motor_is_pin_high(1));
    EXPECT_TRUE(test_motor_is_pin_high(2));
    EXPECT_TRUE(test_motor_is_pin_high(3));
    EXPECT_EQ(test_mock_timer1.Control, 0x03U);

    // Execute immediate shutdown
    test_pwmShutdownPulsesForAllMotors();

    EXPECT_FALSE(test_motor_is_pin_high(0));
    EXPECT_FALSE(test_motor_is_pin_high(1));
    EXPECT_FALSE(test_motor_is_pin_high(2));
    EXPECT_FALSE(test_motor_is_pin_high(3));
    EXPECT_EQ(test_mock_timer1.Control, 0x00U);
}

// INT-09: Emergency Failsafe Motor Cutoff Latency
TEST_F(AriesIntegrationTest, EmergencyFailsafeCutoffLatency) {
    // Quad flying at high throttle
    for (int i = 0; i < 4; i++) {
        test_pwmWriteMotor(i, 1850.0f);
    }
    test_pwmCompleteMotorUpdate();

    // Verify all motors asserting power
    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(test_motor_is_pin_high(i));
    }

    // Failsafe triggers immediately: shutdown pulses
    test_pwmShutdownPulsesForAllMotors();

    // Assert zero-latency cutoff: all motors dead and timer halted
    for (int i = 0; i < 4; i++) {
        EXPECT_FALSE(test_motor_is_pin_high(i));
    }
    EXPECT_EQ(test_mock_timer1.Control, 0x00U);
}
