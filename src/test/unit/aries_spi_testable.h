/*
 * Testable interface for ARIES v3.0 SPI Master Bus Driver.
 * Host unit test shim mirroring src/platform/ARIES/bus_spi_aries.c.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THEJAS32_SPI_BUSY            (1 << 4)
#define THEJAS32_SPI_OVERRUN         (1 << 5)
#define THEJAS32_SPI_RX_COMPLETE     (1 << 6)
#define THEJAS32_SPI_TX_HOLD_EMPTY   (1 << 7)
#define THEJAS32_SPI_CTRL_CSAAT      (1 << 8)

typedef struct {
    volatile uint32_t Control;
    volatile uint32_t Status;
    volatile uint32_t Baudrate;
    volatile uint32_t TxData;
    volatile uint32_t RxData;
} test_thejas32_spi_reg_t;

// Pure mathematical clock/prescaler functions
uint16_t spiCalculateDivider(uint32_t freq);
uint32_t spiCalculateClock(uint16_t divisor);

// Hardware device control & transfer functions
void test_spiInitDevice(uint8_t ch);
uint8_t test_thejas32_spi_transfer_byte(test_thejas32_spi_reg_t *spi, uint8_t out);
bool test_spiInternalReadWriteBufPolled(test_thejas32_spi_reg_t *spi, const uint8_t *txData, uint8_t *rxData, int len);
void test_spiSequenceStartMock(test_thejas32_spi_reg_t *spi, uint16_t speed, bool leadingEdge);

// Register access helpers for test assertion
test_thejas32_spi_reg_t *test_spi_get_device(uint8_t ch);

#ifdef __cplusplus
}
#endif
