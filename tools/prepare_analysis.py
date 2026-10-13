"""Prepare only selected RTTI methods and exception-table bounds for native research."""
import argparse
import json
import re
import struct
from pathlib import Path

import pefile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("exe", type=Path)
parser.add_argument("--tables", type=Path, required=True)
parser.add_argument("--find", required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
pe = pefile.PE(str(args.exe), fast_load=True)
exception_directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
data = pe.get_data(exception_directory.VirtualAddress, exception_directory.Size)
entries = []
for offset in range(0, len(data) - 11, 12):
    begin, end, unwind = struct.unpack_from("<3I", data, offset)
    if begin and begin < end:
        entries.append((begin, end, unwind))

def root_begin(entry, depth=0):
    begin, end, unwind = entry
    if depth > 16:
        raise ValueError("Unwind chain exceeds bounded recursion")
    header = pe.get_data(unwind & ~3, 4)
    if len(header) != 4:
        raise ValueError("Invalid unwind metadata")
    flags = header[0] >> 3
    if flags & 4:
        chained_offset = 4 + ((header[2] + 1) & ~1) * 2
        parent = struct.unpack("<III", pe.get_data((unwind & ~3) + chained_offset, 12))
        return root_begin(parent, depth + 1)
    return begin

# PE unwind metadata splits some functions into chained fragments; a prologue
# fragment alone is not the function body. Preserve the complete root extent.
extents = {}
for entry in entries:
    root = root_begin(entry)
    extents[root] = max(extents.get(root, 0), entry[1])
spans = [f"{begin:#x} {end:#x}" for begin, end in sorted(extents.items())]
(args.out / "function-spans.tsv").write_text("\n".join(spans) + "\n", encoding="utf-8")
pattern = re.compile(args.find)
seeds = []
for record in json.loads(args.tables.read_text()):
    if not pattern.search(record["name"]):
        continue
    name = re.sub("[^a-zA-Z0-9_]", "_", record["name"])
    for table_index, table in enumerate(record["vtables"]):
        for slot, method in enumerate(table["methods"]):
            seeds.append(f"{method} {name}_{table_index}_{slot}")
(args.out / "hero-seeds.tsv").write_text("\n".join(seeds) + "\n", encoding="utf-8")
print(f"{len(seeds)} selected method seeds; {len(spans)} native function bounds")
