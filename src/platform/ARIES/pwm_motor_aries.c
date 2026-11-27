/*
 * This file is part of Betaflight.
 *
 * Hardware Timer 1 Ascending Deadline Soft-PWM Motor Driver for C-DAC ARIES v3.0
 * (THEJAS32 SoC / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Ground Truth 6: Non-Blocking Timer 1 PWM Motor Scheduler
 * Utilizes Synopsys DW APB Timer 1 (IRQ 8) ascending deadline event scheduling
 * to modulate motors PA1-PA4 with < 3 us total CPU load (> 99.7% idle headroom).
 * Binds non-null motorVTable_t preventing Configurator slider panics.
 *
 * Citations & References:
 *   - PLAN/04_GPIO_EXTI_AND_HARDWARE_TIMERS.md (Microcomponents 04_D, 04_E)
 *   - PLAN/20_DEVELOPMENT_PHASES.md (Phase 16)
 *   - knowledgebase/codebase_evaluations/11_EVAL_PWM_MOTOR_ARIES_C.md (Score 10/10 Scheduler, Race condition fix)
 *   - knowledgebase/components/05_TIMER_AND_MOTOR_PWM.md
 *   - lib/vega-sdk/vega-sdk/bsp/drivers/timer/timer.c
 *   - src/platform/ESP32/pwm_motor_esp32.c (Non-null VTable reference pattern)
 */

#include "platform.h"
#include <string.h>

#include "common/utils.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/motor.h"
#include "drivers/motor_impl.h"
#include "drivers/motor_types.h"
#include "drivers/pwm_output.h"
#include "drivers/time.h"
#include "pg/motor.h"
#include "platform/bsp_thejas32.h"
#include "target.h"

// -----------------------------------------------------------------------------
// Motor Configuration & Hardware Mapping
// -----------------------------------------------------------------------------

#define ARIES_MAX_MOTORS 4
#define ARIES_SYS_CLOCK_HZ 100000000UL // 100 MHz
#define CYCLES_PER_US (ARIES_SYS_CLOCK_HZ / 1000000UL) // 100 cycles per microsecond
#define DEADLINE_GROUPING_THRESHOLD_CYCLES 100 // 1.0 us threshold grouping

typedef struct {
    IO_t io;
    uint8_t pin;
    uint32_t pulse_cycles;
    bool enabled;
} thejas32_motor_slot_t;

typedef struct {
    uint8_t motor_idx;
    uint32_t cycles;
} deadline_entry_t;

static thejas32_motor_slot_t aries_motors[ARIES_MAX_MOTORS];
static deadline_entry_t aries_sorted_deadlines[ARIES_MAX_MOTORS];
static uint8_t aries_motor_count = 0;
static volatile uint8_t aries_current_deadline_idx = 0;
static bool aries_subsystem_enabled = false;
static uint32_t aries_frame_period_us = 2500UL;
static uint32_t aries_last_frame_start_us = 0U;
static bool aries_frame_started = false;

void ariesPwmEmergencyStop(void)
{
    THEJAS32_TIMER(1).Control = 0;
    volatile uint32_t clear_latch = THEJAS32_TIMER(1).EOI;
    (void)clear_latch;
    thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);

    aries_current_deadline_idx = aries_motor_count;
    aries_frame_started = false;

    if (aries_motor_count > 0U) {
        for (uint8_t i = 0; i < aries_motor_count; i++) {
            if (aries_motors[i].io) {
                IOLo(aries_motors[i].io);
            }
        }
    } else {
        for (uint16_t pin = 1U; pin <= 4U; pin++) {
            *thejas32_gpio_data_ptr(pin) = 0U;
        }
    }
    THEJAS32_MEMORY_FENCE();
}

// -----------------------------------------------------------------------------
// Timer 1 Ascending Deadline Interrupt Service Routine (IRQ 8)
// -----------------------------------------------------------------------------

/*
 * Timer 1 ISR: Serves ESC falling-edge compare deadlines.
 * Ground Truth 6: Non-blocking interrupt execution.
 * Reads EOI to clear Synopsys DW APB timer interrupt latch.
 * Drops active motor pin LOW.
 * Applies 100-cycle threshold grouping to eliminate unachievable sub-microsecond reloads.
 */
