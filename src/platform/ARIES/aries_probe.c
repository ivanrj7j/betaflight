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

// [HARDWARE-DEBUG-PROBE: DBG-04] Referenced from startup.S trap_entry when ARIES_PROBE_DEBUG > 0
volatile uint8_t probeTrapMode = 0U;

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
    // [/HARDWARE-DEBUG-PROBE: DBG-02]

    const serialPort_t *lastU0Port;
    const mspPort_t *lastMspPort;
} gProbe;

// [HARDWARE-DEBUG-PROBE: DBG-02]
static inline uint32_t probeReadMstatus(void)
{
    uint32_t val = 0U;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(val));
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

static void probeCheckMieCommon(uint8_t siteTag, uint32_t mstatNow)
{
    if ((mstatNow & 8U) != 0U) {
        gProbe.mieArmed = true;
        return;
    }
    if (gProbe.mieArmed && !gProbe.mieLostDumped) {
        gProbe.mieLostDumped = true;
        ariesProbeDumpReport("MIE_LOST", ((uint32_t)siteTag << 24) | (gProbe.trapHead & 0xFFFFFFU));
    }
}

void ariesProbeRecordCsr(uint8_t siteTag, uint32_t prevMstatus)
{
    uint32_t postMstatus = probeReadMstatus();
    gProbe.lastCsrSite = siteTag;
    gProbe.lastCsrMs16 = (uint16_t)(millis() & 0xFFFFU);
    gProbe.lastCsrPre = prevMstatus;
    gProbe.lastCsrPost = postMstatus;

    if ((prevMstatus & 8U) != 0U && (postMstatus & 8U) == 0U && !gProbe.mieLostDumped) {
        gProbe.mieLostDumped = true;
        ariesProbeDumpReport("CSR_MIE_DROP", ((uint32_t)siteTag << 24) | (gProbe.trapHead & 0xFFFFFFU));
    }
}
// [/HARDWARE-DEBUG-PROBE: DBG-02]

void ariesProbeTickMain(void)
{
    gProbe.taskMainCnt++;
    probeCheckMieCommon('M', probeReadMstatus()); // [HARDWARE-DEBUG-PROBE: DBG-02]
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

    probeCheckMieCommon('S', probeReadMstatus()); // [HARDWARE-DEBUG-PROBE: DBG-02]

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
    thejas32_early_uart_puts(" badMpie="); probePrintDec(gProbe.badMpieTrapCnt);
    thejas32_early_uart_puts(" csr=");
    thejas32_early_uart_putc((gProbe.lastCsrSite >= 32U && gProbe.lastCsrSite < 127U) ? (char)gProbe.lastCsrSite : '?');
    thejas32_early_uart_putc('@'); probePrintDec(gProbe.lastCsrMs16);
    thejas32_early_uart_puts(" 0x"); thejas32_early_uart_print_hex(gProbe.lastCsrPre);
    thejas32_early_uart_puts("->0x"); thejas32_early_uart_print_hex(gProbe.lastCsrPost);

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

// [HARDWARE-DEBUG-PROBE: DBG-04] Boot-time Silicon Lab: reproduces & isolates MIE=0 / millis / div silicon behavior
static volatile uint32_t gLabIrqCnt = 0U;
static volatile uint8_t  gLabLongIsr = 0U;
static volatile uint32_t gLabSink = 0U;

enum {
    LAB_MODE_OLD_MILLIS_WAIT = 0U,
    LAB_MODE_OLD_DIV_MILLIS  = 1U,
    LAB_MODE_MCYCLE_MILLIS   = 2U,
    LAB_MODE_MCYCLE_DIV_CHK  = 3U,
    LAB_MODE_MCYCLE_NODIV    = 4U,
    LAB_MODE_MCYCLE_ATOMIC   = 5U,
};

static inline uint32_t labReadMcycle(void)
{
    uint32_t c = 0U;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(c));
    return c;
}

static void labTimer1Isr(void)
{
    (void)THEJAS32_TIMER(1).EOI;
    gLabIrqCnt++;
    if (gLabLongIsr) {
        const uint32_t start = labReadMcycle();
        while ((uint32_t)(labReadMcycle() - start) < 1500U) {
            __asm__ volatile ("nop");
        }
    }
}

