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

TOTAL_TESTS=10
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

# T8-02: Flash/Image footprint <= 248 KB (253,952 bytes)
FLASH_MAX=253952
if [ $FLASH_SIZE -le $FLASH_MAX ]; then
    report_pass "T8-02" "Binary footprint <= 248 KB ($FLASH_SIZE bytes / 248 KB)"
else
    report_fail "T8-02" "Binary footprint <= 248 KB" "$FLASH_SIZE exceeds limit $FLASH_MAX"
fi

# T8-03: RAM runtime footprint <= 40 KB (40,960 bytes)
RAM_MAX=40960
if [ $RAM_SIZE -le $RAM_MAX ]; then
    report_pass "T8-03" "RAM runtime footprint <= 40 KB ($RAM_SIZE bytes / 40 KB)"
else
    report_fail "T8-03" "RAM runtime footprint <= 40 KB" "$RAM_SIZE exceeds limit $RAM_MAX"
fi

# T8-04: _start symbol at 0x00202000 (Physical in-SRAM execution entry)
START_ADDR=$("$NM" "$ELF" 2>/dev/null | grep -w "_start" | awk '{print $1}')
if [ "$START_ADDR" == "00202000" ] || [ "$START_ADDR" == "202000" ]; then
    report_pass "T8-04" "_start entry point at 0x00202000 (in-SRAM)"
else
    report_fail "T8-04" "_start entry point at 0x00202000" "Address is 0x$START_ADDR"
fi

# T8-05: trap_entry symbol at 0x002020f0
TRAP_ADDR=$("$NM" "$ELF" 2>/dev/null | grep -w "trap_entry" | awk '{print $1}')
if [ "$TRAP_ADDR" == "002020f0" ] || [ "$TRAP_ADDR" == "2020f0" ]; then
    report_pass "T8-05" "trap_entry vector at 0x002020f0 (in-SRAM)"
else
    report_fail "T8-05" "trap_entry vector at 0x002020f0" "Address is 0x$TRAP_ADDR"
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

# T8-09: Exactly zero 16-bit compressed instructions (RVC) in executable code
ETEXT_ADDR=$("$NM" "$ELF" 2>/dev/null | grep -w "_etext" | awk '{print $1}')
if [ -n "$ETEXT_ADDR" ]; then
    RVC_COUNT=$("$OBJDUMP" -d "$ELF" --stop-address=0x"$ETEXT_ADDR" 2>/dev/null | grep -E "^\s*[0-9a-f]+:\s+[0-9a-f]{4}\s+" | wc -l)
    if [ "$RVC_COUNT" -eq 0 ]; then
        report_pass "T8-09" "Zero 16-bit compressed instructions in executable code (RV32IM pure 32-bit)"
    else
        report_fail "T8-09" "Zero 16-bit compressed instructions in executable code" "Found $RVC_COUNT compressed opcodes"
    fi
else
    report_fail "T8-09" "Zero 16-bit compressed instructions" "_etext symbol not found"
fi

# T8-10: Raw binary artifact exists and fits within 248 KB physical limit
BIN="obj/betaflight_2026.12.0-alpha_ARIESV3.bin"
if [ -s "$BIN" ]; then
    BIN_BYTES=$(wc -c < "$BIN")
    if [ "$BIN_BYTES" -le 253952 ]; then
        report_pass "T8-10" "Raw binary artifact exists and is under 248 KB ($BIN_BYTES bytes / 253,952 bytes)"
    else
        report_fail "T8-10" "Raw binary artifact exceeds 248 KB" "$BIN_BYTES bytes exceeds 253952"
    fi
else
    report_fail "T8-10" "Raw binary artifact exists" "BIN file missing or 0 bytes"
fi

echo "=============================================================================="
echo "Phase 7 Summary: $PASSED_TESTS/$TOTAL_TESTS checks PASSED ($FAILED_TESTS FAILED)"
echo "=============================================================================="

if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
else
    exit 0
fi
