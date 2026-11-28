/*
 * This file is part of Betaflight.
 *
 * [HARDWARE-DEBUG-PROBE: DBG-01] (Temporary entire file - delete at Step D5 cleanup)
 * Silicon Diagnostic Probe (Hardware Debug Session 01: UART0/MSP RX Stall)
 * for C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031).
 * Records ISR/Poll/MSP flight-recorder rings in SRAM and emits ASCII report
 * over polled UART0 strictly outside active MSP replies (on stall / trap / storm).
 */

#include "platform.h"

#if defined(ARIES_PROBE_DEBUG) && (ARIES_PROBE_DEBUG > 0) && !defined(UNIT_TEST)

#include "drivers/io.h"
#include "drivers/light_led.h"
#include "drivers/serial.h"
#include "drivers/time.h"
#include "msp/msp_serial.h"
#include "platform/aries_probe.h"
#include "platform/bsp_thejas32.h"

// [HARDWARE-DEBUG-PROBE: DBG-04 / DBG-05] Referenced from startup.S trap_entry when ARIES_PROBE_DEBUG > 0
volatile uint8_t  probeTrapMode = 0U;
volatile uint32_t probeTrapEnterCnt = 0U;
volatile uint8_t  gProbeFastArmed = 0U;
volatile uint8_t  gProbeInTrap = 0U;
volatile uint8_t  gProbeLastOkSite = 0U;
volatile uint32_t gProbeLastOkCaller = 0U;

#define PROBE_ISR_RING_LEN 24U
#define PROBE_RX_RING_LEN  48U
#define PROBE_MSP_RING_LEN  24U
// [HARDWARE-DEBUG-PROBE: DBG-02]
#define PROBE_TRAP_RING_LEN 16U
// [/HARDWARE-DEBUG-PROBE: DBG-02]

typedef struct {
    uint16_t ms16;
    uint8_t iir;
    uint8_t lsrBefore;
    uint8_t rxTaken;
} probeIsrRec_t;

typedef struct {
    uint16_t ms16;
    uint8_t srcTag; // 'I'=ISR, 'P'=100Hz poll, 'U'=uartPlatformPollRx
    uint8_t lsr;
    uint8_t byteVal;
} probeRxRec_t;

typedef struct {
    uint16_t ms16;
    uint8_t portState;
    uint8_t pktState;
    uint8_t rxChar;
    uint8_t offsetLo;
    uint8_t dataSizeLo;
    uint8_t cmdLo;
    uint8_t pending;
} probeMspRec_t;

// [HARDWARE-DEBUG-PROBE: DBG-02]
typedef struct {
    uint16_t ms16;
    uint32_t mcause;
    uint32_t mepc;
    uint32_t mstatusIn;
    uint32_t mstatusOut;
} probeTrapRec_t;
// [/HARDWARE-DEBUG-PROBE: DBG-02]

static struct {
    volatile uint32_t irqCnt[12];
    volatile uint32_t irqOtherCnt;
    volatile uint32_t taskMainCnt;
    volatile uint32_t taskSerialCnt;

    // Storm guard state
    volatile uint32_t lastSerialSeenInIrq;
    volatile uint32_t nonTimer0IrqStreak;
    volatile uint8_t  stormDumped;

    // UART0 counters
    volatile uint32_t u0IsrCnt;
    volatile uint32_t u0IsrNoDrCnt;
    volatile uint32_t u0IsrNoIrqCnt;
    volatile uint32_t u0IsrRxBytes;
    volatile uint32_t u0PollTicks;
    volatile uint32_t u0PollDrBytes;
    volatile uint32_t u0PlatPollTicks;
    volatile uint32_t u0PlatPollDrBytes;
    volatile uint32_t u0OeCnt;
    volatile uint32_t u0PeCnt;
    volatile uint32_t u0FeCnt;
    volatile uint32_t u0BiCnt;
    volatile uint32_t u0DropCnt;
    volatile uint32_t u0TxPollBytes;
    volatile uint32_t u0TxIsrBytes;

    // Reconfig history
    volatile uint32_t u0ReconfCnt;
    volatile uint32_t u0LastBaud;
    volatile uint8_t  u0LastMode;
    volatile uint8_t  u0LastIe;

    // UART1 counters
    volatile uint32_t u1IsrCnt;
    volatile uint32_t u1NoiseCnt;
    volatile uint32_t u1SuppressCnt;
    volatile uint32_t u1RearmCnt;

    // MSP counters & auto-stall detector
    volatile uint32_t mspCmdOkCnt;
    volatile uint32_t mspCrcErrCnt;
    volatile uint32_t mspRxBytes;
    volatile uint32_t mspCli02Cnt;
    volatile uint32_t mspCliIntCnt;
    volatile uint16_t mspLastCmd;
    volatile uint16_t mspLastCrcCmd;
    volatile uint8_t  mspLastCrcPktState;
    volatile timeMs_t lastRxMs;
    volatile timeMs_t lastReplyMs;
    volatile bool     sawTraffic;
    volatile bool     stallDumped;
    volatile bool     dumpInProgress;

    // Rings
    probeIsrRec_t isrRing[PROBE_ISR_RING_LEN];
    volatile uint32_t isrHead;

    probeRxRec_t  rxRing[PROBE_RX_RING_LEN];
    volatile uint32_t rxHead;

    probeMspRec_t mspRing[PROBE_MSP_RING_LEN];
    volatile uint32_t mspHead;

    // [HARDWARE-DEBUG-PROBE: DBG-02] Trap & CSR MIE loss forensic telemetry
    probeTrapRec_t trapRing[PROBE_TRAP_RING_LEN];
    volatile uint32_t trapHead;
    probeTrapRec_t badTrap;
    volatile uint32_t badMpieTrapCnt;
    volatile uint32_t badTrapIdx;
    volatile uint32_t badTrapCsrPre;
    volatile uint32_t lastCsrPre;
    volatile uint32_t lastCsrPost;
    volatile uint16_t lastCsrMs16;
    volatile uint8_t  lastCsrSite;
    volatile uint8_t  badTrapCsrSite;
    volatile bool     badTrapCaptured;
    volatile bool     mieArmed;
    volatile bool     mieLostDumped;
    // [HARDWARE-DEBUG-PROBE: DBG-05] Raw hardware CSR snapshot + timewarp telemetry
    volatile uint32_t trigCaller;
    volatile uint32_t trigHwMepc;
    volatile uint32_t trigHwMcause;
    volatile uint32_t trigHwMtval;
    volatile uint32_t trigHwStat;
    volatile uint32_t trigHwRaw;
    volatile uint32_t warpUsCnt;
    volatile uint32_t warpUsPrev;
    volatile uint32_t warpUsCur;
    volatile uint32_t warpMsCnt;
    volatile uint32_t warpMsPrev;
    volatile uint32_t warpMsCur;
    volatile uint8_t  trigSite;
    // [/HARDWARE-DEBUG-PROBE: DBG-02]

    const serialPort_t *lastU0Port;
    const mspPort_t *lastMspPort;
} gProbe;

// [HARDWARE-DEBUG-PROBE: DBG-02 / DBG-05]
static inline uint32_t probeReadMstatus(void)
{
    uint32_t val = 0U;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(val));
    return val;
}

static inline uint32_t probeReadMepc(void)
{
    uint32_t val = 0U;
    __asm__ volatile ("csrr %0, mepc" : "=r"(val));
    return val;
}

static inline uint32_t probeReadMcause(void)
{
    uint32_t val = 0U;
    __asm__ volatile ("csrr %0, mcause" : "=r"(val));
    return val;
}

static inline uint32_t probeReadMtval(void)
{
    uint32_t val = 0U;
    __asm__ volatile ("csrr %0, mtval" : "=r"(val));
    return val;
}

static inline void probeMarkRxTraffic(timeMs_t now)
{
    if (!gProbe.sawTraffic) {
        gProbe.sawTraffic = true;
        gProbe.lastReplyMs = now;
    }
    gProbe.lastRxMs = now;
}
// [/HARDWARE-DEBUG-PROBE: DBG-02]

static void probePrintDec(uint32_t val)
{
    char buf[12];
    int i = 0;
    if (val == 0U) {
        thejas32_early_uart_putc('0');
        return;
    }
    while (val > 0U && i < 11) {
        buf[i++] = (char)('0' + (val % 10U));
        val /= 10U;
    }
    while (i > 0) {
        thejas32_early_uart_putc(buf[--i]);
    }
}

