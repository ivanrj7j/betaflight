/*
 * This file is part of Betaflight.
 *
 * Polled SPI Master Driver for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 * Header J1 SPI0 Interface Strictly Clamped to 12.5 MHz (Silicon Ground Truth 7).
 *
 * Citations & Evaluations:
 *   - knowledgebase/components/07_BUS_SPI_AND_I2C.md
 *   - knowledgebase/codebase_evaluations/09_EVAL_BUS_SPI_ARIES_C.md
 *   - logs/hardware_inspection/10_STM32_BUS_SPI_HW.md
 *   - logs/hardware_inspection/21_ESP32_BUS_SPI_I2C.md
 *   - lib/vega-sdk/vega-sdk/bsp/include/spi.h & drivers/spi/spi.c
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_SPI

#include "build/atomic.h"
#include "drivers/bus.h"
#include "drivers/bus_spi.h"
#include "drivers/bus_spi_impl.h"
#include "drivers/io.h"
#include "pg/bus_spi.h"

// Hardware Register Status Bitmasks (from C-DAC VEGA SDK spi.h)
#define THEJAS32_SPI_STAT_BUSY        (1 << 4)
#define THEJAS32_SPI_STAT_OVERRUN     (1 << 5)
#define THEJAS32_SPI_STAT_RX_COMPLETE (1 << 6)
#define THEJAS32_SPI_STAT_TX_EMPTY    (1 << 7)

// Control Register Bitmasks
#define THEJAS32_SPI_CTRL_MODE_SHIFT  4
#define THEJAS32_SPI_CTRL_MODE_0      0
#define THEJAS32_SPI_CTRL_MODE_3      3

// Baudrate Prescalers (Ground Truth 7: Strictly Clamped to Divisor 1 = 12.5 MHz)
#define THEJAS32_SPI_BAUD_DIV_4       0 // 25.0 MHz (FORBIDDEN: Overclocks ICM-42688P by 4.1%)
#define THEJAS32_SPI_BAUD_DIV_8       1 // 12.5 MHz (Safe Maximum Ceiling)
#define THEJAS32_SPI_BAUD_DIV_16      2 // 6.25 MHz
#define THEJAS32_SPI_BAUD_DIV_32      3 // 3.125 MHz (Slow Safe Boot / JEDEC ID)
#define THEJAS32_SPI_BAUD_DIV_64      4 // 1.5625 MHz

#define THEJAS32_SPI_TIMEOUT_CYCLES   50000

const spiHardware_t spiHardware[SPIDEV_COUNT] = {
    {
        .device = SPIDEV_1,
        .reg = (spiResource_t *)THEJAS32_SPI_0_1_BASE,
        .sckPins = { { DEFIO_TAG_E(PA5) } },
        .misoPins = { { DEFIO_TAG_E(PA6) } },
        .mosiPins = { { DEFIO_TAG_E(PA7) } },
    }
};

static inline thejas32_spi_reg_t *thejas32_spi_get_hw(const spiResource_t *instance)
{
    if (instance == NULL) {
        return (thejas32_spi_reg_t *)THEJAS32_SPI_0_1_BASE;
    }
    return (thejas32_spi_reg_t *)instance;
}

static void thejas32_spi_reset_controller(thejas32_spi_reg_t *hw)
{
    // C-DAC VEGA SDK Reset & Drain Protocol
    volatile uint32_t dummy = hw->RxData;
    (void)dummy;

    // Toggle SPI controller control word to clear any stuck internal state
    uint32_t current_ctrl = hw->Control;
    hw->Control = 0;
    __asm__ volatile ("fence" : : : "memory");
    hw->Control = current_ctrl;
    __asm__ volatile ("fence" : : : "memory");

    dummy = hw->RxData;
    (void)dummy;

    spiDevice[SPIDEV_1].errorCount++;
}

void spiPinConfigure(const struct spiPinConfig_s *pConfig)
{
    for (size_t hwindex = 0; hwindex < ARRAYLEN(spiHardware); hwindex++) {
        const spiHardware_t *hw = &spiHardware[hwindex];
        const spiDevice_e device = hw->device;
        spiDevice_t *pDev = &spiDevice[device];

        pDev->sck = hw->sckPins[0].pin;
        pDev->miso = hw->misoPins[0].pin;
        pDev->mosi = hw->mosiPins[0].pin;

        if (pConfig) {
            if (pConfig[device].ioTagSck) {
                pDev->sck = pConfig[device].ioTagSck;
            }
            if (pConfig[device].ioTagMiso) {
                pDev->miso = pConfig[device].ioTagMiso;
            }
            if (pConfig[device].ioTagMosi) {
                pDev->mosi = pConfig[device].ioTagMosi;
            }
        }

        pDev->dev = hw->reg;
        pDev->leadingEdge = false;
    }
}

void spiInitDevice(spiDevice_e device)
{
    if (device >= SPIDEV_COUNT) {
        return;
    }

    spiDevice_t *spi = &spiDevice[device];
    thejas32_spi_reg_t *hw = thejas32_spi_get_hw(spi->dev);

    // Initialize IO lines on Header J1
    if (spi->sck) {
        IOInit(IOGetByTag(spi->sck), OWNER_SPI_SCK, RESOURCE_INDEX(device));
        IOConfigGPIO(IOGetByTag(spi->sck), IOCFG_AF_PP);
    }
    if (spi->mosi) {
        IOInit(IOGetByTag(spi->mosi), OWNER_SPI_SDO, RESOURCE_INDEX(device));
        IOConfigGPIO(IOGetByTag(spi->mosi), IOCFG_AF_PP);
    }
    if (spi->miso) {
        IOInit(IOGetByTag(spi->miso), OWNER_SPI_SDI, RESOURCE_INDEX(device));
        IOConfigGPIO(IOGetByTag(spi->miso), IOCFG_IN_FLOATING);
    }

    // Default configuration: 8-bit, MSB first, polling mode (interrupts disabled)
    // C-DAC VEGA SDK: Baudrate = (bBaudCFD << 4)
    // Ground Truth 7: Clamped to Divisor 1 = 12.5 MHz
    hw->Baudrate = (THEJAS32_SPI_BAUD_DIV_8 << 4);
    hw->Control = 0; // Mode 0 (CPOL=0, CPHA=0), MSB first, fixed periph 0

    __asm__ volatile ("fence" : : : "memory");

    // Dummy read to clear RX pipeline
    volatile uint32_t dummy = hw->RxData;
    (void)dummy;
}

uint16_t spiCalculateDivider(uint32_t freq)
{
    /*
     * Inviolable Silicon Ground Truth 7:
     * Divisor 0 (25.0 MHz) overclocks ICM-42688P (24.0 MHz max rating) -> STRICTLY PROHIBITED.
     * Divisor 1 (12.5 MHz) is the safe maximum hardware ceiling.
     */
    if (freq >= 12500000) {
        return THEJAS32_SPI_BAUD_DIV_8; // Clamped to 12.5 MHz
    } else if (freq >= 6250000) {
        return THEJAS32_SPI_BAUD_DIV_16; // 6.25 MHz
    } else {
        return THEJAS32_SPI_BAUD_DIV_32; // 3.125 MHz (Safe boot / JEDEC ID)
    }
}

