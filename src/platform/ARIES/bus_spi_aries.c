/*
 * This file is part of Betaflight.
 *
 * SPI Driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V SoC).
 * Implements hardware SPI master registers from C-DAC VEGA SDK BSP.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "platform/bsp_thejas32.h"

#ifdef USE_SPI

#include "common/utils.h"
#include "drivers/bus.h"
#include "drivers/bus_spi.h"
#include "drivers/bus_spi_impl.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "pg/bus_spi.h"

#define THEJAS32_SPI_BUSY            (1 << 4)
#define THEJAS32_SPI_OVERRUN         (1 << 5)
#define THEJAS32_SPI_RX_COMPLETE     (1 << 6)
#define THEJAS32_SPI_TX_HOLD_EMPTY   (1 << 7)

const spiHardware_t spiHardware[SPIDEV_COUNT] = {
    {
        .device = SPIDEV_1,
        .reg = (spiResource_t *)THEJAS32_SPI_0_1_BASE,
        .sckPins = { { DEFIO_TAG_E(PA5) } },
        .misoPins = { { DEFIO_TAG_E(PA6) } },
        .mosiPins = { { DEFIO_TAG_E(PA7) } },
    }
};

void spiPreinit(void)
{
    // Configure SPI0 pins on Header J1
    IOConfigGPIO(IOGetByTag(DEFIO_TAG(PA5)), IOCFG_AF_PP);
    IOConfigGPIO(IOGetByTag(DEFIO_TAG(PA6)), IOCFG_IN_FLOATING);
    IOConfigGPIO(IOGetByTag(DEFIO_TAG(PA7)), IOCFG_AF_PP);

    // Initialize SPI1 hardware device
    spiInitDevice(SPIDEV_1);
}

void spiInitDevice(spiDevice_e device)
{
    thejas32_spi_reg_t *spi = (thejas32_spi_reg_t *)spiInstanceByDevice(device);
    if (!spi) {
        return;
    }

    // Default baud divisor: Div by 8 = 12.5 MHz (at 100 MHz SystemCoreClock)
    spi->Baudrate = (1 << 4);
    __asm__ __volatile__("fence");

    // Default control word: 8-bit, Mode 0, MSB first, fixed peripheral
    spi->Control = 0x0000;
    __asm__ __volatile__("fence");
}

uint16_t spiCalculateDivider(uint32_t freq)
{
    // SystemCoreClock is 100 MHz
    // Divisors on THEJAS32 SPI controller:
    // 0: /4  = 25 MHz
    // 1: /8  = 12.5 MHz
    // 2: /16 = 6.25 MHz
    // 3: /32 = 3.125 MHz
    // 4: /64 = 1.56 MHz
    // 5: /128 = 781 kHz
    // 6: /256 = 390 kHz
    // 7: /512 = 195 kHz
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

static uint8_t thejas32_spi_transfer_byte(thejas32_spi_reg_t *spi, uint8_t out)
{
    uint32_t timeout = 100000;
    while ((spi->Status & THEJAS32_SPI_BUSY) && --timeout) {
        __NOP();
    }

    timeout = 100000;
    while (!(spi->Status & THEJAS32_SPI_TX_HOLD_EMPTY) && --timeout) {
        __NOP();
    }

    spi->TxData = out;
    __asm__ __volatile__("fence");

    timeout = 100000;
    while (!(spi->Status & THEJAS32_SPI_RX_COMPLETE) && --timeout) {
        __NOP();
    }

    return (uint8_t)(spi->RxData & 0xFF);
}

bool spiInternalReadWriteBufPolled(spiResource_t *instance, const uint8_t *txData, uint8_t *rxData, int len)
{
    thejas32_spi_reg_t *spi = (thejas32_spi_reg_t *)instance;
    if (!spi) {
        return false;
    }

    for (int i = 0; i < len; i++) {
        uint8_t bOut = txData ? txData[i] : 0xFF;
        uint8_t bIn = thejas32_spi_transfer_byte(spi, bOut);
        if (rxData) {
            rxData[i] = bIn;
        }
    }
    return true;
}

void spiSequenceStart(const extDevice_t *dev)
{
    thejas32_spi_reg_t *spi = (thejas32_spi_reg_t *)dev->bus->busType_u.spi.instance;
    if (spi) {
        // Set clock divisor
        uint16_t speed = dev->busType_u.spi.speed;
        spi->Baudrate = (speed << 4);

        // Betaflight busDevice supports Mode 0 (leadingEdge True) and Mode 3 (leadingEdge False)
        if (dev->busType_u.spi.leadingEdge) {
            spi->Control = 0x0000; // Mode 0 (CPOL=0, CPHA=0)
        } else {
            spi->Control = 0x0030; // Mode 3 (CPOL=1, CPHA=1)
        }
        __asm__ __volatile__("fence");
    }

    // Process all segments using polled transfer
    spiProcessSegmentsPolled(dev);
}

void spiPinConfigure(const struct spiPinConfig_s *pConfig)
{
    for (size_t hwindex = 0; hwindex < ARRAYLEN(spiHardware); hwindex++) {
        const spiHardware_t *hw = &spiHardware[hwindex];
        if (!hw->reg) continue;

        const spiDevice_e device = hw->device;
        spiDevice_t *pDev = &spiDevice[device];

        for (int pindex = 0; pindex < MAX_SPI_PIN_SEL; pindex++) {
            if (pConfig[device].ioTagSck == hw->sckPins[pindex].pin) {
                pDev->sck = hw->sckPins[pindex].pin;
            }
            if (pConfig[device].ioTagMiso == hw->misoPins[pindex].pin) {
                pDev->miso = hw->misoPins[pindex].pin;
            }
            if (pConfig[device].ioTagMosi == hw->mosiPins[pindex].pin) {
                pDev->mosi = hw->mosiPins[pindex].pin;
            }
        }
    }
}

void spiInitBusDMA(void)
{
}

#endif // USE_SPI
