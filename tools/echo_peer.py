"""Developer test peer: joins a hosting game and plays the host's own poses back to it.

The host then shows a second Miles that repeats everything the player does, a little
later and a little to the side, through the real network code. It is a test aid for one
PC; it is not a player and it is not part of the release.

    python tools/echo_peer.py 127.0.0.1:47584 --delay 0.4 --offset 2 0 0

With --listen it plays the host instead: the game joins it (set join=127.0.0.1:<port> in
melty.properties before starting the game). --lighting sends that lighting mode (0-6) as the
peer's world state, so a joining game can be seen matching the host's time of day and weather.

Shared enemies on one PC: --record FILE, while joined to a hosting game, saves the enemies the
host shares. --replay FILE with --listen plays them back to a joining game, moved so they stand
around that game's Miles as they stood around the host's, again and again. Together they let the
joiner's side of enemy sharing be tested without a second PC:

    python tools/echo_peer.py 127.0.0.1:47584 --record enemies.json --seconds 60
    python tools/echo_peer.py --listen 47600 --replay enemies.json
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
    parser.add_argument("address", help="host:port from the host's melty.log, or with --listen the port to host on")
    parser.add_argument("--listen", action="store_true", help="host: wait for the game to join on this port")
    parser.add_argument("--lighting", type=int, help="lighting mode (0-6) to announce as this peer's world state")
    parser.add_argument("--delay", type=float, default=0.3, help="seconds between a pose arriving and going back")
    parser.add_argument("--offset", type=float, nargs=3, default=[2.0, 0.0, 0.0], metavar=("X", "Y", "Z"), help="metres added to the position")
    parser.add_argument("--suit", nargs="*", help="skin config ids (hex) to wear in place of the host's suit")
    parser.add_argument("--seconds", type=float, default=0, help="stop after this long; 0 runs until the host closes")
    parser.add_argument("--record", help="save the enemies the host shares to this file")
    parser.add_argument("--replay", help="with --listen: send the enemies saved in this file, around the game's Miles")
    args = parser.parse_args()

    if args.listen:
        listener = socket.create_server(("127.0.0.1", int(args.address.rpartition(":")[2])))
        print("waiting for the game to join on", listener.getsockname())
        connection, _ = listener.accept()
        listener.close()
    else:
        host, _, port = args.address.rpartition(":")
        connection = socket.create_connection((host, int(port)), timeout=5)
    connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    connection.settimeout(0.01)
    connection.sendall(frame("hello", struct.pack("<IHI", MAGIC, PROTOCOL, BUILD)))
    if args.lighting is not None:
        connection.sendall(frame("world", struct.pack("<B", args.lighting)))

    recorded: list[list] = []  # [seconds since the first enemy, message, body hex]
    origin = None  # the host's position when recording started
    replay = json.loads(Path(args.replay).read_text(encoding="utf-8")) if args.replay else None
    replay_at = 0  # next frame to send
    replay_start = None  # monotonic time the current loop started
    replay_loop = 0
    replay_ids: set[int] = set()
    replay_offset = None
    joiner_clock = None  # (their pose time_ms, monotonic time it arrived)

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
            if kind in ("enemy", "enemy_gone") and args.record:
                if not recorded:
                    record_start = now
                recorded.append([round(now - record_start, 3), kind, body.hex(), origin])
            if kind == "pose":
                pose_bytes += len(body)
                x, y, z = struct.unpack_from("<3f", body, 4)
                joiner_clock = (struct.unpack_from("<I", body, 0)[0], now)
                if args.record and origin is None:
                    origin = [x, y, z]
                if replay and replay_offset is None:
                    first = next(r[3] for r in replay["frames"] if r[3])
                    replay_offset = [x - first[0], y - first[1], z - first[2]]
                    replay_start = now
                    print("replaying", len(replay["frames"]), "enemy messages around", [round(v) for v in (x, y, z)])
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
            elif kind == "world":
                print("game's lighting mode:", body[0])
            elif kind == "ping":
                connection.sendall(frame("ping", body))
        while replay and replay_start is not None and joiner_clock:
            frames = replay["frames"]
            if replay_at >= len(frames):  # loop: remove this round's enemies, start again with new ids
                for enemy_id in replay_ids:
                    connection.sendall(frame("enemy_gone", struct.pack("<I", enemy_id)))
                replay_ids.clear()
                replay_at, replay_loop, replay_start = 0, replay_loop + 1, now
                continue
            at, kind, body_hex, _ = frames[replay_at]
            if replay_start + at > now:
                break
            replay_at += 1
            body = bytearray(bytes.fromhex(body_hex))
            enemy_id = struct.unpack_from("<I", body, 0)[0] ^ ((replay_loop & 0xFF) << 24)
            struct.pack_into("<I", body, 0, enemy_id)
            if kind == "enemy":
                their_ms = (joiner_clock[0] + int((now - joiner_clock[1]) * 1000)) & 0xFFFFFFFF
                struct.pack_into("<I", body, 13, their_ms)
                x, y, z = struct.unpack_from("<3f", body, 17)
                struct.pack_into("<3f", body, 17, x + replay_offset[0], y + replay_offset[1], z + replay_offset[2])
                replay_ids.add(enemy_id)
            else:
                replay_ids.discard(enemy_id)
            connection.sendall(frame(kind, bytes(body)))
        while pending and pending[0][0] <= now:
            connection.sendall(pending.popleft()[1])
        if now - last_report >= 5:
            seconds = now - last_report
            print(f"{dict(counts)}  pose traffic {pose_bytes / seconds / 1024:.1f} KB/s")
            counts.clear()
            pose_bytes = 0
            last_report = now
    connection.close()
    if args.record:
        Path(args.record).write_text(json.dumps({"frames": recorded}), encoding="utf-8")
        print("saved", len(recorded), "enemy messages to", args.record)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
