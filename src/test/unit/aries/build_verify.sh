#!/usr/bin/env bash
# ==============================================================================
# Build Verification and Binary Audit Script for Betaflight ARIES v3.0 (THEJAS32)
# Module T8 (Phase 7)
# ==============================================================================

set -o pipefail

TOOLCHAIN_BIN="/home/ivan/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin"
SIZE="${TOOLCHAIN_BIN}/riscv32-esp-elf-size"
NM="${TOOLCHAIN_BIN}/riscv32-esp-elf-nm"
OBJDUMP="${TOOLCHAIN_BIN}/riscv32-esp-elf-objdump"
ELF="obj/main/betaflight_ARIESV3.elf"
HEX="obj/betaflight_2026.12.0-alpha_ARIESV3.hex"

echo "=============================================================================="
echo "Running Module T8: ARIES v3.0 Build Verification & Binary Disassembly Audit"
echo "=============================================================================="

TOTAL_TESTS=8
PASSED_TESTS=0
FAILED_TESTS=0

report_pass() {
    local id="$1"
    local desc="$2"
    echo -e "[\e[32mPASS\e[0m] $id: $desc"
    ((PASSED_TESTS++))
}

report_fail() {
    local id="$1"
    local desc="$2"
    local reason="$3"
    echo -e "[\e[31mFAIL\e[0m] $id: $desc -> $reason"
    ((FAILED_TESTS++))
}

# T8-01: Clean build exits with code 0
echo "Step 1: Compiling TARGET=ARIESV3..."
make TARGET=ARIESV3 -j"$(nproc)" hex > /dev/null 2>&1
BUILD_RC=$?
if [ $BUILD_RC -eq 0 ] && [ -f "$ELF" ]; then
    report_pass "T8-01" "Clean build exits with code 0"
else
    report_fail "T8-01" "Clean build exits with code 0" "make exited with code $BUILD_RC"
fi

# Extract sizes
if [ -f "$ELF" ]; then
    SIZE_OUTPUT=$("$SIZE" "$ELF" | tail -n 1)
    TEXT_SIZE=$(echo "$SIZE_OUTPUT" | awk '{print $1}')
    DATA_SIZE=$(echo "$SIZE_OUTPUT" | awk '{print $2}')
    BSS_SIZE=$(echo "$SIZE_OUTPUT" | awk '{print $3}')
    FLASH_SIZE=$((TEXT_SIZE + DATA_SIZE))
    RAM_SIZE=$((DATA_SIZE + BSS_SIZE))
else
    FLASH_SIZE=9999999
    RAM_SIZE=9999999
fi

# T8-02: Flash footprint <= 800 KB (819,200 bytes)
FLASH_MAX=819200
if [ $FLASH_SIZE -le $FLASH_MAX ]; then
    report_pass "T8-02" "Flash footprint <= 800 KB ($FLASH_SIZE bytes / 800 KB)"
else
    report_fail "T8-02" "Flash footprint <= 800 KB" "$FLASH_SIZE exceeds limit $FLASH_MAX"
fi

# T8-03: RAM footprint <= 80 KB (81,920 bytes)
RAM_MAX=81920
if [ $RAM_SIZE -le $RAM_MAX ]; then
    report_pass "T8-03" "RAM footprint <= 80 KB ($RAM_SIZE bytes / 80 KB)"
else
    report_fail "T8-03" "RAM footprint <= 80 KB" "$RAM_SIZE exceeds limit $RAM_MAX"
fi

# T8-04: _start symbol at 0x20000000
START_ADDR=$("$NM" "$ELF" 2>/dev/null | grep -w "_start" | awk '{print $1}')
if [ "$START_ADDR" == "20000000" ]; then
    report_pass "T8-04" "_start entry point at 0x20000000"
else
    report_fail "T8-04" "_start entry point at 0x20000000" "Address is 0x$START_ADDR"
fi

# T8-05: trap_entry symbol at 0x200000f0
TRAP_ADDR=$("$NM" "$ELF" 2>/dev/null | grep -w "trap_entry" | awk '{print $1}')
if [ "$TRAP_ADDR" == "200000f0" ]; then
    report_pass "T8-05" "trap_entry vector at 0x200000f0"
else
    report_fail "T8-05" "trap_entry vector at 0x200000f0" "Address is 0x$TRAP_ADDR"
fi

# T8-06: Zero __adddf3/__muldf3/__divdf3 calls in PID loop functions
PID_DOUBLE=$("$OBJDUMP" -d "$ELF" 2>/dev/null | grep -A 200 "<subTaskPidController>:" | grep -E "(__adddf3|__muldf3|__divdf3|__subdf3)" | wc -l)
GYRO_DOUBLE=$("$OBJDUMP" -d "$ELF" 2>/dev/null | grep -A 200 "<taskGyroSample>:" | grep -E "(__adddf3|__muldf3|__divdf3|__subdf3)" | wc -l)
FILTER_DOUBLE=$("$OBJDUMP" -d "$ELF" 2>/dev/null | grep -A 200 "<taskFiltering>:" | grep -E "(__adddf3|__muldf3|__divdf3|__subdf3)" | wc -l)
TOTAL_DOUBLE=$((PID_DOUBLE + GYRO_DOUBLE + FILTER_DOUBLE))

if [ $TOTAL_DOUBLE -eq 0 ]; then
    report_pass "T8-06" "Zero double-precision soft-float calls in PID/Gyro/Filter functions"
else
    report_fail "T8-06" "Zero double-precision soft-float calls in PID loop" "Found $TOTAL_DOUBLE calls"
fi

# T8-07: No __udivdi3 calls in micros function
MICROS_UDIV=$("$OBJDUMP" -d "$ELF" 2>/dev/null | grep -A 50 "<micros>:" | grep "__udivdi3" | wc -l)
if [ $MICROS_UDIV -eq 0 ]; then
    report_pass "T8-07" "No __udivdi3 calls in micros function"
else
    report_fail "T8-07" "No __udivdi3 calls in micros function" "Found $MICROS_UDIV call(s) to __udivdi3 in micros"
fi

# T8-08: Intel HEX artifact exists and is non-empty
if [ -s "$HEX" ]; then
    HEX_BYTES=$(wc -c < "$HEX")
    report_pass "T8-08" "Intel HEX artifact exists and is non-empty ($HEX_BYTES bytes)"
else
    report_fail "T8-08" "Intel HEX artifact exists and is non-empty" "HEX file missing or 0 bytes"
fi

echo "=============================================================================="
echo "Phase 7 Summary: $PASSED_TESTS/$TOTAL_TESTS checks PASSED ($FAILED_TESTS FAILED)"
echo "=============================================================================="

if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
else
    exit 0
fi