static void probePrintHex8(uint8_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    thejas32_early_uart_putc(hex[(val >> 4) & 0x0FU]);
    thejas32_early_uart_putc(hex[val & 0x0FU]);
}

void ariesProbeRecordIrq(uint8_t irq, uint32_t intrStatus)
{
    if (irq < 12U) {
        gProbe.irqCnt[irq]++;
    } else {
        gProbe.irqOtherCnt++;
    }

    if (irq == THEJAS32_IRQ_TIMER0) {
        return;
    }

    if (gProbe.lastSerialSeenInIrq == gProbe.taskSerialCnt) {
        if (++gProbe.nonTimer0IrqStreak == 25000U && !gProbe.stormDumped) {
            gProbe.stormDumped = 1U;
            // Break storm on offending non-Timer0 bits before dumping so UART0 isn't re-entered
            uint32_t badMask = intrStatus & ~(1UL << THEJAS32_IRQ_TIMER0);
            THEJAS32_INTR_CTRL.INTR_EN &= ~badMask;
            if (badMask & (1UL << THEJAS32_IRQ_UART0)) {
                THEJAS32_UART(0).UART_IE = 0x00U;
            }
            if (badMask & (1UL << THEJAS32_IRQ_UART1)) {
                THEJAS32_UART(1).UART_IE = 0x00U;
            }
            THEJAS32_MEMORY_FENCE();
            ariesProbeDumpReport("IRQ_STORM", intrStatus);
        }
    } else {
        gProbe.lastSerialSeenInIrq = gProbe.taskSerialCnt;
        gProbe.nonTimer0IrqStreak = 0U;
    }
}

void ariesProbeResetStormGuard(void)
{
    gProbe.nonTimer0IrqStreak = 0U;
    gProbe.lastSerialSeenInIrq = gProbe.taskSerialCnt;
}

void ariesProbeRecordUartReconfig(uint8_t ch, uint32_t baud, uint8_t mode, uint8_t ie)
{
    if (ch == 0U) {
        gProbe.u0ReconfCnt++;
        gProbe.u0LastBaud = baud;
        gProbe.u0LastMode = mode;
        gProbe.u0LastIe = ie;
    }
}

void ariesProbeRecordUart0IsrEntry(uint8_t iir, uint8_t lsrBefore)
{
    gProbe.u0IsrCnt++;
    if (iir & 0x01U) {
        gProbe.u0IsrNoIrqCnt++;
    }
    if ((lsrBefore & 0x01U) == 0U) {
        gProbe.u0IsrNoDrCnt++;
    }
    if (gProbe.taskSerialCnt > 0U) {
        probeMarkRxTraffic(millis()); // [HARDWARE-DEBUG-PROBE: DBG-02]
    }
    uint32_t slot = gProbe.isrHead % PROBE_ISR_RING_LEN;
    gProbe.isrRing[slot].ms16 = (uint16_t)(millis() & 0xFFFFU);
    gProbe.isrRing[slot].iir = iir;
    gProbe.isrRing[slot].lsrBefore = lsrBefore;
    gProbe.isrRing[slot].rxTaken = 0U;
}

void ariesProbeRecordUart0IsrDone(uint8_t rxTaken)
{
    uint32_t slot = gProbe.isrHead % PROBE_ISR_RING_LEN;
    gProbe.isrRing[slot].rxTaken = rxTaken;
    gProbe.isrHead++;
}

void ariesProbeRecordUartPollTick(uint8_t srcTag)
{
    if (srcTag == 'P') {
        gProbe.u0PollTicks++;
    } else if (srcTag == 'U') {
        gProbe.u0PlatPollTicks++;
    }
}

void ariesProbeRecordUartRxByte(uint8_t ch, uint8_t srcTag, uint8_t lsr, uint8_t byteVal, bool dropped)
{
    if (ch != 0U) {
        return;
    }
    if (gProbe.taskSerialCnt > 0U) {
        probeMarkRxTraffic(millis()); // [HARDWARE-DEBUG-PROBE: DBG-02]
    }
    if (srcTag == 'I') {
        gProbe.u0IsrRxBytes++;
    } else if (srcTag == 'P') {
        gProbe.u0PollDrBytes++;
    } else if (srcTag == 'U') {
        gProbe.u0PlatPollDrBytes++;
    }
    if (lsr & 0x02U) gProbe.u0OeCnt++;
    if (lsr & 0x04U) gProbe.u0PeCnt++;
    if (lsr & 0x08U) gProbe.u0FeCnt++;
    if (lsr & 0x10U) gProbe.u0BiCnt++;
    if (dropped) gProbe.u0DropCnt++;

    uint32_t slot = gProbe.rxHead++ % PROBE_RX_RING_LEN;
    gProbe.rxRing[slot].ms16 = (uint16_t)(millis() & 0xFFFFU);
    gProbe.rxRing[slot].srcTag = srcTag;
    gProbe.rxRing[slot].lsr = lsr;
    gProbe.rxRing[slot].byteVal = byteVal;
}

void ariesProbeRecordUartTxBytes(uint8_t ch, uint8_t count, bool fromIsr)
{
    if (ch != 0U) {
        return;
    }
    if (fromIsr) {
        gProbe.u0TxIsrBytes += count;
    } else {
        gProbe.u0TxPollBytes += count;
    }
}

void ariesProbeRecordUart1Event(uint8_t kind)
{
    switch (kind) {
    case 0: gProbe.u1IsrCnt++; break;
    case 1: gProbe.u1NoiseCnt++; break;
    case 2: gProbe.u1SuppressCnt++; break;
    case 3: gProbe.u1RearmCnt++; break;
    default: break;
    }
}

void ariesProbeRecordMspByte(uint8_t portState, uint8_t pktState, uint8_t rxChar,
                             uint16_t offset, uint16_t dataSize, uint16_t cmd, uint8_t pending)
{
    gProbe.mspRxBytes++;
    probeMarkRxTraffic(millis()); // [HARDWARE-DEBUG-PROBE: DBG-02]

    uint32_t slot = gProbe.mspHead++ % PROBE_MSP_RING_LEN;
    gProbe.mspRing[slot].ms16 = (uint16_t)(gProbe.lastRxMs & 0xFFFFU);
    gProbe.mspRing[slot].portState = portState;
    gProbe.mspRing[slot].pktState = pktState;
    gProbe.mspRing[slot].rxChar = rxChar;
    gProbe.mspRing[slot].offsetLo = (uint8_t)(offset & 0xFFU);
    gProbe.mspRing[slot].dataSizeLo = (uint8_t)(dataSize & 0xFFU);
    gProbe.mspRing[slot].cmdLo = (uint8_t)(cmd & 0xFFU);
    gProbe.mspRing[slot].pending = pending;
}

void ariesProbeRecordMspCmdOk(uint16_t cmd)
{
    gProbe.mspCmdOkCnt++;
    gProbe.mspLastCmd = cmd;
    gProbe.sawTraffic = true;
    gProbe.lastReplyMs = millis();
    gProbe.stallDumped = false; // Arm auto-dump again if a later request stalls
}

void ariesProbeRecordMspCrcErr(uint16_t cmd, uint8_t pktState)
{
    gProbe.mspCrcErrCnt++;
    gProbe.mspLastCrcCmd = cmd;
    gProbe.mspLastCrcPktState = pktState;
}

void ariesProbeRecordCliEnter(bool interactive)
{
    probeMarkRxTraffic(millis()); // [HARDWARE-DEBUG-PROBE: DBG-02]
    if (interactive) {
        gProbe.mspCliIntCnt++;
    } else {
        gProbe.mspCli02Cnt++;
        gProbe.stallDumped = false;
    }
}

// [HARDWARE-DEBUG-PROBE: DBG-02]
void ariesProbeRecordTrap(uint32_t mcause, uint32_t mepc, uint32_t mstatusIn, uint32_t mstatusOut)
{
    uint32_t idx = gProbe.trapHead;
    uint32_t slot = idx % PROBE_TRAP_RING_LEN;
    probeTrapRec_t rec;
    rec.ms16 = (uint16_t)(millis() & 0xFFFFU);
    rec.mcause = mcause;
    rec.mepc = mepc;
    rec.mstatusIn = mstatusIn;
    rec.mstatusOut = mstatusOut;
    gProbe.trapRing[slot] = rec;
    gProbe.trapHead = idx + 1U;

    if ((mcause & 0x80000000UL) != 0U && (mstatusIn & 0x80U) == 0U) {
        gProbe.badMpieTrapCnt++;
        if (!gProbe.badTrapCaptured) {
            gProbe.badTrap = rec;
            gProbe.badTrapIdx = idx;
            gProbe.badTrapCsrSite = gProbe.lastCsrSite;
            gProbe.badTrapCsrPre = gProbe.lastCsrPre;
            gProbe.badTrapCaptured = true;
        }
    }
}

