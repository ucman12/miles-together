"""Read bounded DAT1 config records for private research; never write back to the game."""
from __future__ import annotations
import argparse
import json
import struct
from pathlib import Path


class ConfigReader:
    def __init__(self, raw: bytes):
        self.wrapper = struct.unpack_from("<I", raw)[0]
        if self.wrapper not in (0x21A56F68, 0x7C207220):
            raise ValueError("Unsupported config wrapper")
        self.data = raw[36:]
        magic, kind, size, section_count, unknown_count = struct.unpack_from("<IIIHH", self.data)
        if magic != 0x44415431 or size != len(self.data):
            raise ValueError("Invalid DAT1 container")
        self.sections = {}
        for index in range(section_count):
            tag, offset, length = struct.unpack_from("<III", self.data, 16 + index * 12)
            if offset + length > len(self.data):
                raise ValueError("Section boundary out of range")
            self.sections[tag] = self.data[offset:offset + length]

    def string_at(self, offset: int) -> str:
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("ascii")

    def decode_section(self, tag: int, selected_data: bytes | None = None):
        data = self.sections[tag] if selected_data is None else selected_data
        cursor = 0
        anchor = 0
        def read(fmt):
            nonlocal cursor
            result = struct.unpack_from("<" + fmt, data, cursor)
            cursor += struct.calcsize("<" + fmt)
            return result[0] if len(result) == 1 else result
        def align():
            nonlocal cursor
            cursor = anchor + (cursor - anchor + 3) // 4 * 4
        def node(kind, depth):
            nonlocal cursor
            if depth > 48:
                raise ValueError("Config nesting exceeds bounded depth")
            primitives = {0:"B", 1:"H", 2:"I", 4:"b", 5:"h", 6:"i", 8:"f", 15:"?", 17:"Q"}
            if kind in primitives:
                return read(primitives[kind])
            if kind == 19:
                read("B")
                return None
            if kind == 10:
                length, crc32, crc64 = read("IIQ")
                if length > len(data) - cursor - 1:
                    raise ValueError("String exceeds section")
                value = data[cursor:cursor+length].decode("ascii")
                cursor += length + 1
                align()
                return value
            if kind == 13:
                return obj(depth+1)
            raise ValueError(f"Unknown config node type {kind}")
        def obj(depth):
            nonlocal cursor
            zero, version, count, length = read("IIII")
            if zero != 0 or version != 0x03150044 or count > 8192:
                raise ValueError("Invalid serialized-object header")
            end = cursor + length
            if end > len(data):
                raise ValueError("Object exceeds section")
            headers = [read("IHBB") for _ in range(count)]
            names = [self.string_at(read("I")) for _ in range(count)]
            result = {}
            for name, header in zip(names, headers):
                items = header[1] >> 4
                if name in result:
                    raise ValueError("Duplicate config property")
                result[name] = [node(header[3], depth) for _ in range(items)] if items > 1 else node(header[3], depth)
            align()
            if cursor > end:
                raise ValueError("Object data-length mismatch")
            cursor = end
            return result
        values = []
        while cursor < len(data):
            anchor = cursor
            values.append(obj(0))
        return values[0] if len(values) == 1 else values

    def decode(self):
        if self.wrapper == 0x7C207220:
            definitions = self.sections.get(0x135832C8, b"")
            if len(definitions) % 32:
                raise ValueError("Invalid actor component-definition stride")
            components = []
            for entry in struct.iter_unpack("<8I", definitions):
                offset, size = entry[5:7]
                if offset + size > len(self.data):
                    raise ValueError("Actor component payload is out of range")
                payload = self.data[offset:offset+size]
                fields = self.decode_section(0, payload) if size else {}
                components.append({"name": self.string_at(entry[2]), "fields": fields})
            model_offset = struct.unpack("<I", self.sections[0x32FAC8E0])[0]
            return {"type": {"wrapper": hex(self.wrapper), "kind": "actor"}, "content": {"model": self.string_at(model_offset), "components": components}}
        return {"type": self.decode_section(0x4A128222), "content": self.decode_section(0xE501186F)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asset", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    destination = args.out.resolve()
    project = Path(__file__).resolve().parents[1]
    if destination == project or project in destination.parents:
        parser.error("Decoded game content must stay outside the project")
    result = ConfigReader(args.asset.read_bytes()).decode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"type": result["type"], "keys": list(result["content"])} if args.summary else result, indent=2))
