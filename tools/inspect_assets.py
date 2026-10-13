"""Read selected asset-index entries from the player's TOC. Never modify game files."""
from __future__ import annotations

import argparse
import io
import json
import re
import struct
import zlib
import zipfile
from pathlib import Path


def path_id(path: str) -> int:
    # Format facts: normalized UTF-8 paths, reflected CRC-64/ECMA polynomial,
    # game-specific seed and final bit layout. Table is generated independently.
    text = re.sub("/+", "/", path.replace("\\", "/").lower()).strip("/")
    value = 0xC96C5795D7870F42
    polynomial = 0xC96C5795D7870F42
    for byte in text.encode("utf-8"):
        value ^= byte
        for bit in range(8):
            value = (value >> 1) ^ (polynomial if value & 1 else 0)
    return (value >> 2) | 0x8000000000000000


def sections(data: bytes) -> dict[int, bytes]:
    magic, kind, size, count, unknown_count = struct.unpack_from("<IIIHH", data)
    if magic != 0x44415431 or size != len(data):
        raise ValueError("Invalid DAT1 header or length")
    result = {}
    for index in range(count):
        tag, offset, length = struct.unpack_from("<III", data, 16 + index * 12)
        if offset + length > len(data):
            raise ValueError("Section extends past end of file")
        result[tag] = data[offset:offset + length]
    return result


def locate(toc_path: Path, paths: list[str]) -> list[dict]:
    raw = toc_path.read_bytes()
    magic, expected_length = struct.unpack_from("<II", raw)
    if magic != 0x77AF12AF:
        raise ValueError("Unsupported TOC version")
    # Some shipped TOCs omit the zlib trailer. Accept only the exact declared
    # decoded length, then validate every DAT1 section boundary.
    decoder = zlib.decompressobj()
    decoded = decoder.decompress(raw[8:]) + decoder.flush()
    if len(decoded) != expected_length:
        raise ValueError("TOC decompressed length mismatch")
    table = sections(decoded)
    ids = [row[0] for row in struct.iter_unpack("<Q", table[0x506D7B8A])]
    archives = [row[2].split(b"\0", 1)[0].decode("ascii") for row in struct.iter_unpack("<II64s", table[0x398ABFF0])]
    offsets = list(struct.iter_unpack("<II", table[0xDCD720B5]))
    sizes = list(struct.iter_unpack("<III", table[0x65BCF461]))
    wanted = {path_id(path): path for path in paths}
    matches = {key: [] for key in wanted}
    for index, key in enumerate(ids):
        if key not in wanted:
            continue
        archive_index, offset = offsets[index]
        matches[key].append({"index": index, "archive": archives[archive_index], "offset": offset, "size": sizes[index][1]})
    return [{"path": path, "id": f"{key:016x}", "present": bool(matches[key]), "entries": matches[key]} for key, path in wanted.items()]


def extract_entry(folder: Path, entry: dict) -> bytes:
    archive = folder / entry["archive"]
    start, size = entry["offset"], entry["size"]
    with archive.open("rb") as stream:
        header = stream.read(32)
        if header[:4] != b"DSAR":
            stream.seek(start)
            output = stream.read(size)
        else:
            import lz4.block
            table_end = struct.unpack_from("<I", header, 12)[0]
            table = stream.read(table_end - 32)
            output = bytearray()
            for block_start, compressed_offset, real_size, compressed_size, kind in struct.iter_unpack("<QQIIB7x", table):
                if block_start >= start + size or block_start + real_size <= start:
                    continue
                if kind != 3:
                    raise ValueError(f"Unsupported DSAR compression {kind}; no fallback bytes produced")
                stream.seek(compressed_offset)
                block = lz4.block.decompress(stream.read(compressed_size), uncompressed_size=real_size)
                low = max(start, block_start) - block_start
                high = min(start + size, block_start + real_size) - block_start
                output.extend(block[low:high])
    if len(output) != size:
        raise ValueError("Extracted asset length mismatch")
    return bytes(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("toc", type=Path)
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--hashes", type=Path, help="Official modding-tool hashes.zip for selecting asset names")
    parser.add_argument("--find", help="Regex to select paths from the public name index")
    parser.add_argument("--extract", type=Path, help="Private research folder outside this project")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.hashes:
        if not args.find:
            parser.error("--hashes requires --find")
        pattern = re.compile(args.find, re.I)
        with zipfile.ZipFile(args.hashes) as archive:
            with io.TextIOWrapper(archive.open("hashes.txt"), encoding="utf-8-sig") as stream:
                for line in stream:
                    fields = line.rstrip().split(",")
                    if len(fields) >= 2 and pattern.search(fields[1]):
                        args.paths.append(fields[1])
    if not args.paths:
        parser.error("At least one selected asset path is required")
    result = locate(args.toc, args.paths)
    if args.extract:
        project = Path(__file__).resolve().parents[1]
        destination = args.extract.resolve()
        if destination == project or project in destination.parents:
            parser.error("Game-derived research assets must stay outside this project")
        destination.mkdir(parents=True, exist_ok=True)
        for record in result:
            if record["entries"]:
                extracted = extract_entry(args.toc.parent, record["entries"][0])
                output = destination / (record["id"] + ".bin")
                output.write_bytes(extracted)
                record["private_output"] = str(output)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.quiet:
        print(json.dumps({"selected": len(result), "present": sum(row["present"] for row in result), "report": str(args.out)}))
    else:
        print(json.dumps(result, indent=2))