void ariesProbeRecordTimeWarp(uint8_t isMs, uint32_t prevVal, uint32_t curVal)
{
    if (isMs) {
        if (gProbe.warpMsCnt == 0U) {
            gProbe.warpMsPrev = prevVal;
            gProbe.warpMsCur = curVal;
        }
        gProbe.warpMsCnt++;
    } else {
        if (gProbe.warpUsCnt == 0U) {
            gProbe.warpUsPrev = prevVal;
            gProbe.warpUsCur = curVal;
        }
        gProbe.warpUsCnt++;
    }
}

void ariesProbeOnMieLost(uint8_t siteTag, const void *caller, uint32_t mstatNow)
{
    (void)mstatNow;
    if (gProbe.dumpInProgress || gProbe.mieLostDumped || !gProbe.mieArmed) {
        return;
    }
    gProbeFastArmed = 0U;
    gProbe.mieLostDumped = true;
    gProbe.trigSite     = siteTag;
    gProbe.trigCaller   = (uint32_t)(uintptr_t)caller;
    gProbe.trigHwMepc   = probeReadMepc();
    gProbe.trigHwMcause = probeReadMcause();
    gProbe.trigHwMtval  = probeReadMtval();
    gProbe.trigHwStat   = THEJAS32_INTR_CTRL.INTR_STATUS;
    gProbe.trigHwRaw    = THEJAS32_INTR_CTRL.RAW_INTR;
    ariesProbeDumpReport("MIE_LOST", ((uint32_t)siteTag << 24) | (gProbe.trapHead & 0xFFFFFFU));
}

static void probeCheckMieCommon(uint8_t siteTag, uint32_t mstatNow, const void *caller)
{
    if ((mstatNow & 8U) != 0U) {
        gProbe.mieArmed = true;
        gProbeFastArmed = 1U;
        return;
    }
    if (gProbe.mieArmed && !gProbe.mieLostDumped) {
        ariesProbeOnMieLost(siteTag, caller, mstatNow);
    }
}

void ariesProbeRecordCsr(uint8_t siteTag, uint32_t prevMstatus)
{
    uint32_t postMstatus = probeReadMstatus();
    gProbe.lastCsrSite = siteTag;
    gProbe.lastCsrPre = prevMstatus;
    gProbe.lastCsrPost = postMstatus;

    if ((prevMstatus & 8U) != 0U && (postMstatus & 8U) == 0U && !gProbe.mieLostDumped) {
        gProbeFastArmed = 0U;
        gProbe.mieLostDumped = true;
        gProbe.trigSite     = siteTag;
        gProbe.trigCaller   = (uint32_t)(uintptr_t)__builtin_return_address(0);
        gProbe.trigHwMepc   = probeReadMepc();
        gProbe.trigHwMcause = probeReadMcause();
        gProbe.trigHwMtval  = probeReadMtval();
        gProbe.trigHwStat   = THEJAS32_INTR_CTRL.INTR_STATUS;
        gProbe.trigHwRaw    = THEJAS32_INTR_CTRL.RAW_INTR;
        ariesProbeDumpReport("CSR_MIE_DROP", ((uint32_t)siteTag << 24) | (gProbe.trapHead & 0xFFFFFFU));
        return;
    }

    // Nested atomic sections (prevMstatus.MIE == 0, e.g. taskHandleSerial -> uartIrqHandler -> uartEnableTxInterrupt)
    // intentionally run millis() while outer MIE=0; suppress MIE_LOST alarm until the outer atomic exit.
    uint8_t savedFast = gProbeFastArmed;
    if ((prevMstatus & 8U) == 0U) {
        gProbeFastArmed = 0U;
    }
    gProbe.lastCsrMs16 = (uint16_t)(millis() & 0xFFFFU);
    if ((prevMstatus & 8U) == 0U) {
        gProbeFastArmed = savedFast;
    }
}
// [/HARDWARE-DEBUG-PROBE: DBG-02]

void ariesProbeTickMain(void)
{
    gProbe.taskMainCnt++;
    probeCheckMieCommon('M', probeReadMstatus(), __builtin_return_address(0)); // [HARDWARE-DEBUG-PROBE: DBG-02]
}

void ariesProbeTickSerial(const serialPort_t *u0Port, const mspPort_t *mspPort)
{
    gProbe.taskSerialCnt++;
    ariesProbeResetStormGuard();
    gProbe.lastU0Port = u0Port;
    gProbe.lastMspPort = mspPort;

    // 2.5 Hz physical probe heartbeat on LED0 (PA22) so silicon liveness is visible even if UART dies
    if ((gProbe.taskSerialCnt % 20U) == 0U) {
        LED0_TOGGLE;
    }

    probeCheckMieCommon('S', probeReadMstatus(), __builtin_return_address(0)); // [HARDWARE-DEBUG-PROBE: DBG-02]

    if (!gProbe.sawTraffic || gProbe.stallDumped) {
        return;
    }

    // Do not dump while interactive CLI is intentionally open unless 0x02 non-interactive hijack occurred
    if (mspPort && mspPort->portState == PORT_CLI_ACTIVE && gProbe.mspCli02Cnt == 0U) {
        return;
    }

    const timeMs_t now = millis();
    const int32_t sinceRx = cmp32(now, gProbe.lastRxMs);
    const int32_t sinceReply = cmp32(now, gProbe.lastReplyMs);
    const int32_t rxAfterReply = cmp32(gProbe.lastRxMs, gProbe.lastReplyMs);

    // Dump cleanly when:
    //  (A) An inbound RX arrived after last valid MSP reply, stalled >= 2200 ms without reply,
    //      and wire has been quiet >= 1200 ms (Configurator stopped retrying or disconnected), OR
    //  (B) Continuous retries kept arriving with no valid reply for >= 8500 ms, OR
    //  (C) Non-interactive 0x02 CLI trap fired >= 1200 ms ago without valid MSP reply.
    const bool stalledWithQuiet = (rxAfterReply > 0) && (sinceReply >= 2200) && (sinceRx >= 1200);
    const bool stalledHard = (rxAfterReply > 0) && (sinceReply >= 8500);
    const bool cli02Trap = (gProbe.mspCli02Cnt > 0U) && (sinceReply >= 1200) && (sinceRx >= 800);

    if (stalledWithQuiet || stalledHard || cli02Trap) {
        gProbe.stallDumped = true;
        ariesProbeDumpReport(cli02Trap ? "CLI_0x02_TRAP" : "MSP_STALL",
                             (mspPort ? (uint32_t)mspPort->portState : 0xFFU));
    }
}

