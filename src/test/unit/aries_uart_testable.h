/*
 * Testable interface for ARIES v3.0 UART 16550 Driver.
 * Host unit test shim mirroring src/platform/ARIES/serial_uart_aries.c.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "common/time.h"

#ifdef __cplusplus
extern "C" {
#endif

// 16550 Register offsets and bit masks
#define UART_LSR_DR     0x01 // Data Ready
#define UART_LSR_THRE   0x20 // Transmitter Holding Register Empty
#define UART_IE_RDA     0x01 // Received Data Available Interrupt Enable
#define UART_IE_THRE    0x02 // Transmitter Holding Register Empty Interrupt Enable

// UART port structure simplified for unit testing
typedef void (*test_rx_callback_t)(uint8_t byte, void *data);

typedef struct {
    uint8_t *rxBuffer;
    uint8_t *txBuffer;
    uint32_t rxBufferSize;
    uint32_t txBufferSize;
    uint32_t rxBufferHead;
    uint32_t rxBufferTail;
    uint32_t txBufferHead;
    uint32_t txBufferTail;
    uint32_t baudRate;
    uint8_t  options;
    uint8_t  mode;
    test_rx_callback_t rxCallback;
    void *rxCallbackData;
} test_serial_port_t;

typedef struct {
    uint32_t reg_base;
    test_serial_port_t port;
} test_uart_port_t;

#define TEST_SERIAL_STOPBITS_1 0x00
#define TEST_SERIAL_STOPBITS_2 0x04
#define TEST_SERIAL_PARITY_NO  0x00
#define TEST_SERIAL_PARITY_EVEN 0x18
#define TEST_MODE_RX          0x01
#define TEST_MODE_TX          0x02

// Driver functions under test
uint8_t test_uartGetIndex(uint32_t reg_addr);
void test_thejas32_uart_set_baudrate(uint8_t ch, uint32_t baudRate);
void test_thejas32_uart_set_baudrate_ex(uint8_t ch, uint32_t baudRate, bool resetFifo);
void test_uartReconfigure(test_uart_port_t *uartPort);
void test_uartEnableTxInterrupt(test_uart_port_t *uartPort);
void test_uartIrqHandler(test_uart_port_t *s);
uint8_t test_uartRead(test_uart_port_t *instance);
uint32_t test_uartTotalTxBytesFree(test_uart_port_t *uartPort);
bool test_uartCanWrite(const test_uart_port_t *uartPort, uint16_t txPin);
uint8_t test_uartSanitizeMode(uint16_t rxPin, uint16_t txPin, uint8_t mode);
void test_uartPinConfigure(test_uart_port_t *uartPort, uint16_t *rxPin, uint16_t *txPin, uint16_t defaultRx, uint16_t defaultTx);

// MMIO register accessor helpers for test assertions
uint32_t test_uart_read_reg(uint8_t ch, uint32_t offset);
void test_uart_write_reg(uint8_t ch, uint32_t offset, uint32_t val);
uint32_t test_uart_get_iir_read_count(uint8_t ch);
void test_uart_reset_iir_read_count(void);

#ifdef __cplusplus
}
#endif
