/*
 * Host-testable implementation of ARIES v3.0 SPI Master Driver logic.
 * Mirrors src/platform/ARIES/bus_spi_aries.c using modular mock infrastructure.
 */

#include "aries_spi_testable.h"
#include "aries/mock_mmio.h"

#define THEJAS32_SPI0_OFFSET 0x600

test_thejas32_spi_reg_t *test_spi_get_device(uint8_t ch)
{
    // ch 0 = SPI0 at offset 0x600 in mock_periph_mem
    return (test_thejas32_spi_reg_t *)&mock_periph_mem[THEJAS32_SPI0_OFFSET + (ch * 0x100)];
}

uint16_t spiCalculateDivider(uint32_t freq)
{
    if (freq >= 20000000) {
        return 0; // 25 MHz
    } else if (freq >= 10000000) {
        return 1; // 12.5 MHz
    } else if (freq >= 5000000) {
        return 2; // 6.25 MHz
    } else if (freq >= 2500000) {
        return 3; // 3.125 MHz
    } else if (freq >= 1200000) {
        return 4; // 1.56 MHz
    } else if (freq >= 600000) {
        return 5; // 781 kHz
    } else if (freq >= 300000) {
        return 6; // 390 kHz
    } else {
        return 7; // 195 kHz
    }
}

uint32_t spiCalculateClock(uint16_t divisor)
{
    switch (divisor) {
    case 0:  return 25000000;
    case 1:  return 12500000;
    case 2:  return 6250000;
    case 3:  return 3125000;
    case 4:  return 1562500;
    case 5:  return 781250;
    case 6:  return 390625;
    case 7:  return 195312;
    default: return 12500000;
    }
}

void test_spiInitDevice(uint8_t ch)
{
    test_thejas32_spi_reg_t *spi = test_spi_get_device(ch);
    if (!spi) {
        return;
    }

    // Default baud divisor: Div by 8 = 12.5 MHz (at 100 MHz SystemCoreClock)
    spi->Baudrate = (1 << 4);

    // Default control word: 8-bit, Mode 0, MSB first, fixed peripheral
    spi->Control = 0x0000;
}

uint8_t test_thejas32_spi_transfer_byte(test_thejas32_spi_reg_t *spi, uint8_t out)
{
    uint32_t timeout = 100000;
    while ((spi->Status & THEJAS32_SPI_BUSY) && --timeout) {
    }

    timeout = 100000;
    while (!(spi->Status & THEJAS32_SPI_TX_HOLD_EMPTY) && --timeout) {
    }

    spi->TxData = out;

    timeout = 100000;
    while (!(spi->Status & THEJAS32_SPI_RX_COMPLETE) && --timeout) {
    }

    return (uint8_t)(spi->RxData & 0xFF);
}

bool test_spiInternalReadWriteBufPolled(test_thejas32_spi_reg_t *spi, const uint8_t *txData, uint8_t *rxData, int len)
{
    if (!spi) {
        return false;
    }

    for (int i = 0; i < len; i++) {
        uint8_t bOut = txData ? txData[i] : 0xFF;
        uint8_t bIn = test_thejas32_spi_transfer_byte(spi, bOut);
        if (rxData) {
            rxData[i] = bIn;
        }
    }
    return true;
}

void test_spiSequenceStartMock(test_thejas32_spi_reg_t *spi, uint16_t speed, bool leadingEdge)
{
    if (spi) {
        spi->Baudrate = (speed << 4);

        if (leadingEdge) {
            spi->Control = 0x0000; // Mode 0 (CPOL=0, CPHA=0)
        } else {
            spi->Control = 0x0030; // Mode 3 (CPOL=1, CPHA=1)
        }
    }
}
