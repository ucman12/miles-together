"""Developer test peer: joins a hosting game and plays the host's own poses back to it.

The host then shows a second Miles that repeats everything the player does, a little
later and a little to the side, through the real network code. It is a test aid for one
PC; it is not a player and it is not part of the release.

    python tools/echo_peer.py 127.0.0.1:47584 --delay 0.4 --offset 2 0 0
"""
from __future__ import annotations

import argparse
import collections
import json
import socket
import struct
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sheet(name: str) -> dict:
    rows = json.loads((ROOT / "sheets" / f"{name}.json").read_text(encoding="utf-8"))["rows"]
    return {row["id"]: row for row in rows}


MESSAGES = {row["code"]: name for name, row in sheet("net_messages").items()}
CODES = {name: code for code, name in MESSAGES.items()}
PROTOCOL = sheet("settings")["protocol_version"]["value"]
BUILD = int(sheet("compatibility")["steam_current"]["pe_timestamp"], 16)
MAGIC = 0x3147544D


def frame(kind: str, body: bytes = b"") -> bytes:
    return struct.pack("<HB", len(body) + 1, CODES[kind]) + body


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address", help="host:port from the host's melty.log")
    parser.add_argument("--delay", type=float, default=0.3, help="seconds between a pose arriving and going back")
    parser.add_argument("--offset", type=float, nargs=3, default=[2.0, 0.0, 0.0], metavar=("X", "Y", "Z"), help="metres added to the position")
    parser.add_argument("--suit", nargs="*", help="skin config ids (hex) to wear in place of the host's suit")
    parser.add_argument("--seconds", type=float, default=0, help="stop after this long; 0 runs until the host closes")
    args = parser.parse_args()

    host, _, port = args.address.rpartition(":")
    connection = socket.create_connection((host, int(port)), timeout=5)
    connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    connection.settimeout(0.01)
    connection.sendall(frame("hello", struct.pack("<IHI", MAGIC, PROTOCOL, BUILD)))

    pending: collections.deque[tuple[float, bytes]] = collections.deque()
    counts: collections.Counter[str] = collections.Counter()
    buffer = b""
    started = last_report = time.monotonic()
    pose_bytes = 0
    while True:
        now = time.monotonic()
        if args.seconds and now - started > args.seconds:
            break
        try:
            data = connection.recv(65536)
            if not data:
                print("host closed the connection")
                break
            buffer += data
        except socket.timeout:
            pass
        while len(buffer) >= 3:
            length, code = struct.unpack_from("<HB", buffer)
            if len(buffer) < 2 + length:
                break
            body, buffer = buffer[3 : 2 + length], buffer[2 + length :]
            kind = MESSAGES.get(code, f"unknown-{code}")
            counts[kind] += 1
            if kind == "pose":
                pose_bytes += len(body)
                x, y, z = struct.unpack_from("<3f", body, 4)
                moved = struct.pack("<3f", x + args.offset[0], y + args.offset[1], z + args.offset[2])
                pending.append((now + args.delay, frame("pose", body[:4] + moved + body[16:])))
            elif kind == "suit":
                if args.suit:
                    ids = [int(value, 16) for value in args.suit]
                    body = struct.pack("<B", len(ids)) + b"".join(struct.pack("<Q", value) for value in ids)
                print("host suit:", [hex(value) for value in struct.unpack_from(f"<{body[0]}Q", body, 1)] if not args.suit else "replaced")
                pending.append((now + args.delay, frame("suit", body)))
            elif kind == "away":
                pending.append((now + args.delay, frame("away", body)))
            elif kind == "ping":
                connection.sendall(frame("ping", body))
        while pending and pending[0][0] <= now:
            connection.sendall(pending.popleft()[1])
        if now - last_report >= 5:
            seconds = now - last_report
            print(f"{dict(counts)}  pose traffic {pose_bytes / seconds / 1024:.1f} KB/s")
            counts.clear()
            pose_bytes = 0
            last_report = now
    connection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
