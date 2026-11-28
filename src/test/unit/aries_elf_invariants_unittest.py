#!/usr/bin/env python3
"""
Test Suite: C-DAC ARIES v3.0 Built ELF / BIN / MAP Structural Invariants (Wave 2 - Suite 9)
Target Silicon: C-DAC THEJAS32 SoC (VEGA ET1031 RISC-V RV32IM @ 100 MHz, 248 KB SRAM, 0 RVC)

References & Citations:
  - src/platform/ARIES/link/aries_v3.ld
  - src/test/unit/aries/build_verify.sh
  - src/platform/ARIES/mk/ARIES.mk
  - knowledgebase/silicon_facts/ (RV32IM 0-RVC, <= 248 KB SRAM @ 0x00202000, CONFIG_IN_RAM)
"""

import glob
import os
import re
import shutil
import struct
import subprocess
import unittest


def find_repo_root() -> str:
    cur = os.path.abspath(os.path.dirname(__file__))
    for _ in range(6):
        if os.path.isdir(os.path.join(cur, "src", "platform", "ARIES")):
            return cur
        cur = os.path.dirname(cur)
    return os.path.abspath(".")


REPO_ROOT = find_repo_root()
OBJ_MAIN = os.path.join(REPO_ROOT, "obj", "main")
BIN_PATH = os.path.join(OBJ_MAIN, "betaflight_ARIESV3.bin")
ELF_PATH = os.path.join(OBJ_MAIN, "betaflight_ARIESV3.elf")
MAP_PATH = os.path.join(OBJ_MAIN, "betaflight_ARIESV3.map")


def find_riscv_tool(suffix: str):
    env_dir = os.environ.get("RISCV_TOOLCHAIN_PATH")
    candidates = [f"riscv32-esp-elf-{suffix}", f"riscv-none-elf-{suffix}", f"riscv64-unknown-elf-{suffix}"]
    if env_dir:
        for cand in candidates:
            p = os.path.join(env_dir, cand)
            if os.path.isfile(p) and os.access(p, os.X_OK):
                return p
    for cand in candidates:
        found = shutil.which(cand)
        if found:
            return found
    for pattern in [
        os.path.expanduser(f"~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-{suffix}"),
        f"/usr/local/bin/riscv32-esp-elf-{suffix}",
    ]:
        matches = sorted(glob.glob(pattern))
        if matches and os.access(matches[-1], os.X_OK):
            return matches[-1]
    return None


def parse_elf32_header_and_sections(elf_path: str):
    with open(elf_path, "rb") as f:
        data = f.read()
    e_ident = data[:16]
    if e_ident[:4] != b"\x7fELF" or e_ident[4] != 1:  # ELFCLASS32
        raise AssertionError("Not a valid 32-bit ELF file")
    (_, e_machine, _, e_entry, _, e_shoff, e_flags, _, _, _, e_shentsize, e_shnum, e_shstrndx) = struct.unpack(
        "<HHIIIIIHHHHHH", data[16:52]
    )
    sections = []
    shstr_off, shstr_size = struct.unpack("<II", data[e_shoff + e_shstrndx * e_shentsize + 16 : e_shoff + e_shstrndx * e_shentsize + 24])
    shstrtab = data[shstr_off : shstr_off + shstr_size]
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, _, _, sh_addralign, _ = struct.unpack(
            "<IIIIIIIIII", data[off : off + 40]
        )
        end = shstrtab.find(b"\x00", sh_name)
        name = shstrtab[sh_name:end].decode("ascii", errors="replace")
        sections.append({
            "name": name,
            "type": sh_type,
            "flags": sh_flags,
            "addr": sh_addr,
            "offset": sh_offset,
            "size": sh_size,
            "align": sh_addralign,
        })
    return {
        "raw": data,
        "e_machine": e_machine,
        "e_entry": e_entry,
        "e_flags": e_flags,
        "sections": {s["name"]: s for s in sections},
    }