static void labRunTimer1Case(const char *tag, uint8_t mode, uint8_t trapMode, uint8_t longIsr, uint32_t durationMs)
{
    probeTrapMode = trapMode;
    gLabIrqCnt = 0U;
    gLabLongIsr = longIsr;

    THEJAS32_TIMER(1).Control = 0U;
    (void)THEJAS32_TIMER(1).EOI;
    THEJAS32_TIMER(1).LoadCount = 4000U; // 40 us (25 kHz @ 100 MHz)
    THEJAS32_MEMORY_FENCE();

    uint32_t drops = 0U;
    uint32_t prevDrops = 0U;
    uint32_t divErr = 0U;
    uint32_t msJumps = 0U;
    uint32_t firstMstat = 0U;
    uint32_t firstIrq = 0U;
    uint32_t firstMepc = 0U;
    uint32_t firstIn = 0U;
    uint32_t firstOut = 0U;
    uint32_t badDivA = 0U;
    uint32_t badDivQ = 0U;
    uint32_t badMsPrev = 0U;
    uint32_t badMsNow = 0U;
    uint32_t iter = 0U;

    thejas32_interrupt_register(THEJAS32_IRQ_TIMER1, labTimer1Isr);
    thejas32_interrupt_enable(THEJAS32_IRQ_TIMER1);
    __enable_irq();
    THEJAS32_TIMER(1).Control = 0x03U;
    THEJAS32_MEMORY_FENCE();

    const uint32_t targetCyc = durationMs * 100000U;
    const uint32_t c0 = labReadMcycle();
    const timeMs_t t0 = millis();
    timeMs_t prevMs = t0;
    const bool useMillisExit = (mode == LAB_MODE_OLD_MILLIS_WAIT) || (mode == LAB_MODE_OLD_DIV_MILLIS);

    while (1) {
        iter++;
        if (useMillisExit) {
            const timeMs_t curMs = millis();
            const uint32_t stepMs = (uint32_t)(curMs - prevMs);
            if (stepMs > 5U && msJumps == 0U) {
                badMsPrev = prevMs;
                badMsNow = curMs;
            }
            if (stepMs > 5U) {
                msJumps++;
            }
            prevMs = curMs;
            if ((uint32_t)(curMs - t0) >= durationMs) {
                break;
            }
            if (mode == LAB_MODE_OLD_DIV_MILLIS) {
                uint32_t cyc = labReadMcycle();
                __asm__ volatile ("" : "+r"(cyc));
                uint32_t q = cyc / 100U;
                gLabSink ^= q;
            }
        } else {
            if ((uint32_t)(labReadMcycle() - c0) >= targetCyc) {
                break;
            }
            if (mode == LAB_MODE_MCYCLE_MILLIS) {
                const timeMs_t curMs = millis();
                const uint32_t stepMs = (uint32_t)(curMs - prevMs);
                if (stepMs > 5U && msJumps == 0U) {
                    badMsPrev = prevMs;
                    badMsNow = curMs;
                }
                if (stepMs > 5U) {
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
                if ((q * d + r) != a || ((uint32_t)(sq * sd) != (a - r))) {
                    if (divErr == 0U) {
                        badDivA = a;
                        badDivQ = q;
                    }
                    divErr++;
                }
                gLabSink ^= q ^ (uint32_t)sq;
            } else if (mode == LAB_MODE_MCYCLE_NODIV) {
                uint32_t x = iter * 2654435761U;
                x ^= (x >> 13);
                gLabSink ^= x;
            } else if (mode == LAB_MODE_MCYCLE_ATOMIC) {
                uint32_t prev = thejas32_atomic_enter();
                if ((prev & 8U) == 0U) {
                    prevDrops++;
                }
                for (int k = 0; k < 16; k++) {
                    __asm__ volatile ("nop");
                }
                thejas32_atomic_exit(prev);
            }
        }

        uint32_t m = probeReadMstatus();
        if ((m & 8U) == 0U) {
            if (drops == 0U) {
                firstMstat = m;
                firstIrq = gLabIrqCnt;
                if (gProbe.trapHead > 0U) {
                    const probeTrapRec_t *lastT = &gProbe.trapRing[(gProbe.trapHead - 1U) % PROBE_TRAP_RING_LEN];
                    firstMepc = lastT->mepc;
                    firstIn = lastT->mstatusIn;
                    firstOut = lastT->mstatusOut;
                }
            }
            drops++;
            __enable_irq();
        }
    }

    const uint32_t elapCyc = (uint32_t)(labReadMcycle() - c0);
    const timeMs_t t1 = millis();

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
    thejas32_early_uart_puts(" irqs="); probePrintDec(gLabIrqCnt);
    thejas32_early_uart_puts(" drops="); probePrintDec(drops);
    if (mode == LAB_MODE_MCYCLE_ATOMIC) {
        thejas32_early_uart_puts(" pre0="); probePrintDec(prevDrops);
    }
    if (divErr > 0U) {
        thejas32_early_uart_puts(" divErr="); probePrintDec(divErr);
        thejas32_early_uart_puts("(a=0x"); thejas32_early_uart_print_hex(badDivA);
        thejas32_early_uart_puts(",q=0x"); thejas32_early_uart_print_hex(badDivQ);
        thejas32_early_uart_puts(")");
    }
    if (msJumps > 0U || useMillisExit) {
        thejas32_early_uart_puts(" msJ="); probePrintDec(msJumps);
        thejas32_early_uart_puts(" t="); probePrintDec(t0);
        thejas32_early_uart_puts("->"); probePrintDec(t1);
        if (msJumps > 0U) {
            thejas32_early_uart_puts("(0x"); thejas32_early_uart_print_hex(badMsPrev);
            thejas32_early_uart_puts("->0x"); thejas32_early_uart_print_hex(badMsNow);
            thejas32_early_uart_puts(")");
        }
    }
    if (drops > 0U) {
        thejas32_early_uart_puts(" firstIrq="); probePrintDec(firstIrq);
        thejas32_early_uart_puts(" m=0x"); thejas32_early_uart_print_hex(firstMstat);
        thejas32_early_uart_puts(" epc=0x"); thejas32_early_uart_print_hex(firstMepc);
        thejas32_early_uart_puts(" in=0x"); thejas32_early_uart_print_hex(firstIn);
        thejas32_early_uart_puts(" out=0x"); thejas32_early_uart_print_hex(firstOut);
    }
    thejas32_early_uart_puts("\r\n");
}

void ariesProbeRunSiliconLab(void)
{
    uint32_t m0 = 0U, sM1 = 0U, sM2 = 0U, sM3 = 0U, sM4 = 0U;
    uint32_t anom = 0U;

    __disable_irq();
    m0 = probeReadMstatus();
    for (int i = 0; i < 32; i++) {
        uint32_t m1, m2, prev, m3, m4;
        __asm__ volatile ("csrc mstatus, 8" ::: "memory");
        m1 = probeReadMstatus();
        __asm__ volatile ("csrs mstatus, 8" ::: "memory");
        m2 = probeReadMstatus();
        prev = thejas32_atomic_enter();
        m3 = probeReadMstatus();
        thejas32_atomic_exit(prev);
        m4 = probeReadMstatus();
        if (i == 0) {
            sM1 = m1; sM2 = m2; sM3 = m3; sM4 = m4;
        }
        if (((m1 & 8U) != 0U) || ((m2 & 8U) == 0U) || ((m3 & 8U) != 0U) || ((m4 & 8U) == 0U) || ((m1 ^ m2) & ~8U)) {
            anom++;
        }
        __disable_irq();
    }

    thejas32_early_uart_puts("[SILICON LAB] T1_CSR m0=0x"); thejas32_early_uart_print_hex(m0);
    thejas32_early_uart_puts(" csrc=0x"); thejas32_early_uart_print_hex(sM1);
    thejas32_early_uart_puts(" csrs=0x"); thejas32_early_uart_print_hex(sM2);
    thejas32_early_uart_puts(" ent=0x"); thejas32_early_uart_print_hex(sM3);
    thejas32_early_uart_puts(" ext=0x"); thejas32_early_uart_print_hex(sM4);
    thejas32_early_uart_puts(" anom="); probePrintDec(anom);
    thejas32_early_uart_puts("\r\n");

    // 1. Reproduce DBG-03 early-exit millis() cases with full cycle/ms diagnostics
    labRunTimer1Case("L1_OLD_T2  ", LAB_MODE_OLD_MILLIS_WAIT, 0U, 0U, 30U);
    labRunTimer1Case("L2_OLD_T3  ", LAB_MODE_OLD_DIV_MILLIS,  0U, 1U, 30U);

    // 2. mcycle-timed 20 ms (~500 IRQ) A/B isolation: trapMode 0=csrs1800, 1=skip1800, 2=csrs+fence, 3=skip+fence
    labRunTimer1Case("A0_MS_T0   ", LAB_MODE_MCYCLE_MILLIS,   0U, 0U, 20U);
    labRunTimer1Case("A1_MS_T1   ", LAB_MODE_MCYCLE_MILLIS,   1U, 0U, 20U);
    labRunTimer1Case("A2_MS_T2   ", LAB_MODE_MCYCLE_MILLIS,   2U, 0U, 20U);
    labRunTimer1Case("A3_MS_T3   ", LAB_MODE_MCYCLE_MILLIS,   3U, 0U, 20U);

    // 3. Hardware div/divu/remu integrity + MIE drop test vs pure ALU (no div)
    labRunTimer1Case("B0_DIV_T0  ", LAB_MODE_MCYCLE_DIV_CHK,  0U, 0U, 20U);
    labRunTimer1Case("B1_DIV_T1  ", LAB_MODE_MCYCLE_DIV_CHK,  1U, 0U, 20U);
    labRunTimer1Case("C0_NODIV_T0", LAB_MODE_MCYCLE_NODIV,    0U, 0U, 20U);
    labRunTimer1Case("D1_ATOM_T1 ", LAB_MODE_MCYCLE_ATOMIC,   1U, 1U, 20U);

    // Restore Timer 1 and probe state cleanly before Betaflight init
    THEJAS32_TIMER(1).Control = 0U;
    THEJAS32_TIMER(1).LoadCount = 0U;
    (void)THEJAS32_TIMER(1).EOI;
    thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER1, NULL);
    THEJAS32_MEMORY_FENCE();
    probeTrapMode = 0U;

    volatile uint8_t *p = (volatile uint8_t *)&gProbe;
    for (uint32_t i = 0U; i < sizeof(gProbe); i++) {
        p[i] = 0U;
    }
    __enable_irq();
    thejas32_early_uart_puts("[SILICON LAB] Done. Post-lab mstatus=0x");
    thejas32_early_uart_print_hex(probeReadMstatus());
    thejas32_early_uart_puts("\r\n");
}
// [/HARDWARE-DEBUG-PROBE: DBG-03]

#endif // ARIES_PROBE_DEBUG && !UNIT_TEST
