"""Build Miles Together: preflight the sheets, generate code from them, compile, test, package.

Needs llvm-mingw (https://github.com/mstorsjo/llvm-mingw). Point MILESTOGETHER_LLVM_MINGW at its
folder, or have x86_64-w64-mingw32-clang++ on PATH.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
SOURCES = ["dllmain.cpp", "game.cpp", "codec.cpp", "net.cpp", "melty.cpp", "proxy.cpp"]
MINHOOK = ["src/buffer.c", "src/hook.c", "src/trampoline.c", "src/hde/hde64.c"]
CXXFLAGS = ["-std=c++20", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter", "-fms-extensions", "-fno-rtti", "-DUNICODE", "-D_UNICODE", "-D_WIN32_WINNT=0x0A00"]


def compiler(name: str) -> str:
    home = os.environ.get("MILESTOGETHER_LLVM_MINGW")
    if home:
        candidate = Path(home) / "bin" / f"x86_64-w64-mingw32-{name}.exe"
        if candidate.is_file():
            return str(candidate)
    found = shutil.which(f"x86_64-w64-mingw32-{name}")
    if not found:
        raise SystemExit("llvm-mingw not found: set MILESTOGETHER_LLVM_MINGW to its folder")
    return found


def run(command: list[str]) -> None:
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode != 0:
        raise SystemExit(f"FAILED: {' '.join(str(part) for part in command[:3])} ...")


def design(key: str):
    rows = json.loads((ROOT / "sheets" / "design.json").read_text(encoding="utf-8"))["rows"]
    return next(row["value"] for row in rows if row["id"] == key)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stage", choices=["build", "release"], default="build", help="release also requires every system's verification to be 'passed'")
    args = parser.parse_args()

    run([sys.executable, "tools/preflight.py", "--stage", args.stage, "--report", "reports/preflight.json"])
    run([sys.executable, "tools/gen_sheets.py"])

    BUILD.mkdir(exist_ok=True)
    cxx, cc = compiler("clang++"), compiler("clang")
    objects = []
    for source in SOURCES:
        obj = BUILD / (Path(source).stem + ".o")
        run([cxx, *CXXFLAGS, "-Isrc", "-Ithird_party/minhook/include", "-c", f"src/{source}", "-o", str(obj)])
        objects.append(str(obj))
    for source in MINHOOK:
        obj = BUILD / ("minhook_" + Path(source).stem + ".o")
        run([cc, "-O2", "-w", "-Ithird_party/minhook/include", "-c", f"third_party/minhook/{source}", "-o", str(obj)])
        objects.append(str(obj))

    # The codec is plain maths: test it on this PC before it goes anywhere near the game.
    test_exe = BUILD / "codec_test.exe"
    run([cxx, *CXXFLAGS, "-static", "-Isrc", "tests/codec_test.cpp", "src/codec.cpp", "-o", str(test_exe)])
    run([str(test_exe)])

    dll = BUILD / design("loader")
    run([cxx, "-shared", "-static", "-s", "-o", str(dll), *objects, "src/proxy.def", "-lws2_32", "-Wl,--no-insert-timestamp"])

    version = design("version")
    package = BUILD / "package"
    shutil.rmtree(package, ignore_errors=True)
    (package / "MilesTogether").mkdir(parents=True)
    shutil.copy2(dll, package / dll.name)
    shutil.copy2(ROOT / "packaging" / "README.txt", package / "MilesTogether" / "README.txt")
    shutil.copy2(ROOT / "LICENSE", package / "MilesTogether" / "LICENSE.txt")
    shutil.copy2(ROOT / "THIRD-PARTY-NOTICES.md", package / "MilesTogether" / "THIRD-PARTY-NOTICES.txt")
    DIST.mkdir(exist_ok=True)
    archive = DIST / f"MilesTogether-{version}.zip"
    entries = []
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                name = path.relative_to(package).as_posix()
                info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                bundle.writestr(info, path.read_bytes())
                entries.append({"path": name, "size": path.stat().st_size})
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (DIST / "package.json").write_text(json.dumps({"fileName": archive.name, "size": archive.stat().st_size, "sha256": digest, "stage": args.stage, "entries": entries}, indent=2) + "\n", encoding="utf-8")
    print(f"built {archive.relative_to(ROOT)} ({archive.stat().st_size} bytes, sha256 {digest}) at stage {args.stage}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