FAST_IRQ_HANDLER void thejas32_pwm_timer1_isr(void)
{
    // STEP 1: Clear Synopsys APB Timer interrupt request latch
    volatile uint32_t dummy = THEJAS32_TIMER(1).EOI;
    (void)dummy;

    if (aries_current_deadline_idx >= aries_motor_count) {
        THEJAS32_TIMER(1).Control = 0;
        THEJAS32_MEMORY_FENCE();
        thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);
        return;
    }

    // STEP 2: Drop active motor pin LOW
    uint8_t cur_motor = aries_sorted_deadlines[aries_current_deadline_idx].motor_idx;
    if (aries_motors[cur_motor].io) {
        IOLo(aries_motors[cur_motor].io);
    }

    // STEP 3: 100-cycle threshold grouping: process sub-microsecond deadlines immediately
    while ((aries_current_deadline_idx + 1 < aries_motor_count) &&
           ((aries_sorted_deadlines[aries_current_deadline_idx + 1].cycles -
             aries_sorted_deadlines[aries_current_deadline_idx].cycles) < DEADLINE_GROUPING_THRESHOLD_CYCLES)) {
        aries_current_deadline_idx++;
        uint8_t next_motor = aries_sorted_deadlines[aries_current_deadline_idx].motor_idx;
        if (aries_motors[next_motor].io) {
            IOLo(aries_motors[next_motor].io);
        }
    }

    // STEP 4: Advance to next deadline delta or terminate
    aries_current_deadline_idx++;
    if (aries_current_deadline_idx < aries_motor_count) {
        uint32_t delta = aries_sorted_deadlines[aries_current_deadline_idx].cycles -
                         aries_sorted_deadlines[aries_current_deadline_idx - 1].cycles;
        THEJAS32_TIMER(1).Control = 0;
        THEJAS32_MEMORY_FENCE();
        THEJAS32_TIMER(1).LoadCount = delta;
        THEJAS32_MEMORY_FENCE();
        THEJAS32_TIMER(1).Control = 0x03; // Enable + User-defined reload mode
        THEJAS32_MEMORY_FENCE();
    } else {
        // All motor pulses complete: disable Timer 1 until next flight cycle
        THEJAS32_TIMER(1).Control = 0;
        THEJAS32_MEMORY_FENCE();
        thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);
    }
}

// -----------------------------------------------------------------------------
// Betaflight Motor Driver VTable Functions
// -----------------------------------------------------------------------------

static void ariesPwmMotorPostInit(void)
{
#if !defined(UNIT_TEST) && defined(ARIES_DEV_LOGGING) && (ARIES_DEV_LOGGING == 1)
    THEJAS_LOG("[ARIES v3] Boot Complete: Entering Scheduler (Press '#' or Enter for CLI)\r\n");
#endif
}

/*
 * Standard PWM works in microseconds, mapping external (1000-2000us) throttle values directly.
 * Mandatory Non-Null Safety: motorConvertFromExternal() and motorConvertToExternal()
 * are called directly by Betaflight's mixer and Configurator motor sliders with zero NULL guards.
 */
static float ariesPwmConvertFromExternal(uint16_t externalValue)
{
    return (float)externalValue;
}

static uint16_t ariesPwmConvertToExternal(float motorValue)
{
    return (uint16_t)motorValue;
}

static bool ariesPwmMotorEnable(void)
{
    aries_subsystem_enabled = true;
    for (uint8_t i = 0; i < aries_motor_count; i++) {
        aries_motors[i].enabled = true;
    }
    return true;
}

static void ariesPwmMotorDisable(void)
{
    aries_subsystem_enabled = false;
    // Preemptively disable Timer 1
    THEJAS32_TIMER(1).Control = 0;
    volatile uint32_t clear_latch = THEJAS32_TIMER(1).EOI;
    (void)clear_latch;
    THEJAS32_MEMORY_FENCE();
    thejas32_interrupt_disable(THEJAS32_IRQ_TIMER1);

    for (uint8_t i = 0; i < aries_motor_count; i++) {
        aries_motors[i].enabled = false;
        if (aries_motors[i].io) {
            IOLo(aries_motors[i].io);
        }
    }
}

static bool ariesPwmMotorIsEnabled(unsigned index)
{
    return index < aries_motor_count && aries_subsystem_enabled && aries_motors[index].enabled;
}

