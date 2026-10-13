"""Bounded native-code research on the player's executable; output stays outside this project."""
from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

import capstone
import pefile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--find", help="Regex for class names and relevant diagnostic strings")
    parser.add_argument("--rva", type=lambda s: int(s, 0))
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--limit", type=int, default=80)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    pe = pefile.PE(str(args.exe), fast_load=True)
    data = args.exe.read_bytes()
    matches = []
    if args.find:
        pattern = re.compile(args.find, re.I)
        for section in pe.sections:
            if section.Name.rstrip(b"\0") not in (b".rdata", b".data"):
                continue
            start = section.PointerToRawData
            end = start + section.SizeOfRawData
            for match in re.finditer(rb"[\x20-\x7e]{5,}\x00", data[start:end]):
                value = match[0][:-1].decode("ascii")
                if not pattern.search(value):
                    continue
                offset = start + match.start()
                matches.append({"rva": hex(pe.get_rva_from_offset(offset)), "value": value[:600]})
        print(f"{len(matches)} matching strings; first {args.limit}:")
        for item in matches[:args.limit]:
            print(f"{item['rva']} {item['value']}")
    if args.rva is not None:
        disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        for instruction in disassembler.disasm(pe.get_data(args.rva, args.size), args.rva):
            print(f"{instruction.address:x}: {instruction.bytes.hex()} {instruction.mnemonic} {instruction.op_str}")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(matches, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