uint32_t spiCalculateClock(uint16_t spiClkDivisor)
{
    switch (spiClkDivisor) {
        case THEJAS32_SPI_BAUD_DIV_8:   return 12500000;
        case THEJAS32_SPI_BAUD_DIV_16:  return 6250000;
        case THEJAS32_SPI_BAUD_DIV_32:  return 3125000;
        case THEJAS32_SPI_BAUD_DIV_64:  return 1562500;
        default:                        return 12500000;
    }
}

static uint8_t thejas32_spi_transfer_byte(thejas32_spi_reg_t *hw, uint8_t out)
{
    uint32_t timeout = THEJAS32_SPI_TIMEOUT_CYCLES;

    // 1. Wait until controller is not busy and TX hold register is empty
    while (((hw->Status & THEJAS32_SPI_STAT_BUSY) ||
           !(hw->Status & THEJAS32_SPI_STAT_TX_EMPTY)) && --timeout);
    if (!timeout) {
        thejas32_spi_reset_controller(hw);
        return 0xFF;
    }

    hw->TxData = out;
    __asm__ volatile ("fence" : : : "memory");

    // 2. Wait until RX complete flag asserts
    timeout = THEJAS32_SPI_TIMEOUT_CYCLES;
    while (!(hw->Status & THEJAS32_SPI_STAT_RX_COMPLETE) && --timeout);
    if (!timeout) {
        thejas32_spi_reset_controller(hw);
        return 0xFF;
    }

    uint8_t in = (uint8_t)(hw->RxData & 0xFF);
    return in;
}

