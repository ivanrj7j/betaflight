/*
 * This file is part of Betaflight.
 * Minimal driver stubs for C-DAC ARIES v3.0 dummy build (Phase 2).
 */

#include "platform.h"
#include "common/utils.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/dma.h"
#include "drivers/bus_spi.h"
#include "drivers/bus_spi_impl.h"
#include "drivers/timer.h"
#include "drivers/exti.h"
#include "drivers/pwm_output.h"
#include "drivers/dshot.h"
#include "drivers/servo_impl.h"
#include "drivers/adc.h"
#include "drivers/serial.h"
#include "io/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"
#include "drivers/persistent.h"
#include "config/config_streamer_impl.h"
#include "sensors/esc_sensor.h"

// ----------------------------------------------------------------------------
// Atomic Fallback Implementation (Single-core RV32IM without 'A' extension)
// ----------------------------------------------------------------------------
unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder)
{
    UNUSED(memorder);
    __disable_irq();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old + val;
    __enable_irq();
    return old;
}

// ----------------------------------------------------------------------------
// IO / GPIO Driver Stubs
// ----------------------------------------------------------------------------
void IOInitGlobal(void)
{
}

IO_t IOGetByTag(ioTag_t tag)
{
    UNUSED(tag);
    return NULL;
}

void IOConfigGPIO(IO_t io, ioConfig_t cfg)
{
    UNUSED(io);
    UNUSED(cfg);
}

void IOHi(IO_t io)
{
    UNUSED(io);
}

void IOLo(IO_t io)
{
    UNUSED(io);
}

void IOToggle(IO_t io)
{
    UNUSED(io);
}

bool IORead(IO_t io)
{
    UNUSED(io);
    return false;
}

void IOWrite(IO_t io, bool hi)
{
    UNUSED(io);
    UNUSED(hi);
}

int IO_GPIOPortIdx(IO_t io)
{
    UNUSED(io);
    return 0;
}

int IO_GPIOPinIdx(IO_t io)
{
    UNUSED(io);
    return 0;
}

void unusedPinsInit(void)
{
}

void debugInit(void)
{
}

void dbgPinHi(int index)
{
    UNUSED(index);
}

void dbgPinLo(int index)
{
    UNUSED(index);
}

// ----------------------------------------------------------------------------
// SPI Bus Driver Stubs
// ----------------------------------------------------------------------------
void spiPreinit(void)
{
}

uint16_t spiCalculateDivider(uint32_t freq)
{
    UNUSED(freq);
    return 0;
}

void spiPinConfigure(const struct spiPinConfig_s *pConfig)
{
    UNUSED(pConfig);
}

void spiSequenceStart(const extDevice_t *dev)
{
    UNUSED(dev);
}

// ----------------------------------------------------------------------------
// DMA Driver Stubs
// ----------------------------------------------------------------------------
dmaChannelDescriptor_t dmaDescriptors[1];

int dmaGetHandlerCount(void)
{
    return 0;
}

int dmaGetDeviceNumber(dmaIdentifier_e identifier)
{
    UNUSED(identifier);
    return 0;
}

int dmaGetDeviceIndex(dmaIdentifier_e identifier)
{
    UNUSED(identifier);
    return 0;
}

const char *dmaGetDisplayString(void)
{
    return "NONE";
}

// ----------------------------------------------------------------------------
// Hardware Timer Driver Stubs
// ----------------------------------------------------------------------------
void timerInit(void)
{
}

void timerChannelOverflowHandlerInit(timerOvrHandlerRec_t *self, timerOvrHandlerCallback *fn)
{
    UNUSED(self);
    UNUSED(fn);
}

void timerConfigUpdateCallback(const timerHardware_t *timHw, timerOvrHandlerRec_t *updateCallback)
{
    UNUSED(timHw);
    UNUSED(updateCallback);
}

void timerReconfigureTimeBase(const timerHardware_t *timHw, uint16_t period, uint32_t hz)
{
    UNUSED(timHw);
    UNUSED(period);
    UNUSED(hz);
}

void timerSetPeriod(const timerHardware_t *timHw, uint32_t period)
{
    UNUSED(timHw);
    UNUSED(period);
}

void timerSetCounter(const timerHardware_t *timHw, uint32_t counter)
{
    UNUSED(timHw);
    UNUSED(counter);
}

