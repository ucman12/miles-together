"""Lay every sheet over the others and list what is unfilled, unverified or unresolved.

Stages:
  build    every cell filled, every reference resolves, every native row verified live,
           every source file named by a system exists, every row is used by a system.
  release  all of the above, and every release column (systems.verification) is "passed".
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Sheets whose rows only mean something when a system uses them.
MUST_BE_USED = {"native_functions", "native_data", "layout", "assets", "net_messages", "settings", "melty_contract", "compatibility", "test_checks"}


def empty(value) -> bool:
    return value is None or value == "" or value == [] or value == {}


def load_sheets(directory: Path) -> tuple[dict, list]:
    sheets, findings = {}, []
    files = sorted(directory.glob("*.json"))
    if not files:
        findings.append({"problem": "No sheets found"})
    for path in files:
        try:
            sheet = json.loads(path.read_text(encoding="utf-8"))
            name = sheet["name"]
            if name in sheets:
                raise ValueError(f"Duplicate sheet name: {name}")
            if name != path.stem:
                raise ValueError(f"Sheet name {name!r} does not match its file")
            columns = sheet["columns"]
            if not columns or len(columns) != len(set(columns)) or columns[0] != "id":
                raise ValueError("Columns must be unique and start with id")
            ids = [row.get("id") for row in sheet["rows"]]
            if not ids or len(ids) != len(set(ids)):
                raise ValueError("Rows must be nonempty and have unique ids")
            sheets[name] = sheet
        except (KeyError, TypeError, ValueError) as error:
            findings.append({"sheet": path.stem, "problem": str(error)})
    return sheets, findings


def inspect_sheets(directory: Path, stage: str = "build", root: Path | None = None) -> dict:
    sheets, findings = load_sheets(directory)
    cells = 0
    used: set[tuple[str, str]] = set()
    known = {name: {row["id"] for row in sheet["rows"]} for name, sheet in sheets.items()}
    for name, sheet in sheets.items():
        release_columns = set(sheet.get("release_columns", []))
        for row in sheet["rows"]:
            for column in sheet["columns"]:
                cells += 1
                cell = {"sheet": name, "row": row["id"], "column": column}
                value = row.get(column)
                if empty(value):
                    findings.append({**cell, "problem": "Unfilled cell"})
                    continue
                if column == "runtime_verified" and value is not True:
                    findings.append({**cell, "problem": "Not verified in the running game"})
                if column in release_columns and stage == "release" and value != "passed":
                    findings.append({**cell, "problem": f"Release check is {value!r}, not 'passed'"})
                if column == "source" and root is not None and not (root / value).is_file():
                    findings.append({**cell, "problem": f"Source file {value} does not exist"})
            extras = set(row) - set(sheet["columns"])
            if extras:
                findings.append({"sheet": name, "row": row["id"], "problem": f"Undeclared columns: {sorted(extras)}"})
            for reference in sheet.get("references", []):
                column = reference["column"]
                values = row.get(column)
                values = values if isinstance(values, list) else [values]
                for value in values:
                    if empty(value):
                        continue
                    if reference.get("qualified"):
                        target, _, key = str(value).partition(":")
                    else:
                        target, key = reference["sheet"], value
                    if key not in known.get(target, set()):
                        findings.append({"sheet": name, "row": row["id"], "column": column, "problem": f"Unresolved reference {value!r}"})
                    else:
                        used.add((target, key))
    if "systems" in sheets:
        for name in sorted(MUST_BE_USED & set(sheets)):
            for row in sheets[name]["rows"]:
                if (name, row["id"]) not in used:
                    findings.append({"sheet": name, "row": row["id"], "problem": "No system uses this row (unimplemented)"})
    pending = [
        f"{name}/{row['id']}/{column}"
        for name, sheet in sheets.items()
        for row in sheet["rows"]
        for column in sheet.get("release_columns", [])
        if row.get(column) != "passed"
    ]
    return {"stage": stage, "clean": not findings, "sheet_count": len(sheets), "cell_count": cells, "findings": findings, "pending_release_checks": pending}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sheets", type=Path, default=ROOT / "sheets")
    parser.add_argument("--stage", choices=["build", "release"], default="build")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = inspect_sheets(args.sheets, args.stage, ROOT if args.sheets.resolve() == (ROOT / "sheets").resolve() else None)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Preflight ({args.stage}) {'PASS' if report['clean'] else 'BLOCKED'}: {report['sheet_count']} sheets, {report['cell_count']} cells, {len(report['findings'])} findings, {len(report['pending_release_checks'])} release checks still pending")
    for finding in report["findings"]:
        print(" / ".join(str(finding.get(k, "")) for k in ("sheet", "row", "column")) + ": " + finding["problem"])
    return 0 if report["clean"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