bool spiInternalReadWriteBufPolled(spiResource_t *instance, const uint8_t *txData, uint8_t *rxData, int len)
{
    if (!instance || len <= 0) {
        return false;
    }
    if (!txData && !rxData) {
        return false;
    }

    thejas32_spi_reg_t *hw = thejas32_spi_get_hw(instance);
    uint32_t start_errors = spiDevice[SPIDEV_1].errorCount;

    for (int i = 0; i < len; i++) {
        uint8_t bOut = txData ? txData[i] : 0xFF; // Send 0xFF dummy byte when reading
        uint8_t bIn = thejas32_spi_transfer_byte(hw, bOut);

        if (spiDevice[SPIDEV_1].errorCount > start_errors) {
            // Hardware timeout / error occurred during transfer
            return false;
        }

        if (rxData) {
            rxData[i] = bIn;
        }
    }

    return true;
}

void spiSequenceStart(const extDevice_t *dev)
{
    busDevice_t *bus = dev->bus;
    thejas32_spi_reg_t *hw = thejas32_spi_get_hw(bus->busType_u.spi.instance);

    // Switch bus speed if requested and different from current
    if (dev->busType_u.spi.speed != bus->busType_u.spi.speed) {
        uint16_t divider = dev->busType_u.spi.speed;
        if (divider < THEJAS32_SPI_BAUD_DIV_8) {
            divider = THEJAS32_SPI_BAUD_DIV_8; // Strictly clamp to 12.5 MHz
        }
        hw->Baudrate = ((uint32_t)divider << 4);
        __asm__ volatile ("fence" : : : "memory");
        bus->busType_u.spi.speed = dev->busType_u.spi.speed;
    }

    // Switch clock phase/polarity if requested
    if (dev->busType_u.spi.leadingEdge != bus->busType_u.spi.leadingEdge) {
        uint32_t ctrl = hw->Control & ~(3U << THEJAS32_SPI_CTRL_MODE_SHIFT);
        if (dev->busType_u.spi.leadingEdge) {
            ctrl |= (THEJAS32_SPI_CTRL_MODE_0 << THEJAS32_SPI_CTRL_MODE_SHIFT);
        } else {
            ctrl |= (THEJAS32_SPI_CTRL_MODE_3 << THEJAS32_SPI_CTRL_MODE_SHIFT);
        }
        hw->Control = ctrl;
        __asm__ volatile ("fence" : : : "memory");
        bus->busType_u.spi.leadingEdge = dev->busType_u.spi.leadingEdge;
    }

    // Process all bus segments synchronously via polled engine (Zero DMA complexity)
    spiProcessSegmentsPolled(dev);
}

// Dummy DMA stubs to satisfy any unused declarations
void spiInternalInitStream(const extDevice_t *dev, volatile busSegment_t *segment) { UNUSED(dev); UNUSED(segment); }
void spiInternalStartDMA(const extDevice_t *dev) { UNUSED(dev); }
void spiInternalStopDMA(const extDevice_t *dev) { UNUSED(dev); }
void spiInternalResetStream(dmaChannelDescriptor_t *descriptor) { UNUSED(descriptor); }
void spiInternalResetDescriptors(busDevice_t *bus) { UNUSED(bus); }

#endif // USE_SPI
