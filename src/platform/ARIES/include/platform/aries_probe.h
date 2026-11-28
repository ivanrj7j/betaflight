/*
 * This file is part of Betaflight.
 *
 * [HARDWARE-DEBUG-PROBE: DBG-01] (Temporary entire file - delete at Step D5 cleanup)
 * Silicon Diagnostic Probe (Hardware Debug Session 01: UART0/MSP RX Stall)
 * for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ARIES_PROBE_DEBUG) && (ARIES_PROBE_DEBUG > 0) && !defined(UNIT_TEST)

struct serialPort_s;
struct mspPort_s;

void ariesProbeRecordIrq(uint8_t irq, uint32_t intrStatus);
void ariesProbeResetStormGuard(void);

void ariesProbeRecordUartReconfig(uint8_t ch, uint32_t baud, uint8_t mode, uint8_t ie);
void ariesProbeRecordUart0IsrEntry(uint8_t iir, uint8_t lsrBefore);
void ariesProbeRecordUart0IsrDone(uint8_t rxTaken);
void ariesProbeRecordUartPollTick(uint8_t srcTag);
void ariesProbeRecordUartRxByte(uint8_t ch, uint8_t srcTag, uint8_t lsr, uint8_t byteVal, bool dropped);
void ariesProbeRecordUartTxBytes(uint8_t ch, uint8_t count, bool fromIsr);
void ariesProbeRecordUart1Event(uint8_t kind); // 0=isr, 1=noise, 2=suppress, 3=rearm

// [HARDWARE-DEBUG-PROBE: DBG-02]
void ariesProbeRecordTrap(uint32_t mcause, uint32_t mepc, uint32_t mstatusIn, uint32_t mstatusOut);
void ariesProbeRecordCsr(uint8_t siteTag, uint32_t prevMstatus);
// [/HARDWARE-DEBUG-PROBE: DBG-02]
// [HARDWARE-DEBUG-PROBE: DBG-03]
void ariesProbeRunSiliconLab(void);
// [/HARDWARE-DEBUG-PROBE: DBG-03]

void ariesProbeTickMain(void);
void ariesProbeTickSerial(const struct serialPort_s *u0Port, const struct mspPort_s *mspPort);

void ariesProbeRecordMspByte(uint8_t portState, uint8_t pktState, uint8_t rxChar,
                             uint16_t offset, uint16_t dataSize, uint16_t cmd, uint8_t pending);
void ariesProbeRecordMspCmdOk(uint16_t cmd);
void ariesProbeRecordMspCrcErr(uint16_t cmd, uint8_t pktState);
void ariesProbeRecordCliEnter(bool interactive);
void ariesProbeDumpReport(const char *reason, uint32_t arg);

#else

#define ariesProbeRecordIrq(irq, intrStatus)                                ((void)0)
#define ariesProbeResetStormGuard()                                         ((void)0)
#define ariesProbeRecordUartReconfig(ch, baud, mode, ie)                    ((void)0)
#define ariesProbeRecordUart0IsrEntry(iir, lsrBefore)                       ((void)0)
#define ariesProbeRecordUart0IsrDone(rxTaken)                               ((void)0)
#define ariesProbeRecordUartPollTick(srcTag)                                ((void)0)
#define ariesProbeRecordUartRxByte(ch, srcTag, lsr, byteVal, dropped)       ((void)0)
#define ariesProbeRecordUartTxBytes(ch, count, fromIsr)                     ((void)0)
#define ariesProbeRecordUart1Event(kind)                                    ((void)0)
// [HARDWARE-DEBUG-PROBE: DBG-02]
#define ariesProbeRecordTrap(mcause, mepc, mstatusIn, mstatusOut)           ((void)0)
#define ariesProbeRecordCsr(siteTag, prevMstatus)                           ((void)0)
// [/HARDWARE-DEBUG-PROBE: DBG-02]
// [HARDWARE-DEBUG-PROBE: DBG-03]
#define ariesProbeRunSiliconLab()                                           ((void)0)
// [/HARDWARE-DEBUG-PROBE: DBG-03]
#define ariesProbeTickMain()                                                ((void)0)
#define ariesProbeTickSerial(u0Port, mspPort)                               ((void)0)
#define ariesProbeRecordMspByte(ps, pks, c, off, dsz, cmd, pend)            ((void)0)
#define ariesProbeRecordMspCmdOk(cmd)                                       ((void)0)
#define ariesProbeRecordMspCrcErr(cmd, pks)                                 ((void)0)
#define ariesProbeRecordCliEnter(interactive)                               ((void)0)
#define ariesProbeDumpReport(reason, arg)                                   ((void)0)

#endif

#ifdef __cplusplus
}
#endif
