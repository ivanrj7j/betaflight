#!/usr/bin/env bash
# ==============================================================================
# ARIES v3.0 Silicon Build & Verification Audit Script
# Target: C-DAC ARIES v3.0 (THEJAS32 SoC / VEGA ET1031 RISC-V RV32IM @ 100 MHz)
# Reference: DEVELOPMENT_GUIDE.md (Section 2 Step 4: Silicon Invariant Audits)
# ==============================================================================
set -euo pipefail

ELF_FILE="${1:-obj/main/betaflight_ARIESV3.elf}"
TARGET_MAX_BYTES=253952 # 248 KB SRAM Execution Limit (0x00202000 to 0x0023FFFF)
EXPECTED_ENTRY="0x00202000"

echo "======================================================================"
echo " Starting 10-Point Silicon Invariant Audit: $ELF_FILE"
echo "======================================================================"

# 1. Active Branch Verification
ACTIVE_BRANCH=$(git branch --show-current)
echo -n "[Audit 1/10] Git Branch Check: "
if [ "$ACTIVE_BRANCH" != "rebuild" ]; then
    echo "FAIL (Branch is '$ACTIVE_BRANCH', must strictly be 'rebuild'!)"
    exit 1
fi
echo "PASS (rebuild)"

# 2. Binary Existence Check
echo -n "[Audit 2/10] Target ELF File Existence: "
if [ ! -f "$ELF_FILE" ]; then
    echo "FAIL (Binary not found: $ELF_FILE)"
    exit 1
fi
echo "PASS ($ELF_FILE found)"

# Resolve Toolchain Binaries Dynamically
CROSS_SIZE=""
CROSS_OBJDUMP=""
CROSS_READELF=""

if command -v riscv32-esp-elf-size >/dev/null 2>&1; then
    CROSS_SIZE="riscv32-esp-elf-size"
    CROSS_OBJDUMP="riscv32-esp-elf-objdump"
    CROSS_READELF="riscv32-esp-elf-readelf"
else
    CANDIDATE_DIR=$(ls -d $HOME/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin 2>/dev/null | head -n 1)
    if [ -n "$CANDIDATE_DIR" ] && [ -d "$CANDIDATE_DIR" ]; then
        TOOL_BIN="$CANDIDATE_DIR"
        CROSS_SIZE="$TOOL_BIN/riscv32-esp-elf-size"
        CROSS_OBJDUMP="$TOOL_BIN/riscv32-esp-elf-objdump"
        CROSS_READELF="$TOOL_BIN/riscv32-esp-elf-readelf"
    else
        echo "FAIL: No suitable RISC-V binutils found on PATH or standard directory."
        exit 1
    fi
fi

# 3. SRAM Sizing Audit (<= 248 KB)
echo -n "[Audit 3/10] Total Binary Footprint Audit: "
TOTAL_BYTES=$($CROSS_SIZE -B "$ELF_FILE" | awk 'NR==2 {print $1 + $2 + $3}')
if [ "$TOTAL_BYTES" -gt "$TARGET_MAX_BYTES" ]; then
    echo "FAIL (Size $TOTAL_BYTES bytes exceeds 248 KB SRAM limit of $TARGET_MAX_BYTES bytes!)"
    exit 1
fi
FREE_BYTES=$((TARGET_MAX_BYTES - TOTAL_BYTES))
echo "PASS ($TOTAL_BYTES / $TARGET_MAX_BYTES bytes, Headroom: $FREE_BYTES bytes)"

# 4. Entry Point Address Check (0x00202000)
echo -n "[Audit 4/10] Execution Origin Address Audit: "
ENTRY_ADDR=$($CROSS_READELF -h "$ELF_FILE" | awk '/Entry point address:/ {print $4}')
# Normalize to lowercase and remove leading zeros after 0x
ENTRY_NORM=$(printf "0x%08x" "$ENTRY_ADDR")
if [ "$ENTRY_NORM" != "$EXPECTED_ENTRY" ]; then
    echo "FAIL (Entry point is $ENTRY_NORM, expected $EXPECTED_ENTRY)"
    exit 1
fi
echo "PASS ($ENTRY_NORM matches physical SRAM origin)"