void ariesProbeDumpReport(const char *reason, uint32_t arg)
{
    if (gProbe.dumpInProgress) {
        return;
    }
    gProbe.dumpInProgress = true;

    uint32_t prevMstatus = thejas32_atomic_enter();
    uint32_t mieReg = 0U;
    __asm__ volatile ("csrr %0, mie" : "=r"(mieReg));

    // Read LSR before IIR so live LSR error/DR bits are preserved untouched first
    uint32_t u0_lsr = THEJAS32_UART(0).UART_LSR;
    uint32_t u0_ie  = THEJAS32_UART(0).UART_IE;
    uint32_t u0_lcr = THEJAS32_UART(0).UART_LCR;
    uint32_t u0_iir = THEJAS32_UART(0).UART_IIR_FCR;

    uint32_t u1_lsr = THEJAS32_UART(1).UART_LSR;
    uint32_t u1_ie  = THEJAS32_UART(1).UART_IE;

    uint32_t intr_en   = THEJAS32_INTR_CTRL.INTR_EN;
    uint32_t intr_raw  = THEJAS32_INTR_CTRL.RAW_INTR;
    uint32_t intr_stat = THEJAS32_INTR_CTRL.INTR_STATUS;

    thejas32_early_uart_puts("\r\n\r\n[PROBE DUMP] reason=");
    thejas32_early_uart_puts(reason ? reason : "?");
    thejas32_early_uart_puts(" ms=");
    probePrintDec(millis());
    thejas32_early_uart_puts(" arg=0x");
    thejas32_early_uart_print_hex(arg);

    thejas32_early_uart_puts("\r\n  HW   : U0(IE=0x"); probePrintHex8((uint8_t)u0_ie);
    thejas32_early_uart_puts(" LCR=0x"); probePrintHex8((uint8_t)u0_lcr);
    thejas32_early_uart_puts(" LSR=0x"); probePrintHex8((uint8_t)u0_lsr);
    thejas32_early_uart_puts(" IIR=0x"); probePrintHex8((uint8_t)u0_iir);
    thejas32_early_uart_puts(") U1(IE=0x"); probePrintHex8((uint8_t)u1_ie);
    thejas32_early_uart_puts(" LSR=0x"); probePrintHex8((uint8_t)u1_lsr);
    thejas32_early_uart_puts(")\r\n  CTRL : INTR_EN=0x"); thejas32_early_uart_print_hex(intr_en);
    thejas32_early_uart_puts(" RAW=0x"); thejas32_early_uart_print_hex(intr_raw);
    thejas32_early_uart_puts(" STAT=0x"); thejas32_early_uart_print_hex(intr_stat);
    thejas32_early_uart_puts(" mstatus=0x"); thejas32_early_uart_print_hex(prevMstatus);
    thejas32_early_uart_puts(" mie=0x"); thejas32_early_uart_print_hex(mieReg);

    thejas32_early_uart_puts("\r\n  SCHED: main="); probePrintDec(gProbe.taskMainCnt);
    thejas32_early_uart_puts(" serial="); probePrintDec(gProbe.taskSerialCnt);
    thejas32_early_uart_puts(" irq0="); probePrintDec(gProbe.irqCnt[0]);
    thejas32_early_uart_puts(" irq1="); probePrintDec(gProbe.irqCnt[1]);
    thejas32_early_uart_puts(" irq7="); probePrintDec(gProbe.irqCnt[7]);
    thejas32_early_uart_puts(" irq8="); probePrintDec(gProbe.irqCnt[8]);
    thejas32_early_uart_puts(" irq10="); probePrintDec(gProbe.irqCnt[10]);
    thejas32_early_uart_puts(" irq11="); probePrintDec(gProbe.irqCnt[11]);
    thejas32_early_uart_puts(" oth="); probePrintDec(gProbe.irqOtherCnt);

    thejas32_early_uart_puts("\r\n  U0RX : isr="); probePrintDec(gProbe.u0IsrCnt);
    thejas32_early_uart_puts(" isrDR="); probePrintDec(gProbe.u0IsrRxBytes);
    thejas32_early_uart_puts(" isrNoDR="); probePrintDec(gProbe.u0IsrNoDrCnt);
    thejas32_early_uart_puts(" isrNoIRQ="); probePrintDec(gProbe.u0IsrNoIrqCnt);
    thejas32_early_uart_puts(" pollT="); probePrintDec(gProbe.u0PollTicks);
    thejas32_early_uart_puts(" pollDR="); probePrintDec(gProbe.u0PollDrBytes);
    thejas32_early_uart_puts(" platDR="); probePrintDec(gProbe.u0PlatPollDrBytes);

    thejas32_early_uart_puts("\r\n  U0ERR: OE="); probePrintDec(gProbe.u0OeCnt);
    thejas32_early_uart_puts(" PE="); probePrintDec(gProbe.u0PeCnt);
    thejas32_early_uart_puts(" FE="); probePrintDec(gProbe.u0FeCnt);
    thejas32_early_uart_puts(" BI="); probePrintDec(gProbe.u0BiCnt);
    thejas32_early_uart_puts(" drop="); probePrintDec(gProbe.u0DropCnt);
    thejas32_early_uart_puts(" txPoll="); probePrintDec(gProbe.u0TxPollBytes);
    thejas32_early_uart_puts(" txIsr="); probePrintDec(gProbe.u0TxIsrBytes);
    thejas32_early_uart_puts(" reconf="); probePrintDec(gProbe.u0ReconfCnt);

    if (gProbe.lastU0Port) {
        const serialPort_t *p = gProbe.lastU0Port;
        thejas32_early_uart_puts("\r\n  RING : rxH="); probePrintDec(p->rxBufferHead);
        thejas32_early_uart_puts(" rxT="); probePrintDec(p->rxBufferTail);
        thejas32_early_uart_puts(" txH="); probePrintDec(p->txBufferHead);
        thejas32_early_uart_puts(" txT="); probePrintDec(p->txBufferTail);
        thejas32_early_uart_puts(" cb=0x"); thejas32_early_uart_print_hex((uint32_t)(uintptr_t)p->rxCallback);
    }

    thejas32_early_uart_puts("\r\n  U1   : isr="); probePrintDec(gProbe.u1IsrCnt);
    thejas32_early_uart_puts(" noise="); probePrintDec(gProbe.u1NoiseCnt);
    thejas32_early_uart_puts(" supp="); probePrintDec(gProbe.u1SuppressCnt);
    thejas32_early_uart_puts(" rearm="); probePrintDec(gProbe.u1RearmCnt);

    thejas32_early_uart_puts("\r\n  MSP  : ok="); probePrintDec(gProbe.mspCmdOkCnt);
    thejas32_early_uart_puts(" lastCmd="); probePrintDec(gProbe.mspLastCmd);
    thejas32_early_uart_puts(" rx="); probePrintDec(gProbe.mspRxBytes);
    thejas32_early_uart_puts(" crcErr="); probePrintDec(gProbe.mspCrcErrCnt);
    thejas32_early_uart_puts(" cli02="); probePrintDec(gProbe.mspCli02Cnt);
    thejas32_early_uart_puts(" cliInt="); probePrintDec(gProbe.mspCliIntCnt);
    if (gProbe.lastMspPort) {
        const mspPort_t *m = gProbe.lastMspPort;
        thejas32_early_uart_puts(" | ps="); probePrintDec((uint32_t)m->portState);
        thejas32_early_uart_puts(" pks="); probePrintDec((uint32_t)m->packetState);
        thejas32_early_uart_puts(" off="); probePrintDec((uint32_t)m->offset);
        thejas32_early_uart_puts(" dSz="); probePrintDec((uint32_t)m->dataSize);
        thejas32_early_uart_puts(" cmd="); probePrintDec((uint32_t)m->cmdMSP);
        thejas32_early_uart_puts(" pend="); probePrintDec((uint32_t)m->pendingRequest);
    }

    thejas32_early_uart_puts("\r\n  ISR_H:");
    uint32_t totalIsr = gProbe.isrHead;
    uint32_t showIsr = (totalIsr < PROBE_ISR_RING_LEN) ? totalIsr : PROBE_ISR_RING_LEN;
    for (uint32_t i = totalIsr - showIsr; i < totalIsr; i++) {
        const probeIsrRec_t *r = &gProbe.isrRing[i % PROBE_ISR_RING_LEN];
        thejas32_early_uart_puts(" [");
        probePrintDec(r->ms16);
        thejas32_early_uart_puts(" i="); probePrintHex8(r->iir);
        thejas32_early_uart_puts(" l="); probePrintHex8(r->lsrBefore);
        thejas32_early_uart_puts(" n="); probePrintDec(r->rxTaken);
        thejas32_early_uart_putc(']');
    }

    thejas32_early_uart_puts("\r\n  RX_H :");
    uint32_t totalRx = gProbe.rxHead;
    uint32_t showRx = (totalRx < PROBE_RX_RING_LEN) ? totalRx : PROBE_RX_RING_LEN;
    for (uint32_t i = totalRx - showRx; i < totalRx; i++) {
        const probeRxRec_t *r = &gProbe.rxRing[i % PROBE_RX_RING_LEN];
        thejas32_early_uart_puts(" ");
        thejas32_early_uart_putc((char)r->srcTag);
        thejas32_early_uart_putc(':');
        probePrintHex8(r->byteVal);
        thejas32_early_uart_putc('(');
        probePrintHex8(r->lsr);
        thejas32_early_uart_putc(')');
    }

    thejas32_early_uart_puts("\r\n  MSP_H:");
    uint32_t totalMsp = gProbe.mspHead;
    uint32_t showMsp = (totalMsp < PROBE_MSP_RING_LEN) ? totalMsp : PROBE_MSP_RING_LEN;
    for (uint32_t i = totalMsp - showMsp; i < totalMsp; i++) {
        const probeMspRec_t *r = &gProbe.mspRing[i % PROBE_MSP_RING_LEN];
        thejas32_early_uart_puts(" [");
        probePrintDec(r->portState);
        thejas32_early_uart_putc('/');
        probePrintDec(r->pktState);
        thejas32_early_uart_putc(':');
        probePrintHex8(r->rxChar);
        thejas32_early_uart_putc('(');
        probePrintDec(r->offsetLo);
        thejas32_early_uart_putc('/');
        probePrintDec(r->dataSizeLo);
        thejas32_early_uart_putc(')');
        thejas32_early_uart_putc(']');
    }

    // [HARDWARE-DEBUG-PROBE: DBG-02] Dump trap & CSR telemetry
    thejas32_early_uart_puts("\r\n  TRAP : cnt="); probePrintDec(gProbe.trapHead);
    thejas32_early_uart_puts(" ent="); probePrintDec(probeTrapEnterCnt);
    thejas32_early_uart_puts(" badMpie="); probePrintDec(gProbe.badMpieTrapCnt);
    thejas32_early_uart_puts(" csr=");
    thejas32_early_uart_putc((gProbe.lastCsrSite >= 32U && gProbe.lastCsrSite < 127U) ? (char)gProbe.lastCsrSite : '?');
    thejas32_early_uart_putc('@'); probePrintDec(gProbe.lastCsrMs16);
    thejas32_early_uart_puts(" 0x"); thejas32_early_uart_print_hex(gProbe.lastCsrPre);
    thejas32_early_uart_puts("->0x"); thejas32_early_uart_print_hex(gProbe.lastCsrPost);

    if (gProbe.trigSite != 0U) {
        thejas32_early_uart_puts("\r\n  TRIG : site=");
        thejas32_early_uart_putc((gProbe.trigSite >= 32U && gProbe.trigSite < 127U) ? (char)gProbe.trigSite : '?');
        thejas32_early_uart_puts(" caller=0x"); thejas32_early_uart_print_hex(gProbe.trigCaller);
        thejas32_early_uart_puts(" lastOk=");
        thejas32_early_uart_putc((gProbeLastOkSite >= 32U && gProbeLastOkSite < 127U) ? (char)gProbeLastOkSite : '?');
        thejas32_early_uart_puts("@0x"); thejas32_early_uart_print_hex(gProbeLastOkCaller);
        thejas32_early_uart_puts(" hwMepc=0x"); thejas32_early_uart_print_hex(gProbe.trigHwMepc);
        thejas32_early_uart_puts(" hwMcause=0x"); thejas32_early_uart_print_hex(gProbe.trigHwMcause);
        thejas32_early_uart_puts(" hwMtval=0x"); thejas32_early_uart_print_hex(gProbe.trigHwMtval);
        thejas32_early_uart_puts(" hwStat=0x"); thejas32_early_uart_print_hex(gProbe.trigHwStat);
        thejas32_early_uart_puts(" hwRaw=0x"); thejas32_early_uart_print_hex(gProbe.trigHwRaw);
    }

    if (gProbe.warpUsCnt > 0U || gProbe.warpMsCnt > 0U) {
        thejas32_early_uart_puts("\r\n  WARP : u="); probePrintDec(gProbe.warpUsCnt);
        thejas32_early_uart_puts("(0x"); thejas32_early_uart_print_hex(gProbe.warpUsPrev);
        thejas32_early_uart_puts("->0x"); thejas32_early_uart_print_hex(gProbe.warpUsCur);
        thejas32_early_uart_puts(") m="); probePrintDec(gProbe.warpMsCnt);
        thejas32_early_uart_puts("(0x"); thejas32_early_uart_print_hex(gProbe.warpMsPrev);
        thejas32_early_uart_puts("->0x"); thejas32_early_uart_print_hex(gProbe.warpMsCur);
        thejas32_early_uart_putc(')');
    }

    if (gProbe.badTrapCaptured) {
        thejas32_early_uart_puts("\r\n  BAD_T: idx="); probePrintDec(gProbe.badTrapIdx);
        thejas32_early_uart_puts(" ms="); probePrintDec(gProbe.badTrap.ms16);
        thejas32_early_uart_puts(" c=0x"); thejas32_early_uart_print_hex(gProbe.badTrap.mcause);
        thejas32_early_uart_puts(" epc=0x"); thejas32_early_uart_print_hex(gProbe.badTrap.mepc);
        thejas32_early_uart_puts(" in=0x"); thejas32_early_uart_print_hex(gProbe.badTrap.mstatusIn);
        thejas32_early_uart_puts(" out=0x"); thejas32_early_uart_print_hex(gProbe.badTrap.mstatusOut);
        thejas32_early_uart_puts(" csr=");
        thejas32_early_uart_putc((gProbe.badTrapCsrSite >= 32U && gProbe.badTrapCsrSite < 127U) ? (char)gProbe.badTrapCsrSite : '?');
        thejas32_early_uart_puts(":0x"); thejas32_early_uart_print_hex(gProbe.badTrapCsrPre);
    }

    thejas32_early_uart_puts("\r\n  TR_H :");
    uint32_t totalTr = gProbe.trapHead;
    uint32_t showTr = (totalTr < PROBE_TRAP_RING_LEN) ? totalTr : PROBE_TRAP_RING_LEN;
    for (uint32_t i = totalTr - showTr; i < totalTr; i++) {
        const probeTrapRec_t *r = &gProbe.trapRing[i % PROBE_TRAP_RING_LEN];
        thejas32_early_uart_puts(" [");
        probePrintDec(r->ms16);
        thejas32_early_uart_putc(':');
        thejas32_early_uart_putc((r->mcause & 0x80000000UL) ? 'I' : 'E');
        probePrintHex8((uint8_t)(r->mcause & 0xFFU));
        thejas32_early_uart_putc('@');
        thejas32_early_uart_print_hex(r->mepc);
        thejas32_early_uart_putc(':');
        thejas32_early_uart_print_hex(r->mstatusIn);
        thejas32_early_uart_puts("->");
        thejas32_early_uart_print_hex(r->mstatusOut);
        thejas32_early_uart_putc(']');
    }
    // [/HARDWARE-DEBUG-PROBE: DBG-02]

    thejas32_early_uart_puts("\r\n[PROBE END]\r\n");

    thejas32_atomic_exit(prevMstatus);
    gProbe.dumpInProgress = false;
}