void timerDisable(const timerHardware_t *timHw)
{
    UNUSED(timHw);
}

void timerEnable(const timerHardware_t *timHw)
{
    UNUSED(timHw);
}

void timerEnableInterrupt(const timerHardware_t *timHw)
{
    UNUSED(timHw);
}

// ----------------------------------------------------------------------------
// EXTI (External Interrupt) Driver Stubs
// ----------------------------------------------------------------------------
void EXTIInit(void)
{
}

void EXTIHandlerInit(extiCallbackRec_t *cb, extiHandlerCallback *fn)
{
    UNUSED(cb);
    UNUSED(fn);
}

void EXTIConfig(IO_t io, extiCallbackRec_t *cb, int irqPriority, ioConfig_t config, extiTrigger_t trigger)
{
    UNUSED(io);
    UNUSED(cb);
    UNUSED(irqPriority);
    UNUSED(config);
    UNUSED(trigger);
}

void EXTIRelease(IO_t io)
{
    UNUSED(io);
}

void EXTIEnable(IO_t io)
{
    UNUSED(io);
}

void EXTIDisable(IO_t io)
{
    UNUSED(io);
}

// ----------------------------------------------------------------------------
// Configuration Streamer / EEPROM Stubs
// ----------------------------------------------------------------------------
void configUnlock(void)
{
}

void configLock(void)
{
}

void configClearFlags(void)
{
}

configStreamerResult_e configWriteWord(uintptr_t address, config_streamer_buffer_type_t *buffer)
{
    UNUSED(address);
    UNUSED(buffer);
    return CONFIG_RESULT_SUCCESS;
}

// ----------------------------------------------------------------------------
// Persistent Object Stubs
// ----------------------------------------------------------------------------
static uint32_t persistentObjects[16];

void persistentObjectInit(void)
{
}

uint32_t persistentObjectRead(persistentObjectId_e id)
{
    return ((size_t)id < ARRAYLEN(persistentObjects)) ? persistentObjects[id] : 0;
}

void persistentObjectWrite(persistentObjectId_e id, uint32_t value)
{
    if ((size_t)id < ARRAYLEN(persistentObjects)) {
        persistentObjects[id] = value;
    }
}

// ----------------------------------------------------------------------------
// Serial UART Stubs (Phase 2 dummy)
// ----------------------------------------------------------------------------
uartPort_t *serialUART(uartDevice_t *uart, uint32_t baudRate, portMode_e mode, portOptions_e options)
{
    UNUSED(uart);
    UNUSED(baudRate);
    UNUSED(mode);
    UNUSED(options);
    return NULL;
}

void uartReconfigure(uartPort_t *uartPort)
{
    UNUSED(uartPort);
}

// ----------------------------------------------------------------------------
// Motor, Servo, DShot & Sensor Stubs
// ----------------------------------------------------------------------------
bool useDshotTelemetry = false;

bool dshotPwmDevInit(motorDevice_t *device, const motorDevConfig_t *motorConfig)
{
    UNUSED(device);
    UNUSED(motorConfig);
    return false;
}

bool motorPwmDevInit(motorDevice_t *device, const motorDevConfig_t *motorDevConfig, uint16_t idlePulse)
{
    UNUSED(device);
    UNUSED(motorDevConfig);
    UNUSED(idlePulse);
    return false;
}

void servoDevInit(const servoDevConfig_t *servoDevConfig)
{
    UNUSED(servoDevConfig);
}

void servoWrite(uint8_t index, float value)
{
    UNUSED(index);
    UNUSED(value);
}

float erpmToRpm(uint32_t erpm)
{
    UNUSED(erpm);
    return 0.0f;
}

float getDshotRpmAverage(void)
{
    return 0.0f;
}

escSensorData_t *getEscSensorData(uint8_t motorNumber)
{
    UNUSED(motorNumber);
    return NULL;
}

uint16_t adcGetValue(adcSource_e source)
{
    UNUSED(source);
    return 0;
}

void expressLrsTimerEnableIRQs(const struct timerHardware_s *timer)
{
    UNUSED(timer);
}

void uartPinConfigure(const serialPinConfig_t *pSerialPinConfig)
{
    UNUSED(pSerialPinConfig);
}
