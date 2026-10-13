"""Find bounded RIP-relative references to selected game strings for native research."""
import argparse
import bisect
import json
import re
import struct
from pathlib import Path

import capstone
import pefile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("exe", type=Path)
parser.add_argument("--strings", type=Path, required=True)
parser.add_argument("--find", required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
pattern = re.compile(args.find)
selected = {int(row["rva"], 16): row["value"] for row in json.loads(args.strings.read_text()) if pattern.search(row["value"])}
pe = pefile.PE(str(args.exe), fast_load=True)
directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
data = pe.get_data(directory.VirtualAddress, directory.Size)
spans = sorted((begin, end) for begin, end, unwind in struct.iter_unpack("<III", data) if begin and begin < end)
begins = [item[0] for item in spans]
disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
results = []
for section in pe.sections:
    if not section.Characteristics & 0x20000000:
        continue
    code = section.get_data()
    # Seven-byte 64-bit lea/mov with RIP-relative addressing. These are candidates,
    # checked by the disassembler and later by the surrounding function.
    for match in re.finditer(rb"[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d][\x00-\xff]{4}", code):
        rva = section.VirtualAddress + match.start()
        target = rva + 7 + struct.unpack_from("<i", match[0], 3)[0]
        if target not in selected:
            continue
        instruction = next(disassembler.disasm(match[0], rva), None)
        if not instruction or instruction.size != 7:
            continue
        index = bisect.bisect_right(begins, rva) - 1
        start, end = spans[index] if index >= 0 else (0, 0)
        results.append({"string_rva": hex(target), "value": selected[target], "reference": hex(rva), "function": hex(start) if start <= rva < end else None})
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
print(json.dumps(results, indent=2))