// [HARDWARE-DEBUG-PROBE: DBG-05] Boot-time Silicon Lab: full root-cause isolation matrix
static volatile uint32_t gLabIrqCnt = 0U;
static volatile uint32_t gLabSink = 0U;

enum {
    LAB_MODE_GPR_CHK         = 1U,
    LAB_MODE_MCYCLE_MILLIS   = 2U,
    LAB_MODE_MCYCLE_DIV_CHK  = 3U,
    LAB_MODE_MCYCLE_NODIV    = 4U,
    LAB_MODE_MCYCLE_NOP      = 6U,
    LAB_MODE_MCYCLE_MUL_CHK  = 7U,
    LAB_MODE_MCYCLE_CSR_READ = 8U,
    LAB_MODE_MCYCLE_DIV_ATOM = 9U,
    LAB_MODE_MULHU_TIME      = 10U,
    LAB_MODE_MMIO_POLL       = 11U,
    LAB_MODE_DIVU_ONLY       = 12U,
    LAB_MODE_REMU_ONLY       = 13U,
    LAB_MODE_SDIV_ONLY       = 14U,
    LAB_MODE_SREM_ONLY       = 15U,
};

static inline uint32_t labReadMcycle(void)
{
    uint32_t c = 0U;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(c));
    return c;
}

static inline uint64_t labReadMcycle64(void)
{
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi));
        __asm__ volatile ("csrr %0, mcycle" : "=r"(lo));
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

// Exact lockless mulhu+shift millis() candidate (zero div/divu/rem/remu instructions)
__attribute__((noinline)) static uint32_t labMulhuMillis(void)
{
    uint64_t c = labReadMcycle64();
    uint32_t hi = (uint32_t)(c >> 32);
    uint32_t lo = (uint32_t)c;
    uint32_t q_hi = (uint32_t)(((uint64_t)(hi >> 5) * 0x0A7C5AC5ULL) >> 39);
    uint32_t r_hi = hi - (q_hi * 100000U);
    uint64_t rem = (uint64_t)r_hi * 67296ULL + (uint64_t)lo;
    uint32_t carry = (uint32_t)(rem >> 32);
    uint32_t lo_sum = (uint32_t)rem;
    uint32_t rem_adj = carry * 67296U + lo_sum;
    uint32_t q_lo = (uint32_t)(((uint64_t)(rem_adj >> 5) * 0x0A7C5AC5ULL) >> 39);
    return (r_hi * 42949U) + (carry * 42949U) + q_lo;
}

