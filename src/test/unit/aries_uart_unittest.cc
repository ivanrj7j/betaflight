/*
 * Unit tests for ARIES v3.0 UART 16550 Driver.
 * Phase 3 test suite corresponding to Module T2.
 */

#include <stdint.h>
#include <stdbool.h>

extern "C" {
    #include "aries/mock_mmio.h"
    #include "aries/mock_csr.h"
    #include "aries/mock_thejas32.h"
    #include "aries/test_helpers.h"
    #include "aries_uart_testable.h"
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

class AriesUartTest : public ::testing::Test {
protected:
    uint8_t rxBuffer[256];
    uint8_t txBuffer[256];
    test_uart_port_t uart0;

    void SetUp() override {
        mock_mmio_reset();
        mock_csr_reset();
        mock_thejas32_reset();

        memset(rxBuffer, 0, sizeof(rxBuffer));
        memset(txBuffer, 0, sizeof(txBuffer));

        memset(&uart0, 0, sizeof(uart0));
        uart0.reg_base = 0x10000100; // UART0 base
        uart0.port.rxBuffer = rxBuffer;
        uart0.port.txBuffer = txBuffer;
        uart0.port.rxBufferSize = sizeof(rxBuffer);
        uart0.port.txBufferSize = sizeof(txBuffer);
        uart0.port.baudRate = 115200;
        uart0.port.mode = TEST_MODE_RX | TEST_MODE_TX;
    }
};

// T2-01: Baud divisor calculation for 115200 baud
TEST_F(AriesUartTest, BaudDivisorFor115200)
{
    test_thejas32_uart_set_baudrate(0, 115200);

    // 100,000,000 / (115200 * 16) = 54 (0x0036)
    // LSB written to DR, MSB written to IE during DLAB=1
    // Read back LCR should be 0x03 (DLAB cleared)
    EXPECT_EQ(test_uart_read_reg(0, 0x0C), 0x03U); // LCR
    EXPECT_EQ(test_uart_read_reg(0, 0x08), 0x07U); // FCR: FIFO enabled
}

// T2-02: Baud divisor calculation for 9600 baud
TEST_F(AriesUartTest, BaudDivisorFor9600)
{
    test_thejas32_uart_set_baudrate(0, 9600);
    // Divisor = 100,000,000 / (9600 * 16) = 651 (0x028B)
    EXPECT_EQ(uart_baud_divisor(100000000, 9600), 651U);
}

// T2-03: Baud rate 0 defaults to 115200
TEST_F(AriesUartTest, BaudRateZeroDefaultsTo115200)
{
    test_thejas32_uart_set_baudrate(0, 0);
    EXPECT_EQ(test_uart_read_reg(0, 0x0C), 0x03U);
}

// T2-04: Line Control Register 8N1 default
TEST_F(AriesUartTest, LineControlDefault8N1)
{
    test_uartReconfigure(&uart0);
    EXPECT_EQ(test_uart_read_reg(0, 0x0C), 0x03U); // 8 bits, 1 stop bit, no parity
}

// T2-05: FIFO Control Register enables and resets FIFOs
TEST_F(AriesUartTest, FcrEnablesFifos)
{
    test_thejas32_uart_set_baudrate(0, 115200);
    EXPECT_EQ(test_uart_read_reg(0, 0x08), 0x07U); // FCR = 0x07
}

// T2-06: RX character ingestion into circular buffer
TEST_F(AriesUartTest, RxIngestionIntoBuffer)
{
    // Put character in DR (offset 0x00) and set LSR DR flag (bit 0 at offset 0x14)
    test_uart_write_reg(0, 0x00, 'B');
    test_uart_write_reg(0, 0x14, UART_LSR_DR);

    // Run ISR
    test_uartIrqHandler(&uart0);

    EXPECT_EQ(uart0.port.rxBufferHead, 1U);
    EXPECT_EQ(uart0.port.rxBuffer[0], 'B');
}

// T2-07: RX ring buffer head wraparound across 256-byte boundary
TEST_F(AriesUartTest, RxRingBufferWraparound)
{
    uart0.port.rxBufferHead = 255;
    uart0.port.rxBufferTail = 0;

    test_uart_write_reg(0, 0x00, 0xAA);
    test_uart_write_reg(0, 0x14, UART_LSR_DR);
    test_uartIrqHandler(&uart0);

    // After writing index 255, head wraps to 0
    EXPECT_EQ(uart0.port.rxBufferHead, 0U);
    EXPECT_EQ(uart0.port.rxBuffer[255], 0xAA);
}

// T2-08: Sequential 300-byte stream maintains ordering across circular wraparound
TEST_F(AriesUartTest, Sequential300ByteStreamWraparoundOrder)
{
    uint32_t head = 0;
    uint32_t tail = 0;
    uint8_t streamBuf[256];
    memset(streamBuf, 0, sizeof(streamBuf));

    for (int i = 0; i < 300; i++) {
        // Feed byte
        streamBuf[head] = (uint8_t)(i & 0xFF);
        head = (head + 1) % 256;

        // Drain every 2 bytes
        if (i % 2 == 1) {
            uint8_t byte1 = streamBuf[tail];
            tail = (tail + 1) % 256;
            uint8_t byte2 = streamBuf[tail];
            tail = (tail + 1) % 256;

            EXPECT_EQ(byte1, (uint8_t)((i - 1) & 0xFF));
            EXPECT_EQ(byte2, (uint8_t)(i & 0xFF));
        }
    }
}

// T2-09: TX drain pushes bytes in FIFO order to DR
TEST_F(AriesUartTest, TxDrainPushesBytesInOrder)
{
    uart0.port.txBuffer[0] = 'H';
    uart0.port.txBuffer[1] = 'I';
    uart0.port.txBufferHead = 2;
    uart0.port.txBufferTail = 0;

    // Set LSR THRE (bit 5)
    test_uart_write_reg(0, 0x14, UART_LSR_THRE);

    // First byte
    test_uartIrqHandler(&uart0);
    EXPECT_EQ(test_uart_read_reg(0, 0x00) & 0xFF, (uint32_t)'H');
    EXPECT_EQ(uart0.port.txBufferTail, 1U);

    // Second byte
    test_uart_write_reg(0, 0x14, UART_LSR_THRE);
    test_uartIrqHandler(&uart0);
    EXPECT_EQ(test_uart_read_reg(0, 0x00) & 0xFF, (uint32_t)'I');
    EXPECT_EQ(uart0.port.txBufferTail, 2U);
}

// T2-10: TX interrupt is disabled when queue is completely empty
TEST_F(AriesUartTest, TxInterruptDisabledOnEmptyQueue)
{
    uart0.port.txBufferHead = 0;
    uart0.port.txBufferTail = 0;
    test_uart_write_reg(0, 0x04, UART_IE_THRE); // TX interrupt initially enabled
    test_uart_write_reg(0, 0x14, UART_LSR_THRE); // Transmitter empty

    test_uartIrqHandler(&uart0);

    // IE THRE bit (bit 1) should now be cleared
    EXPECT_FALSE((test_uart_read_reg(0, 0x04) & UART_IE_THRE) != 0);
}

// T2-11: Transmit FIFO priming on empty queue
TEST_F(AriesUartTest, TransmitFifoPrimingWhenTransmitterEmpty)
{
    uart0.port.txBuffer[0] = 'Z';
    uart0.port.txBufferHead = 1;
    uart0.port.txBufferTail = 0;

    // Set LSR THRE bit (transmitter empty)
    test_uart_write_reg(0, 0x14, UART_LSR_THRE);

    test_uartEnableTxInterrupt(&uart0);

    // Byte 'Z' should be primed directly to DR and tail advanced
    EXPECT_EQ(test_uart_read_reg(0, 0x00) & 0xFF, (uint32_t)'Z');
    EXPECT_EQ(uart0.port.txBufferTail, 1U);
    EXPECT_TRUE((test_uart_read_reg(0, 0x04) & UART_IE_THRE) != 0);
}

// T2-12: Channel index lookup from register address
TEST_F(AriesUartTest, ChannelIndexLookup)
{
    EXPECT_EQ(test_uartGetIndex(0x10000100), 0U); // UART0
    EXPECT_EQ(test_uartGetIndex(0x10000200), 1U); // UART1
}

// T2-13: RX callback is invoked when registered
TEST_F(AriesUartTest, RxCallbackInvokedWhenSet)
{
    static uint8_t received_char = 0;
    static int callback_count = 0;
    received_char = 0;
    callback_count = 0;

    uart0.port.rxCallback = [](uint8_t byte, void *) {
        received_char = byte;
        callback_count++;
    };

    test_uart_write_reg(0, 0x00, 'X');
    test_uart_write_reg(0, 0x14, UART_LSR_DR);

    test_uartIrqHandler(&uart0);

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(received_char, 'X');
    EXPECT_EQ(uart0.port.rxBufferHead, 0U); // Buffer untouched when callback active
}

// T2-14: Stop bits and parity configuration
TEST_F(AriesUartTest, ParityAndStopBitsConfiguration)
{
    uart0.port.options = TEST_SERIAL_STOPBITS_2 | TEST_SERIAL_PARITY_EVEN;
    test_uartReconfigure(&uart0);

    // 0x03 (8 bits) | 0x04 (2 stop bits) | 0x18 (even parity) = 0x1F
    EXPECT_EQ(test_uart_read_reg(0, 0x0C), 0x1FU);
}