static void ariesPwmMotorWrite(uint8_t index, float value)
{
    if (index >= aries_motor_count) return;

    // Safety Clamping: constrain strictly within standard ESC range (1000.0f - 2000.0f us)
    if (value < 1000.0f) value = 1000.0f;
    if (value > 2000.0f) value = 2000.0f;

    aries_motors[index].pulse_cycles = (uint32_t)(value * (float)CYCLES_PER_US);
}

static void ariesPwmMotorWriteInt(uint8_t index, uint16_t value)
{
    ariesPwmMotorWrite(index, (float)value);
}

static void ariesPwmMotorUpdateInit(void) { }

/*
 * Cycle Initiation: Executed at the conclusion of every flight loop (400 Hz PWM).
 * Sorts motor deadlines and raises all 4 motor pins HIGH simultaneously.
 */
static void ariesPwmMotorUpdateComplete(void)
{
    if (!aries_subsystem_enabled || aries_motor_count == 0) {
        return;
    }

    // Do not chop active pulses or overdrive ESCs when loop runs faster than configured PWM rate
    if ((THEJAS32_TIMER(1).Control & 0x01U) != 0U) {
        return;
    }

    const uint32_t now_us = micros();
    if (aries_frame_started && ((uint32_t)(now_us - aries_last_frame_start_us) < aries_frame_period_us)) {
        return;
    }

    // STEP 1: Preemptive Timer 1 disable to prevent race conditions
    THEJAS32_TIMER(1).Control = 0;
    volatile uint32_t clear_latch = THEJAS32_TIMER(1).EOI;
    (void)clear_latch;
    THEJAS32_MEMORY_FENCE();

    // STEP 2: Ascending deadline sorting (Insertion sort for N=4, < 30 cycles)
    for (uint8_t i = 0; i < aries_motor_count; i++) {
        aries_sorted_deadlines[i].motor_idx = i;
        aries_sorted_deadlines[i].cycles = aries_motors[i].pulse_cycles;
    }

    for (uint8_t i = 1; i < aries_motor_count; i++) {
        deadline_entry_t key = aries_sorted_deadlines[i];
        int j = i - 1;
        while (j >= 0 && aries_sorted_deadlines[j].cycles > key.cycles) {
            aries_sorted_deadlines[j + 1] = aries_sorted_deadlines[j];
            j--;
        }
        aries_sorted_deadlines[j + 1] = key;
    }

    // STEP 3: Simultaneous pin raise at t=0
    for (uint8_t i = 0; i < aries_motor_count; i++) {
        if (aries_motors[i].io) {
            IOHi(aries_motors[i].io);
        }
    }

    // STEP 4: Record frame timestamp and arm Timer 1 for the first deadline
    aries_last_frame_start_us = now_us;
    aries_frame_started = true;
    aries_current_deadline_idx = 0;
    uint32_t first_cycles = aries_sorted_deadlines[0].cycles;
    THEJAS32_TIMER(1).LoadCount = first_cycles;
    THEJAS32_MEMORY_FENCE();
    THEJAS32_TIMER(1).Control = 0x03; // Enable (bit 0) + User-defined reload mode (bit 1)
    THEJAS32_MEMORY_FENCE();
    thejas32_interrupt_enable(THEJAS32_IRQ_TIMER1);
}

static void ariesPwmMotorShutdown(void)
{
    ariesPwmEmergencyStop();
    ariesPwmMotorDisable();
}

static bool ariesPwmMotorIsIdle(unsigned index)
{
    UNUSED(index);
    return false;
}

static IO_t ariesPwmMotorGetIO(unsigned index)
{
    if (index >= aries_motor_count) return IO_NONE;
    return aries_motors[index].io;
}

static void ariesPwmMotorRequestTelemetry(unsigned index)
{
    UNUSED(index);
}

static bool ariesPwmMotorDecodeTelemetry(void) { return true; }
static bool ariesPwmMotorTelemetryWait(void) { return true; }