static void labTimer1Isr(void)
{
    (void)THEJAS32_TIMER(1).EOI;
    gLabIrqCnt++;
}

static void labRunTimer1Case(const char *tag, uint8_t mode, uint8_t trapMode, uint32_t durationMs)
{
    probeTrapMode = trapMode;
    gLabIrqCnt = 0U;

    THEJAS32_TIMER(1).Control = 0U;
    (void)THEJAS32_TIMER(1).EOI;
    THEJAS32_TIMER(1).LoadCount = 4000U; // 40 us (25 kHz @ 100 MHz)
    THEJAS32_MEMORY_FENCE();

    uint32_t drops = 0U;
    uint32_t noEntDrops = 0U;
    uint32_t statPendDrops = 0U;
    uint32_t prevDrops = 0U;
    uint32_t arithErr = 0U;
    uint32_t errKind = 0U;
    uint32_t msJumps = 0U;
    uint32_t firstIter = 0U;
    uint32_t firstMstat = 0U;
    uint32_t firstIrq = 0U;
    uint32_t firstEnt = 0U;
    uint32_t firstTr = 0U;
    uint32_t firstHwMepc = 0U;
    uint32_t firstHwMcause = 0U;
    uint32_t firstHwMtval = 0U;
    uint32_t firstStat = 0U;
    uint32_t firstRaw = 0U;
    uint32_t badA = 0U, badD = 0U, badQ = 0U, badR = 0U, badSq = 0U;
    uint32_t badMsPrev = 0U, badMsCur = 0U;
    uint32_t d2It = 0U, d2Ent = 0U, d2Stat = 0U, d2Mepc = 0U, diffEpcCnt = 0U, diffCauseCnt = 0U;
    uint32_t iter = 0U;

    const uint32_t ent0 = probeTrapEnterCnt;
    const uint32_t tr0 = gProbe.trapHead;
    uint32_t lastEntSeen = 0U;

    thejas32_interrupt_register(THEJAS32_IRQ_TIMER1, labTimer1Isr);
    thejas32_interrupt_enable(THEJAS32_IRQ_TIMER1);
    __enable_irq();
    THEJAS32_TIMER(1).Control = 0x03U;
    THEJAS32_MEMORY_FENCE();

    const uint32_t targetCyc = durationMs * 100000U;
    const uint32_t c0 = labReadMcycle();
    timeMs_t prevMs = (mode == LAB_MODE_MULHU_TIME) ? labMulhuMillis() : millis();
    uint64_t prevC64 = 0ULL;

    while ((uint32_t)(labReadMcycle() - c0) < targetCyc) {
        iter++;
        if (mode == LAB_MODE_GPR_CHK) {
            uint32_t gprBad = 0U;
            __asm__ volatile (
                "li t0, 0x11111111\n\t"
                "li t1, 0x22222222\n\t"
                "li t2, 0x33333333\n\t"
                "li t3, 0x44444444\n\t"
                "li t4, 0x55555555\n\t"
                "li t5, 0x66666666\n\t"
                "li t6, 0x77777777\n\t"
                ".rept 24\n\tnop\n\t.endr\n\t"
                "li %0, 0\n\t"
                "li a4, 0x11111111\n\t"
                "beq t0, a4, 1f\n\t"
                "ori %0, %0, 1\n\t"
                "1: li a4, 0x22222222\n\t"
                "beq t1, a4, 2f\n\t"
                "ori %0, %0, 2\n\t"
                "2: li a4, 0x33333333\n\t"
                "beq t2, a4, 3f\n\t"
                "ori %0, %0, 4\n\t"
                "3: li a4, 0x44444444\n\t"
                "beq t3, a4, 4f\n\t"
                "ori %0, %0, 8\n\t"
                "4: li a4, 0x55555555\n\t"
                "beq t4, a4, 5f\n\t"
                "ori %0, %0, 16\n\t"
                "5: li a4, 0x66666666\n\t"
                "beq t5, a4, 6f\n\t"
                "ori %0, %0, 32\n\t"
                "6: li a4, 0x77777777\n\t"
                "beq t6, a4, 7f\n\t"
                "ori %0, %0, 64\n\t"
                "7:\n\t"
                : "=&r"(gprBad)
                :
                : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a4", "memory"
            );
            if (gprBad != 0U) {
                arithErr++;
                errKind |= gprBad;
            }
        } else if (mode == LAB_MODE_MCYCLE_MILLIS || mode == LAB_MODE_MULHU_TIME) {
            const timeMs_t curMs = (mode == LAB_MODE_MULHU_TIME) ? labMulhuMillis() : millis();
            const uint32_t stepMs = (uint32_t)(curMs - prevMs);
            if (stepMs > 5U) {
                if (msJumps == 0U) {
                    badMsPrev = prevMs;
                    badMsCur = curMs;
                }
                msJumps++;
            }
            prevMs = curMs;
            gLabSink ^= curMs;
        } else if (mode == LAB_MODE_MCYCLE_DIV_CHK) {
            uint32_t a = 0x12345670U + (iter & 0xFFU);
            uint32_t d = 97U + (iter & 7U);
            __asm__ volatile ("" : "+r"(a), "+r"(d));
            uint32_t q = a / d;
            uint32_t r = a % d;
            int32_t sa = (int32_t)a;
            int32_t sd = -(int32_t)d;
            int32_t sq = sa / sd;
            uint32_t qBad = ((uint32_t)(q * d) > a) ? 1U : 0U;
            uint32_t rBad = (r >= d || (q * d + r) != a) ? 2U : 0U;
            uint32_t sqBad = ((uint32_t)(sq * sd) != (a - r)) ? 4U : 0U;
            uint32_t kMask = qBad | rBad | sqBad;
            if (kMask != 0U) {
                if (arithErr == 0U) {
                    badA = a; badD = d; badQ = q; badR = r; badSq = (uint32_t)sq;
                }
                arithErr++;
                errKind |= kMask;
            }
            gLabSink ^= q ^ r ^ (uint32_t)sq;
        } else if (mode == LAB_MODE_MCYCLE_NODIV) {
            uint32_t x = iter * 2654435761U;
            x ^= (x >> 13);
            gLabSink ^= x;
        } else if (mode == LAB_MODE_MCYCLE_NOP) {
            __asm__ volatile (".rept 32\n\tnop\n\t.endr" ::: "memory");
        } else if (mode == LAB_MODE_MCYCLE_MUL_CHK) {
            uint32_t a = 0x12345670U + (iter & 0xFFU);
            uint32_t k = 0x80000001U;
            __asm__ volatile ("" : "+r"(a), "+r"(k));
            uint64_t p = (uint64_t)a * (uint64_t)k;
            uint64_t ref = ((uint64_t)a << 31) + (uint64_t)a;
            if (p != ref) {
                if (arithErr == 0U) {
                    badA = a; badQ = (uint32_t)(p >> 32); badR = (uint32_t)p;
                }
                arithErr++;
                errKind |= 8U;
            }
            gLabSink ^= (uint32_t)(p ^ (p >> 32));
        } else if (mode == LAB_MODE_MCYCLE_CSR_READ) {
            uint64_t c64 = labReadMcycle64();
            if (prevC64 != 0ULL && c64 < prevC64) {
                arithErr++;
                errKind |= 16U;
            }
            prevC64 = c64;
            gLabSink ^= (uint32_t)c64 ^ (uint32_t)(c64 >> 32);
        } else if (mode == LAB_MODE_MCYCLE_DIV_ATOM) {
            uint32_t a = 0x12345670U + (iter & 0xFFU);
            uint32_t d = 97U + (iter & 7U);
            __asm__ volatile ("" : "+r"(a), "+r"(d));
            uint32_t prev = thejas32_atomic_enter();
            if ((prev & 8U) == 0U) {
                prevDrops++;
            }
            uint32_t q = a / d;
            uint32_t r = a % d;
            int32_t sa = (int32_t)a;
            int32_t sd = -(int32_t)d;
            int32_t sq = sa / sd;
            thejas32_atomic_exit(prev);
            if ((q * d + r) != a || ((uint32_t)(sq * sd) != (a - r))) {
                arithErr++;
                errKind |= 32U;
            }
            gLabSink ^= q ^ (uint32_t)sq;
        } else if (mode == LAB_MODE_MMIO_POLL) {
            uint32_t t0v = THEJAS32_TIMER(0).CurrentValue;
            uint32_t u0l = THEJAS32_UART(0).UART_LSR;
            uint32_t rwi = THEJAS32_INTR_CTRL.RAW_INTR;
            gLabSink ^= t0v ^ u0l ^ rwi;
        } else if (mode == LAB_MODE_DIVU_ONLY) {
            uint32_t a = 0x12345670U + (iter & 0xFFU);
            uint32_t d = 97U + (iter & 7U);
            __asm__ volatile ("" : "+r"(a), "+r"(d));
            uint32_t q = a / d;
            uint64_t pd = (uint64_t)q * (uint64_t)d;
            if (pd > a || ((uint64_t)a - pd) >= d) {
                if (arithErr == 0U) {
                    badA = a; badD = d; badQ = q;
                }
                arithErr++;
                errKind |= 1U;
            }
            gLabSink ^= q;
        } else if (mode == LAB_MODE_REMU_ONLY) {
            uint32_t a = 0x12345670U + (iter & 0xFFU);
            uint32_t d = 100U;
            __asm__ volatile ("" : "+r"(a), "+r"(d));
            uint32_t r = a % d;
            uint32_t qRef = (uint32_t)(((uint64_t)a * 0x51EB851FULL) >> 37);
            if (r >= 100U || (qRef * 100U + r) != a) {
                if (arithErr == 0U) {
                    badA = a; badD = 100U; badQ = qRef; badR = r;
                }
                arithErr++;
                errKind |= 2U;
            }
            gLabSink ^= r;
        } else if (mode == LAB_MODE_SDIV_ONLY) {
            int32_t sa = (int32_t)(0x12345670U + (iter & 0xFFU));
            int32_t sd = -(int32_t)(97U + (iter & 7U));
            __asm__ volatile ("" : "+r"(sa), "+r"(sd));
            int32_t sq = sa / sd;
            int64_t prod = (int64_t)sq * (int64_t)sd;
            int64_t diff = (int64_t)sa - prod;
            if (diff < 0 || diff >= -(int64_t)sd) {
                if (arithErr == 0U) {
                    badA = (uint32_t)sa; badD = (uint32_t)(-sd); badSq = (uint32_t)sq;
                }
                arithErr++;
                errKind |= 4U;
            }
            gLabSink ^= (uint32_t)sq;
        } else if (mode == LAB_MODE_SREM_ONLY) {
            int32_t sa = -(int32_t)(0x12345670U + (iter & 0xFFU));
            int32_t sd = 100;
            __asm__ volatile ("" : "+r"(sa), "+r"(sd));
            int32_t sr = sa % sd;
            uint32_t ua = (uint32_t)(-sa);
            uint32_t qRef = (uint32_t)(((uint64_t)ua * 0x51EB851FULL) >> 37);
            if (sr > 0 || sr <= -100 || (qRef * 100U + (uint32_t)(-sr)) != ua) {
                if (arithErr == 0U) {
                    badA = ua; badD = 100U; badQ = qRef; badR = (uint32_t)sr;
                }
                arithErr++;
                errKind |= 64U;
            }
            gLabSink ^= (uint32_t)sr;
        }

        // Snapshot raw CSRs & controller state BEFORE calling __enable_irq()
        uint32_t m = probeReadMstatus();
        if ((m & 8U) == 0U) {
            uint32_t hwMepc   = probeReadMepc();
            uint32_t hwMcause = probeReadMcause();
            uint32_t hwMtval  = probeReadMtval();
            uint32_t intrStat = THEJAS32_INTR_CTRL.INTR_STATUS;
            uint32_t intrRaw  = THEJAS32_INTR_CTRL.RAW_INTR;
            uint32_t curEnt   = probeTrapEnterCnt - ent0;
            uint32_t curTr    = gProbe.trapHead - tr0;

            if (curEnt == lastEntSeen) {
                noEntDrops++;
            }
            if ((intrStat & (1UL << THEJAS32_IRQ_TIMER1)) != 0U) {
                statPendDrops++;
            }
            if (drops == 0U) {
                firstIter = iter;
                firstMstat = m;
                firstIrq = gLabIrqCnt;
                firstEnt = curEnt;
                firstTr = curTr;
                firstHwMepc = hwMepc;
                firstHwMcause = hwMcause;
                firstHwMtval = hwMtval;
                firstStat = intrStat;
                firstRaw = intrRaw;
            } else {
                if (drops == 1U) {
                    d2It = iter;
                    d2Ent = curEnt;
                    d2Stat = intrStat;
                    d2Mepc = hwMepc;
                }
                if (hwMepc != firstHwMepc) {
                    diffEpcCnt++;
                }
                if (hwMcause != firstHwMcause) {
                    diffCauseCnt++;
                }
            }
            lastEntSeen = curEnt;
            drops++;
            __enable_irq();
        } else {
            lastEntSeen = probeTrapEnterCnt - ent0;
        }
    }

    const uint32_t elapCyc = (uint32_t)(labReadMcycle() - c0);
    const uint32_t elapEnt = probeTrapEnterCnt - ent0;
    const uint32_t elapTr = gProbe.trapHead - tr0;

    THEJAS32_TIMER(1).Control = 0U;
    (void)THEJAS32_TIMER(1).EOI;
    thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);
    THEJAS32_MEMORY_FENCE();
    probeTrapMode = 0U;

    thejas32_early_uart_puts("[SILICON LAB] ");
    thejas32_early_uart_puts(tag);
    thejas32_early_uart_puts(" tm="); probePrintDec(trapMode);
    thejas32_early_uart_puts(" cyc="); probePrintDec(elapCyc);
    thejas32_early_uart_puts(" it="); probePrintDec(iter);
    thejas32_early_uart_puts(" ent="); probePrintDec(elapEnt);
    thejas32_early_uart_puts(" tr="); probePrintDec(elapTr);
    thejas32_early_uart_puts(" irqs="); probePrintDec(gLabIrqCnt);
    thejas32_early_uart_puts(" drops="); probePrintDec(drops);
    if (drops > 0U) {
        thejas32_early_uart_puts("(noE="); probePrintDec(noEntDrops);
        thejas32_early_uart_puts(",pend="); probePrintDec(statPendDrops);
        thejas32_early_uart_putc(')');
    }
    if (mode == LAB_MODE_MCYCLE_DIV_ATOM) {
        thejas32_early_uart_puts(" pre0="); probePrintDec(prevDrops);
    }
    if (arithErr > 0U) {
        thejas32_early_uart_puts(" err="); probePrintDec(arithErr);
        thejas32_early_uart_puts("(k=0x"); thejas32_early_uart_print_hex(errKind);
        thejas32_early_uart_puts(" a=0x"); thejas32_early_uart_print_hex(badA);
        thejas32_early_uart_puts(" d="); probePrintDec(badD);
        thejas32_early_uart_puts(" q=0x"); thejas32_early_uart_print_hex(badQ);
        thejas32_early_uart_puts(" r="); probePrintDec(badR);
        thejas32_early_uart_puts(" sq=0x"); thejas32_early_uart_print_hex(badSq);
        thejas32_early_uart_putc(')');
    }
    if (msJumps > 0U) {
        thejas32_early_uart_puts(" msJ="); probePrintDec(msJumps);
        thejas32_early_uart_puts("(0x"); thejas32_early_uart_print_hex(badMsPrev);
        thejas32_early_uart_puts("->0x"); thejas32_early_uart_print_hex(badMsCur);
        thejas32_early_uart_putc(')');
    }
    if (drops > 0U) {
        thejas32_early_uart_puts(" it0="); probePrintDec(firstIter);
        thejas32_early_uart_puts(" f(e="); probePrintDec(firstEnt);
        thejas32_early_uart_puts(",t="); probePrintDec(firstTr);
        thejas32_early_uart_puts(",i="); probePrintDec(firstIrq);
        thejas32_early_uart_puts(") m=0x"); thejas32_early_uart_print_hex(firstMstat);
        thejas32_early_uart_puts(" hw(c=0x"); thejas32_early_uart_print_hex(firstHwMcause);
        thejas32_early_uart_puts(",epc=0x"); thejas32_early_uart_print_hex(firstHwMepc);
        thejas32_early_uart_puts(",tv=0x"); thejas32_early_uart_print_hex(firstHwMtval);
        thejas32_early_uart_puts(",st=0x"); thejas32_early_uart_print_hex(firstStat);
        thejas32_early_uart_puts(",rw=0x"); thejas32_early_uart_print_hex(firstRaw);
        thejas32_early_uart_putc(')');
        if (drops > 1U) {
            thejas32_early_uart_puts(" d2(it="); probePrintDec(d2It);
            thejas32_early_uart_puts(",e="); probePrintDec(d2Ent);
            thejas32_early_uart_puts(",epc=0x"); thejas32_early_uart_print_hex(d2Mepc);
            thejas32_early_uart_puts(",st=0x"); thejas32_early_uart_print_hex(d2Stat);
            thejas32_early_uart_puts(",dE="); probePrintDec(diffEpcCnt);
            thejas32_early_uart_puts(",dC="); probePrintDec(diffCauseCnt);
            thejas32_early_uart_putc(')');
        }
    }
    thejas32_early_uart_puts("\r\n");
}