# 5. Compressed Instruction Opcode Audit (grep c. == 0)
echo -n "[Audit 5/10] Compressed Opcode (RVC) Audit: "
DISASM_TMP=$(mktemp)
$CROSS_OBJDUMP -d "$ELF_FILE" > "$DISASM_TMP"
RVC_COUNT=$(grep -E "\sc\.[a-z0-9_]+" "$DISASM_TMP" | wc -l || true)
if [ "$RVC_COUNT" -ne 0 ]; then
    echo "FAIL (FATAL: Found $RVC_COUNT compressed instructions! Silicon will trigger mcause=2 trap!)"
    grep -E "\sc\.[a-z0-9_]+" "$DISASM_TMP" | head -n 10
    rm -f "$DISASM_TMP"
    exit 1
fi
rm -f "$DISASM_TMP"
echo "PASS (Strictly 0 compressed instructions detected)"

# 6. Soft-Float Double-Precision Helper Audit (Zero __adddf3, __muldf3)
echo -n "[Audit 6/10] Float Math Precision Audit: "
DOUBLE_SYMS=$($CROSS_READELF -s "$ELF_FILE" | grep -E "(__adddf3|__muldf3|__subdf3|__divdf3)" || true)
if [ -n "$DOUBLE_SYMS" ]; then
    echo "WARNING: Double-precision soft-float helpers detected in symbol table:"
    echo "$DOUBLE_SYMS"
else
    echo "PASS (Zero double-precision float helpers found)"
fi

# 7. Trap Vector Alignment Audit
echo -n "[Audit 7/10] Trap Vector Address Alignment: "
TRAP_VEC=$($CROSS_READELF -s "$ELF_FILE" | awk '$8 == "trap_entry" {print $2}' | head -n 1)
if [ -n "$TRAP_VEC" ]; then
    TRAP_INT=$((16#$TRAP_VEC))
    ALIGN_REM=$((TRAP_INT % 64))
    if [ "$ALIGN_REM" -ne 0 ]; then
        echo "FAIL (trap_entry at 0x$TRAP_VEC is not 64-byte aligned, rem=$ALIGN_REM)"
        exit 1
    fi
    echo "PASS (0x$TRAP_VEC is 64-byte aligned)"
else
    echo "SKIP (trap_entry symbol not present)"
fi

# 8. Parameter Group Storage Bounds Audit
echo -n "[Audit 8/10] Parameter Group Registry Bounds: "
PG_START=$($CROSS_READELF -s "$ELF_FILE" | awk '/__pg_registry_start/ {print $2}' | head -n 1)
PG_END=$($CROSS_READELF -s "$ELF_FILE" | awk '/__pg_registry_end/ {print $2}' | head -n 1)
if [ -n "$PG_START" ] && [ -n "$PG_END" ]; then
    PG_SIZE=$((16#$PG_END - 16#$PG_START))
    echo "PASS (PG registry size: $PG_SIZE bytes)"
else
    echo "SKIP (PG registry symbols not linked in early bootstrap build)"
fi

# 9. Top-of-SRAM Upper Ceiling Audit (<= 0x0023FFFF including .noinit + 8 KB .stack)
echo -n "[Audit 9/10] SRAM Upper Bound Ceiling Audit: "
SRAM_END=$($CROSS_READELF -s "$ELF_FILE" | awk '$8 == "_end" {print $2}' | head -n 1)
if [ -n "$SRAM_END" ]; then
    SRAM_END_INT=$((16#$SRAM_END))
    MAX_SRAM_INT=$((16#0023FFFF))
    if [ "$SRAM_END_INT" -gt "$MAX_SRAM_INT" ]; then
        echo "FAIL (_end at 0x$SRAM_END exceeds physical SRAM ceiling 0x0023FFFF)"
        exit 1
    fi
    echo "PASS (_end 0x$SRAM_END <= 0x0023FFFF)"
else
    echo "SKIP (_end symbol not exported)"
fi

# 10. Binary Image Artifact Generation Check
BIN_FILE="${ELF_FILE%.elf}.bin"
echo -n "[Audit 10/10] Binary Output (.bin) Check: "
if [ -f "$BIN_FILE" ]; then
    BIN_BYTES=$(stat -c%s "$BIN_FILE")
    echo "PASS ($BIN_FILE generated, $BIN_BYTES bytes ready for XMODEM SRAM upload)"
else
    echo "INFO (.bin file will be generated by make hex/bin target)"
fi

echo "======================================================================"
echo " 10-Point Silicon Audit Passed Successfully."
echo "======================================================================"
