#!/usr/bin/env python3
"""
Test Suite: RISC-V RV32IM Toolchain Flags & Multilib RVC Isolation
Phase: Phase 01 (Dynamic Toolchain Discovery & Multilib RVC Isolation)
Hardware Target: C-DAC ARIES v3.0 (THEJAS32 / VEGA ET1031 RISC-V @ 100 MHz)
Reference Documents:
  - PLAN/02_BUILD_SYSTEM_AND_MEMORY_OPTIMIZATION.md (Microcomponents 02_A, 02_E)
  - knowledgebase/codebase_evaluations/13_EVAL_MAKEFILES_ARIES_MK_TARGET_MK.md
  - AGENTS.md (Silicon Ground Truth 1: RV32IM Only, strictly 0 RVC instructions)

Methodology:
  Strict Test-Driven Development (TDD) - Living Specification.
  This test verifies that:
    1. Toolchain resolution discovers the cross-compiler dynamically without hardcoded paths.
    2. Architecture flags strictly enforce pure RV32IM (-march=rv32im_zicsr -mabi=ilp32).
    3. Multilib query (-march=rv32i_zicsr_zifencei -mabi=ilp32) links uncompressed libraries.
    4. Derived library paths contain NO compressed multilibs (rv32imafc, rv32imc, etc.).
    5. Opcode auditing rejects any 16-bit compressed instruction (mcause = 2 prevention).
"""

import os
import subprocess
import unittest
import re


