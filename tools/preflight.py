"""Reject incomplete or unverified sheet cells and unresolved references before a build."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def inspect_sheets(directory: Path) -> dict:
    sheets = {}
    findings = []
    files = sorted(directory.glob("*.json"))
    if not files:
        findings.append({"problem": "No sheets found"})
    for path in files:
        try:
            sheet = json.loads(path.read_text(encoding="utf-8"))
            name = sheet["name"]
            if name in sheets:
                raise ValueError(f"Duplicate sheet name: {name}")
            columns = sheet["columns"]
            if not columns or len(columns) != len(set(columns)) or "id" not in columns:
                raise ValueError("Columns must be unique and contain id")
            rows = sheet["rows"]
            ids = [row.get("id") for row in rows]
            if not rows or len(ids) != len(set(ids)):
                raise ValueError("Rows must be nonempty and have unique ids")
            sheets[name] = sheet
        except (KeyError, TypeError, ValueError) as error:
            findings.append({"file": path.name, "problem": str(error)})
    cells = 0
    for name, sheet in sheets.items():
        checks = sheet.get("checks", {})
        for row in sheet["rows"]:
            checked = sheet["columns"] if checks == "all" else checks.get(row["id"], [])
            for column in sheet["columns"]:
                cells += 1
                value = row.get(column)
                cell = {"sheet": name, "row": row["id"], "column": column}
                if value is None or value == "" or value == []:
                    findings.append({**cell, "problem": "Unfilled cell"})
                if column not in checked:
                    findings.append({**cell, "problem": "Unverified cell"})
                if column == "runtime_verified" and value is not True:
                    findings.append({**cell, "problem": "Runtime verification has not passed"})
                if column == "verification" and value != "passed":
                    findings.append({**cell, "problem": f"Verification is {value!r}"})
            extras = set(row) - set(sheet["columns"])
            if extras:
                findings.append({"sheet": name, "row": row["id"], "problem": f"Undeclared columns: {sorted(extras)}"})
            for ref in sheet.get("references", []):
                target = sheets.get(ref["sheet"])
                known = {r.get(ref["key"]) for r in target["rows"]} if target else set()
                if row.get(ref["column"]) not in known:
                    findings.append({"sheet": name, "row": row["id"], "column": ref["column"], "problem": f"Unresolved reference to {ref['sheet']}.{ref['key']}"})
    return {"clean": not findings, "sheet_count": len(sheets), "cell_count": cells, "findings": findings}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sheets", type=Path, default=Path(__file__).resolve().parents[1] / "sheets")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = inspect_sheets(args.sheets)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Preflight {'PASS' if report['clean'] else 'BLOCKED'}: {report['sheet_count']} sheets, {report['cell_count']} cells, {len(report['findings'])} findings")
    for finding in report["findings"]:
        print(" / ".join(str(finding.get(k, "")) for k in ("sheet", "row", "column")) + ": " + finding["problem"])
    return 0 if report["clean"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