def extract_func_ranges_from_elf(elf_info):
    secs = elf_info["sections"]
    symtab = secs.get(".symtab")
    strtab = secs.get(".strtab")
    text = secs.get(".text")
    if not (symtab and strtab and text):
        return []
    raw = elf_info["raw"]
    t_addr = text["addr"]
    t_off = text["offset"]
    t_end = t_addr + text["size"]
    ranges = []
    for off in range(symtab["offset"], symtab["offset"] + symtab["size"], 16):
        st_name, st_value, st_size, st_info, _, _ = struct.unpack("<IIIBBH", raw[off : off + 16])
        if (st_info & 0xF) == 2 and st_size >= 4 and st_value >= t_addr and (st_value + st_size) <= t_end:
            ranges.append((st_value, st_value + st_size, t_off + (st_value - t_addr)))
    return ranges


def parse_map_symbols(map_path: str):
    symbols = {}
    with open(map_path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()
    for match in re.finditer(r"^\s*0x([0-9a-fA-F]+)\s+([_a-zA-Z][_a-zA-Z0-9]*)\s*=", content, re.MULTILINE):
        symbols[match.group(2)] = int(match.group(1), 16)
    return symbols, content


class AriesElfInvariantsTest(unittest.TestCase):
    def setUp(self):
        if not (os.path.isfile(ELF_PATH) and os.path.isfile(BIN_PATH) and os.path.isfile(MAP_PATH)):
            self.skipTest("obj/main/betaflight_ARIESV3.{elf,bin,map} not built in workspace")

    def test_01_footprint_sram_origin_and_bootloader_ceiling(self):
        bin_size = os.path.getsize(BIN_PATH)
        self.assertGreater(bin_size, 0)
        self.assertLessEqual(bin_size, 247808, f"Binary {bin_size} B exceeds 242 KB safe limit")

        elf_info = parse_elf32_header_and_sections(ELF_PATH)
        self.assertEqual(elf_info["e_machine"], 243, "ELF e_machine must be EM_RISCV (243)")
        self.assertEqual(elf_info["e_entry"], 0x00202000, f"Entry 0x{elf_info['e_entry']:08x} != 0x00202000")

        symbols, _ = parse_map_symbols(MAP_PATH)
        for req in ("_sdata", "_edata", "_sbss", "_ebss", "_snoinit", "_enoinit", "_sstack", "_estack", "_end", "_stack_size"):
            self.assertIn(req, symbols, f"Missing linker symbol {req} in map")
        self.assertLessEqual(symbols["_end"], 0x0023E800, "_end encroaches on ROM bootloader RAM")
        self.assertGreaterEqual(symbols["_stack_size"], 0x2000, "Stack budget fell below 8 KB")
        self.assertEqual(symbols["_estack"] & 0xF, 0, "_estack must be 16-byte aligned")

    def test_02_zero_rvc_compressed_opcodes_and_rv32i_multilib(self):
        elf_info = parse_elf32_header_and_sections(ELF_PATH)
        # EF_RISCV_RVC == 0x1, EF_RISCV_FLOAT_ABI == 0x6 (must both be 0 -> RV32IM soft-float)
        self.assertEqual(elf_info["e_flags"] & 0x7, 0, f"Forbidden RVC or HW-FP ELF flags: 0x{elf_info['e_flags']:x}")

        _, map_text = parse_map_symbols(MAP_PATH)
        self.assertIn("rv32i_zicsr_zifencei/ilp32", map_text)
        self.assertNotIn("rv32iac", map_text)
        self.assertNotIn("rv32imac", map_text)
        self.assertNotIn("ilp32f", map_text)

        # Verify .riscv.attributes section carries RV32IM without 'C' or 'F'/'D' extension
        attrs = elf_info["sections"].get(".riscv.attributes")
        self.assertIsNotNone(attrs, "Missing .riscv.attributes section")
        attr_bytes = elf_info["raw"][attrs["offset"] : attrs["offset"] + attrs["size"]]
        self.assertIn(b"rv32i", attr_bytes)
        self.assertNotIn(b"_c", attr_bytes)

        # Direct ELF machine-code scan across all STT_FUNC functions in .text:
        # every RV32IM 32-bit instruction has bits [1:0] == 0b11; 16-bit RVC has bits [1:0] != 0b11.
        func_ranges = extract_func_ranges_from_elf(elf_info)
        self.assertGreater(len(func_ranges), 100, "Expected >100 functions in firmware ELF .symtab")
        rvc_violations = []
        raw = elf_info["raw"]
        for v_start, v_end, f_start in func_ranges:
            cursor = 0
            length = v_end - v_start
            while cursor + 2 <= length:
                hw = struct.unpack("<H", raw[f_start + cursor : f_start + cursor + 2])[0]
                if (hw & 0x3) != 0x3:
                    rvc_violations.append((hex(v_start + cursor), hex(hw)))
                    break
                cursor += 4
        self.assertEqual(len(rvc_violations), 0, f"Detected 16-bit RVC instructions: {rvc_violations[:3]}")

        # Also run external objdump -d if toolchain is accessible in environment
        objdump = find_riscv_tool("objdump")
        if objdump:
            proc = subprocess.run([objdump, "-d", ELF_PATH], capture_output=True, text=True, check=True)
            c_insns = re.findall(r"^\s*[0-9a-f]+:\s+[0-9a-f]{4}\s+.*$", proc.stdout, re.MULTILINE)
            self.assertEqual(len(c_insns), 0, f"Detected {len(c_insns)} forbidden 16-bit RVC instructions: {c_insns[:3]}")
            hw_fp = re.findall(r"^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(flw|fsw|fadd\.s|fmul\.s|fdiv\.s)\b", proc.stdout, re.MULTILINE)
            self.assertEqual(len(hw_fp), 0, f"Detected hardware FPU instructions on soft-float core: {hw_fp[:3]}")

    def test_03_pg_registry_and_noinit_section_ordering(self):
        elf_info = parse_elf32_header_and_sections(ELF_PATH)
        secs = elf_info["sections"]
        for sname in (".text", ".rodata", ".pg_registry", ".pg_resetdata", ".data", ".bss", ".noinit", ".stack"):
            self.assertIn(sname, secs, f"Missing ELF section {sname}")

        self.assertEqual(secs[".pg_registry"]["addr"] & 3, 0)
        self.assertEqual(secs[".pg_registry"]["size"] % 4, 0)
        self.assertGreater(secs[".pg_registry"]["size"], 0)
        self.assertEqual(secs[".text"]["addr"], 0x00202000)

        symbols, _ = parse_map_symbols(MAP_PATH)
        self.assertEqual(symbols["_sidata"], symbols["_sdata"], "In-SRAM mode requires LMA == VMA for .data")
        self.assertLessEqual(symbols["_ebss"], symbols["_snoinit"])
        self.assertLessEqual(symbols["_enoinit"], symbols["_sstack"])
        self.assertGreaterEqual(secs[".bss"]["size"], 8192, "CONFIG_IN_RAM 8 KB EEPROM missing from .bss")

    def test_04_key_aries_symbols_present_and_xip_unreferenced(self):
        _, map_text = parse_map_symbols(MAP_PATH)
        for sym in ("handle_trap", "ariesPwmMotorWrite" if False else "motorPwmDevInit", "uartPollTx", "targetPreInit"):
            self.assertIn(sym, map_text, f"Expected symbol {sym} missing from ELF map")
        # No XIP flash pointer region (0x20000000) placed in any section load address
        elf_info = parse_elf32_header_and_sections(ELF_PATH)
        for sname, s in elf_info["sections"].items():
            if s["addr"] != 0:
                self.assertGreaterEqual(s["addr"], 0x00202000, f"Section {sname} below SRAM origin")
                self.assertLessEqual(s["addr"], 0x00240000, f"Section {sname} above SRAM limit (possible XIP leak)")


if __name__ == "__main__":
    unittest.main(verbosity=2)