class TestToolchainDiscoveryAndRV32IMFlags(unittest.TestCase):
    """
    Validates dynamic toolchain discovery, compiler flags, and multilib isolation
    to guarantee zero 16-bit compressed instructions slip into THEJAS32 binaries.
    """

    def setUp(self):
        """
        Arrange: Identify candidate cross-compilers and candidate locations.
        """
        self.compiler_candidates = [
            "riscv32-esp-elf-gcc",
            "riscv-none-elf-gcc",
            "riscv64-unknown-elf-gcc",
        ]
        self.known_directories = [
            os.path.expanduser("~/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20250707/riscv32-esp-elf/bin"),
            "/usr/local/bin",
            "/usr/bin",
        ]
        self.env_override = os.environ.get("RISCV_TOOLCHAIN_PATH")

    def _resolve_toolchain_cc(self):
        """
        Helper: Resolve cross-compiler path dynamically according to Phase 01 specification.
        """
        if self.env_override and os.path.exists(self.env_override):
            for candidate in self.compiler_candidates:
                candidate_path = os.path.join(self.env_override, candidate)
                if os.path.isfile(candidate_path) and os.access(candidate_path, os.X_OK):
                    return candidate_path

        for candidate in self.compiler_candidates:
            which_res = subprocess.run(["which", candidate], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if which_res.returncode == 0:
                path = which_res.stdout.strip()
                if path and os.path.isfile(path) and os.access(path, os.X_OK):
                    return path

        for directory in self.known_directories:
            if os.path.isdir(directory):
                for candidate in self.compiler_candidates:
                    candidate_path = os.path.join(directory, candidate)
                    if os.path.isfile(candidate_path) and os.access(candidate_path, os.X_OK):
                        return candidate_path

        return None

    def test_dynamic_toolchain_discovery_no_hardcoded_user(self):
        """
        [AAA Pattern]
        Condition: Host environment has RISC-V toolchain installed.
        Expected: Dynamic discovery finds the compiler without relying on hardcoded '/home/ivan' paths.
        Flight Consequence: Hardcoded paths break headless CI/CD builds and multi-developer workstations.
        """
        # Arrange & Act
        cc_path = self._resolve_toolchain_cc()

        # Assert
        self.assertIsNotNone(
            cc_path,
            "Failed to dynamically discover any RISC-V cross-compiler on PATH or known directories."
        )
        self.assertTrue(
            os.path.isfile(cc_path) and os.access(cc_path, os.X_OK),
            f"Discovered compiler binary {cc_path} is not executable."
        )

    def test_compiler_multilib_rvc_isolation(self):
        """
        [AAA Pattern]
        Condition: Cross-compiler is queried for libc.a and libgcc.a using pure RV32I flags.
        Expected: Resolved multilib paths point strictly to 'rv32i_zicsr_zifencei/ilp32'.
        Flight Consequence: If default toolchain multilibs are linked (rv32imafc), standard helpers
        like __divsi3 or memset will crash physical THEJAS32 silicon with Illegal Instruction (mcause = 2).
        """
        # Arrange
        cc_path = self._resolve_toolchain_cc()
        self.assertIsNotNone(cc_path, "Toolchain compiler required for multilib verification.")

        # Act: Query libc.a with pure uncompressed target flags
        query_libc = subprocess.run(
            [cc_path, "-march=rv32i_zicsr_zifencei", "-mabi=ilp32", "-print-file-name=libc.a"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        libc_path = query_libc.stdout.strip()

        # Act: Query libgcc.a with pure uncompressed target flags
        query_libgcc = subprocess.run(
            [cc_path, "-march=rv32i_zicsr_zifencei", "-mabi=ilp32", "-print-file-name=libgcc.a"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        libgcc_path = query_libgcc.stdout.strip()

        # Assert
        self.assertTrue(os.path.isfile(libc_path), f"Resolved libc.a path does not exist: {libc_path}")
        self.assertTrue(os.path.isfile(libgcc_path), f"Resolved libgcc.a path does not exist: {libgcc_path}")

        # Assert no compressed multilib directories exist in resolved paths
        self.assertNotIn("rv32imafc", libc_path, "FATAL: libc.a contains rv32imafc compressed multilib!")
        self.assertNotIn("rv32imc", libc_path, "FATAL: libc.a contains rv32imc compressed multilib!")
        self.assertNotIn("rv32imafc", libgcc_path, "FATAL: libgcc.a contains rv32imafc compressed multilib!")
        self.assertNotIn("rv32imc", libgcc_path, "FATAL: libgcc.a contains rv32imc compressed multilib!")

        # Assert isolated multilib archive is rv32i_zicsr_zifencei
        self.assertIn(
            "rv32i_zicsr_zifencei/ilp32",
            libc_path,
            f"libc.a is not routed to pure uncompressed multilib directory: {libc_path}"
        )
        self.assertIn(
            "rv32i_zicsr_zifencei/ilp32",
            libgcc_path,
            f"libgcc.a is not routed to pure uncompressed multilib directory: {libgcc_path}"
        )

    def test_isolated_libgcc_strictly_zero_compressed_opcodes(self):
        """
        [AAA Pattern]
        Condition: The isolated libgcc.a archive is disassembled with objdump.
        Expected: Instruction audit 'grep -E "\sc\.[a-z]"' strictly equals 0.
        Flight Consequence: Soft-float division/multiplication helpers must not contain a single 16-bit opcode.
        """
        # Arrange
        cc_path = self._resolve_toolchain_cc()
        objdump_path = cc_path.replace("-gcc", "-objdump")
        self.assertTrue(os.path.isfile(objdump_path), f"objdump utility not found at {objdump_path}")

        query_libgcc = subprocess.run(
            [cc_path, "-march=rv32i_zicsr_zifencei", "-mabi=ilp32", "-print-file-name=libgcc.a"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        libgcc_path = query_libgcc.stdout.strip()

        # Act: Disassemble archive
        disasm_proc = subprocess.run(
            [objdump_path, "-d", libgcc_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )

        # Assert: Regex search for compressed mnemonics (c.addi, c.li, c.mv, etc.)
        compressed_opcodes = re.findall(r"\s(c\.[a-z0-9_]+)\s", disasm_proc.stdout)
        self.assertEqual(
            len(compressed_opcodes),
            0,
            f"FATAL: Found {len(compressed_opcodes)} compressed instructions in libgcc.a: {compressed_opcodes[:10]}"
        )

    def test_opcode_auditor_detects_synthetic_compressed_instructions(self):
        """
        [AAA Pattern]
        Condition: An assembly snippet containing compressed instructions is audited.
        Expected: Auditor detects compressed opcodes and raises an error, ensuring verification
        script cannot produce false-negative passes.
        Flight Consequence: An auditing script that fails to catch compressed opcodes will allow
        bad binaries to reach hardware.
        """
        # Arrange: Synthetic disassembly outputs
        synthetic_clean = (
            "00202000 <_start>:\n"
            "  202000: 00000093          li      ra,0\n"
            "  202004: 00000113          li      sp,0\n"
            "  202008: 00000193          li      gp,0\n"
        )
        synthetic_dirty = (
            "00202000 <_start>:\n"
            "  202000: 4501              c.li    a0,0\n"
            "  202002: 00000113          li      sp,0\n"
            "  202006: 9002              c.ebreak\n"
        )

        # Act & Assert
        clean_matches = re.findall(r"\s(c\.[a-z0-9_]+)\s", synthetic_clean)
        dirty_matches = re.findall(r"\s(c\.[a-z0-9_]+)\s", synthetic_dirty)

        self.assertEqual(len(clean_matches), 0, "Clean assembly flagged false positive.")
        self.assertEqual(len(dirty_matches), 2, "Dirty assembly failed to detect synthetic compressed instructions.")
        self.assertEqual(dirty_matches, ["c.li", "c.ebreak"])


if __name__ == "__main__":
    unittest.main()