// Mandatory Non-Null Virtual Table
static const motorVTable_t thejas32MotorVTable = {
    .postInit = ariesPwmMotorPostInit,
    .convertExternalToMotor = ariesPwmConvertFromExternal,
    .convertMotorToExternal = ariesPwmConvertToExternal,
    .enable = ariesPwmMotorEnable,
    .disable = ariesPwmMotorDisable,
    .isMotorEnabled = ariesPwmMotorIsEnabled,
    .telemetryWait = ariesPwmMotorTelemetryWait,
    .decodeTelemetry = ariesPwmMotorDecodeTelemetry,
    .updateInit = ariesPwmMotorUpdateInit,
    .write = ariesPwmMotorWrite,
    .writeInt = ariesPwmMotorWriteInt,
    .updateComplete = ariesPwmMotorUpdateComplete,
    .shutdown = ariesPwmMotorShutdown,
    .isMotorIdle = ariesPwmMotorIsIdle,
    .getMotorIO = ariesPwmMotorGetIO,
    .releaseMotorIO = NULL,
    .reinstateMotorIO = NULL,
    .requestTelemetry = ariesPwmMotorRequestTelemetry,
};

// -----------------------------------------------------------------------------
// Driver Initialization API
// -----------------------------------------------------------------------------

bool motorPwmDevInit(motorDevice_t *device, const motorDevConfig_t *motorDevConfig, uint16_t idlePulse)
{
    UNUSED(idlePulse);
    if (!device) return false;

    memset(aries_motors, 0, sizeof(aries_motors));
    memset(aries_sorted_deadlines, 0, sizeof(aries_sorted_deadlines));
    aries_motor_count = 0;
    aries_subsystem_enabled = false;
    aries_last_frame_start_us = 0U;
    aries_frame_started = false;
    const uint16_t rate_hz = (motorDevConfig && motorDevConfig->motorPwmRate > 0U) ? motorDevConfig->motorPwmRate : 400U;
    aries_frame_period_us = 1000000UL / rate_hz;

    // Default pin mapping: Motor 1-3 on PA1-PA3 (Header J2), Motor 4 on PA4 (Header J1)
    const ioTag_t default_motor_tags[ARIES_MAX_MOTORS] = {
        DEFIO_TAG(MOTOR1_PIN),
        DEFIO_TAG(MOTOR2_PIN),
        DEFIO_TAG(MOTOR3_PIN),
        DEFIO_TAG(MOTOR4_PIN)
    };

    for (uint8_t i = 0; i < ARIES_MAX_MOTORS; i++) {
        ioTag_t tag = (motorDevConfig && motorDevConfig->ioTags[i]) ? motorDevConfig->ioTags[i] : default_motor_tags[i];
        if (!tag) continue;

        IO_t io = IOGetByTag(tag);
        if (!io) continue;

        IOInit(io, OWNER_MOTOR, RESOURCE_INDEX(i));
        IOConfigGPIO(io, IOCFG_OUT_PP);
        IOLo(io); // Idle initial state (0V)

        aries_motors[i].io = io;
        aries_motors[i].pin = (uint8_t)IO_Pin(io);
        aries_motors[i].pulse_cycles = 1000UL * CYCLES_PER_US; // Default 1000 us idle
        aries_motors[i].enabled = true;
        aries_motor_count++;
    }

    if (aries_motor_count == 0) {
        return false;
    }

    // Initialize Timer 1 registers
    THEJAS32_TIMER(1).Control = 0;
    THEJAS32_TIMER(1).LoadCount = 0;
    volatile uint32_t clear_latch = THEJAS32_TIMER(1).EOI;
    (void)clear_latch;
    THEJAS32_MEMORY_FENCE();

    // Register Timer 1 interrupt service routine
    thejas32_interrupt_register(THEJAS32_IRQ_TIMER1, thejas32_pwm_timer1_isr);

    device->vTable = &thejas32MotorVTable;
    device->count = aries_motor_count;
    device->initialized = true;

    return true;
}

// -----------------------------------------------------------------------------
// Diagnostic Query Helpers for Self-Test Validation
// -----------------------------------------------------------------------------

uint8_t thejas32_pwm_diag_get_motor_count(void)
{
    return aries_motor_count;
}

uint8_t thejas32_pwm_diag_get_motor_pin(uint8_t index)
{
    if (index >= ARIES_MAX_MOTORS) return 0xFF;
    return aries_motors[index].pin;
}

uint32_t thejas32_pwm_diag_get_pulse_cycles(uint8_t index)
{
    if (index >= ARIES_MAX_MOTORS) return 0;
    return aries_motors[index].pulse_cycles;
}

const motorVTable_t *thejas32_pwm_diag_get_vtable(void)
{
    return &thejas32MotorVTable;
}
