"""Locate selected MSVC RTTI vtables without disassembling unrelated game code."""
from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

import pefile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--find", required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    pe = pefile.PE(str(args.exe), fast_load=True)
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    text_ranges = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Characteristics & 0x20000000]
    rdata_section = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".rdata")
    rdata_start = rdata_section.VirtualAddress
    rdata = image[rdata_start:rdata_start + rdata_section.Misc_VirtualSize]
    pattern = re.compile(args.find, re.I)
    results = []
    for match in re.finditer(rb"\.\?A[UV][\x20-\x7e]+?@@\x00", image):
        name = match[0][:-1].decode("ascii")
        if not pattern.search(name):
            continue
        descriptor = match.start() - 16
        locators = []
        needle = struct.pack("<I", descriptor)
        pos = 0
        while True:
            found = rdata.find(needle, pos)
            if found < 0:
                break
            pos = found + 1
            if found < 12:
                continue
            col = rdata_start + found - 12
            signature, offset, cd_offset, type_rva, hierarchy_rva, self_rva = struct.unpack_from("<6I", image, col)
            if signature == 1 and self_rva == col:
                locators.append({"rva": hex(col), "offset": offset})
        tables = []
        for col in locators:
            needle = struct.pack("<Q", base + int(col["rva"], 16))
            pos = 0
            while True:
                found = rdata.find(needle, pos)
                if found < 0:
                    break
                pos = found + 1
                table_rva = rdata_start + found + 8
                methods = []
                for slot in range(60):
                    value = struct.unpack_from("<Q", image, table_rva + slot * 8)[0] - base
                    if not any(start <= value < end for start, end in text_ranges):
                        break
                    methods.append(hex(value))
                if methods:
                    tables.append({"rva": hex(table_rva), "offset": col["offset"], "methods": methods})
        results.append({"name": name, "descriptor": hex(descriptor), "vtables": tables})
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