void ariesProbeRunSiliconLab(void)
{
    uint32_t mvendor = 0U, marchid = 0U, mimpid = 0U, misa = 0U, mtvec = 0U, mstat0 = 0U, mie0 = 0U;
    __asm__ volatile ("csrr %0, mvendorid" : "=r"(mvendor));
    __asm__ volatile ("csrr %0, marchid"   : "=r"(marchid));
    __asm__ volatile ("csrr %0, mimpid"    : "=r"(mimpid));
    __asm__ volatile ("csrr %0, misa"      : "=r"(misa));
    __asm__ volatile ("csrr %0, mtvec"     : "=r"(mtvec));
    mstat0 = probeReadMstatus();
    __asm__ volatile ("csrr %0, mie"       : "=r"(mie0));

    thejas32_early_uart_puts("[SILICON LAB] CPU vid=0x"); thejas32_early_uart_print_hex(mvendor);
    thejas32_early_uart_puts(" aid=0x"); thejas32_early_uart_print_hex(marchid);
    thejas32_early_uart_puts(" imp=0x"); thejas32_early_uart_print_hex(mimpid);
    thejas32_early_uart_puts(" misa=0x"); thejas32_early_uart_print_hex(misa);
    thejas32_early_uart_puts(" mtvec=0x"); thejas32_early_uart_print_hex(mtvec);
    thejas32_early_uart_puts(" m=0x"); thejas32_early_uart_print_hex(mstat0);
    thejas32_early_uart_puts(" mie=0x"); thejas32_early_uart_print_hex(mie0);
    thejas32_early_uart_puts("\r\n");

    gProbeFastArmed = 0U;
    probeTrapEnterCnt = 0U;

    // DBG-05 Matrix: tm=0 (baseline), tm=1 (no csrs 0x1800), tm=4 (no csrw mepc), tm=5 (INTERRUPT_Handler_0: neither)
    labRunTimer1Case("T2_GPR_T1   ", LAB_MODE_GPR_CHK,         1U, 20U);
    labRunTimer1Case("T2_GPR_T5   ", LAB_MODE_GPR_CHK,         5U, 20U);
    labRunTimer1Case("A0_MS_T0    ", LAB_MODE_MCYCLE_MILLIS,   0U, 20U);
    labRunTimer1Case("A1_MS_T1    ", LAB_MODE_MCYCLE_MILLIS,   1U, 20U);
    labRunTimer1Case("A4_MS_T4    ", LAB_MODE_MCYCLE_MILLIS,   4U, 20U);
    labRunTimer1Case("A5_MS_T5    ", LAB_MODE_MCYCLE_MILLIS,   5U, 20U);
    labRunTimer1Case("B0_DIV_T0   ", LAB_MODE_MCYCLE_DIV_CHK,  0U, 20U);
    labRunTimer1Case("B1_DIV_T1   ", LAB_MODE_MCYCLE_DIV_CHK,  1U, 20U);
    labRunTimer1Case("B4_DIV_T4   ", LAB_MODE_MCYCLE_DIV_CHK,  4U, 20U);
    labRunTimer1Case("B5_DIV_T5   ", LAB_MODE_MCYCLE_DIV_CHK,  5U, 20U);
    labRunTimer1Case("B2_DIVU_T5  ", LAB_MODE_DIVU_ONLY,       5U, 20U);
    labRunTimer1Case("B3_REMU_T5  ", LAB_MODE_REMU_ONLY,       5U, 20U);
    labRunTimer1Case("B4_SDIV_T5  ", LAB_MODE_SDIV_ONLY,       5U, 20U);
    labRunTimer1Case("B6_SREM_T5  ", LAB_MODE_SREM_ONLY,       5U, 20U);
    labRunTimer1Case("C0_NODIV_T0 ", LAB_MODE_MCYCLE_NODIV,    0U, 20U);
    labRunTimer1Case("C1_NODIV_T1 ", LAB_MODE_MCYCLE_NODIV,    1U, 20U);
    labRunTimer1Case("C4_NODIV_T4 ", LAB_MODE_MCYCLE_NODIV,    4U, 20U);
    labRunTimer1Case("C5_NODIV_T5 ", LAB_MODE_MCYCLE_NODIV,    5U, 20U);
    labRunTimer1Case("E0_NOP_T0   ", LAB_MODE_MCYCLE_NOP,      0U, 20U);
    labRunTimer1Case("E1_NOP_T1   ", LAB_MODE_MCYCLE_NOP,      1U, 20U);
    labRunTimer1Case("E5_NOP_T5   ", LAB_MODE_MCYCLE_NOP,      5U, 20U);
    labRunTimer1Case("F0_MUL_T0   ", LAB_MODE_MCYCLE_MUL_CHK,  0U, 20U);
    labRunTimer1Case("F1_MUL_T1   ", LAB_MODE_MCYCLE_MUL_CHK,  1U, 20U);
    labRunTimer1Case("F5_MUL_T5   ", LAB_MODE_MCYCLE_MUL_CHK,  5U, 20U);
    labRunTimer1Case("G1_MCYCLE_T1", LAB_MODE_MCYCLE_CSR_READ, 1U, 20U);
    labRunTimer1Case("G5_MCYCLE_T5", LAB_MODE_MCYCLE_CSR_READ, 5U, 20U);
    labRunTimer1Case("H1_DIV_ATOM ", LAB_MODE_MCYCLE_DIV_ATOM, 1U, 20U);
    labRunTimer1Case("H5_DIV_ATOM ", LAB_MODE_MCYCLE_DIV_ATOM, 5U, 20U);
    labRunTimer1Case("I1_MULHU_T1 ", LAB_MODE_MULHU_TIME,      1U, 20U);
    labRunTimer1Case("I5_MULHU_T5 ", LAB_MODE_MULHU_TIME,      5U, 20U);
    labRunTimer1Case("J1_MMIO_T1  ", LAB_MODE_MMIO_POLL,       1U, 20U);
    labRunTimer1Case("J5_MMIO_T5  ", LAB_MODE_MMIO_POLL,       5U, 20U);

    // Restore Timer 1 and run Betaflight runtime under tm=5 (VEGA SDK INTERRUPT_Handler_0 style)
    THEJAS32_TIMER(1).Control = 0U;
    THEJAS32_TIMER(1).LoadCount = 0U;
    (void)THEJAS32_TIMER(1).EOI;
    thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER1, NULL);
    THEJAS32_MEMORY_FENCE();
    probeTrapMode = 5U;
    probeTrapEnterCnt = 0U;
    gProbeInTrap = 0U;
    gProbeLastOkSite = 0U;
    gProbeLastOkCaller = 0U;

    volatile uint8_t *p = (volatile uint8_t *)&gProbe;
    for (uint32_t i = 0U; i < sizeof(gProbe); i++) {
        p[i] = 0U;
    }
    __enable_irq();
    gProbe.mieArmed = true;
    gProbeFastArmed = 1U;
    thejas32_early_uart_puts("[SILICON LAB] Done. Runtime tm=5 mstatus=0x");
    thejas32_early_uart_print_hex(probeReadMstatus());
    thejas32_early_uart_puts("\r\n");
}
// [/HARDWARE-DEBUG-PROBE: DBG-05]

#endif // ARIES_PROBE_DEBUG && !UNIT_TEST
