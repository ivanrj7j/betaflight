/*
 * This file is part of Betaflight.
 *
 * Target configuration guards and static assertions for C-DAC ARIES v3.0
 * (THEJAS32 SoC / VEGA ET1031 RISC-V RV32IM @ 100 MHz).
 *
 * Citations:
 *   - knowledgebase/codebase_evaluations/14_EVAL_TARGET_H_AND_CONFIG.md
 *   - knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md
 *   - PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (Microcomponent 02_C)
 */

#if defined(TARGET_BOARD_IDENTIFIER) && !defined(ARIESV3_CONFIG_ASSERTED)
#define ARIESV3_CONFIG_ASSERTED

/*
 * Static Assertion Guards for C-DAC ARIES v3.0 / THEJAS32 Silicon Reality:
 *
 * 1. Flight Loop Clamping (Ground Truth 4):
 *    The 100 MHz RV32IM core has no hardware FPU. Soft-float PID calculation
 *    takes ~290 us. Running at 8 kHz (denom 1) or 4 kHz (denom 2) consumes
 *    > 250% CPU capacity, causing complete flight loop stall and watchdog resets.
 *    Clamping to 1.0 kHz (DEFAULT_PID_PROCESS_DENOM >= 8) guarantees > 65% idle headroom.
 */
#if (DEFAULT_PID_PROCESS_DENOM < 8)
#error "DEFAULT_PID_PROCESS_DENOM must be >= 8 to clamp loop rate to <= 1.0 kHz on RV32IM soft-float core!"
#endif

/*
 * 2. In-SRAM Parameter Group Storage (Ground Truth 3):
 *    Physical THEJAS32 silicon lacks memory-mapped XIP flash at 0x20000000.
 *    Dereferencing flash pointers causes mcause = 7 (Store Access Fault).
 *    All configuration parameters must reside in internal SRAM.
 */
#if (!defined(CONFIG_IN_RAM))
#error "CONFIG_IN_RAM must be defined for THEJAS32 (no memory-mapped XIP flash available)!"
#endif

#if (defined(CONFIG_IN_FLASH))
#error "CONFIG_IN_FLASH is strictly prohibited on THEJAS32 silicon (causes mcause = 7 Store Access Fault)!"
#endif

/*
 * 3. Parameter Group Allocation Bounds:
 *    Betaflight 4.x active parameter groups require ~5.1 KB.
 *    Allocating 4096 bytes triggers FAILURE_CONFIG_STORE_FAILURE runtime boot panic.
 *    EEPROM_SIZE must strictly be >= 8192 bytes (8 KB).
 */
#if (EEPROM_SIZE < 8192)
#error "EEPROM_SIZE must be >= 8192 bytes for active Parameter Groups! Lower sizes panic with FAILURE_CONFIG_STORE_FAILURE."
#endif

#endif // TARGET_BOARD_IDENTIFIER && !ARIESV3_CONFIG_ASSERTED
