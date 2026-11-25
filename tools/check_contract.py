#!/usr/bin/env python3
"""Check result files against the contract (DECISIONS D62).

Two checks for every file:
1. The shape: contract/result.schema.json (draft 2020-12, strict).
2. What a schema cannot say: every employee and shift id that is used
   exists in "problem", every "kind" points to a row of the layout, and
   every shift sits in exactly one cell of the weekly table, on its date.

The Catch2 tests check the values; this checks the form an app relies on.

usage: python3 tools/check_contract.py <result.json> [...]
needs: pip install "jsonschema>=4.18"
"""
import json
import pathlib
import sys

from jsonschema import Draft202012Validator

SCHEMA = pathlib.Path(__file__).resolve().parent.parent / "contract" / "result.schema.json"


def references(result):
    """Problems a schema cannot see. Empty list = fine."""
    if "problem" not in result:
        return []
    found = []
    employees = {e["id"] for e in result["problem"]["employees"]}
    shifts = {s["id"]: s for s in result["problem"]["shifts"]}
    kinds = len(result["layout"]["kinds"])

    def employee(where, eid):
        if eid not in employees:
            found.append(f"{where}: no employee {eid} in problem.employees")

    def shift(where, sid):
        if sid not in shifts:
            found.append(f"{where}: no shift {sid!r} in problem.shifts")

    for s in result["problem"]["shifts"]:
        if s["kind"] >= kinds:
            found.append(f"problem.shifts {s['id']}: kind {s['kind']} but layout has {kinds} kinds")

    placed = {}
    for w, week in enumerate(result["layout"]["weeks"]):
        if len(week["cells"]) != kinds:
            found.append(f"layout.weeks[{w}]: {len(week['cells'])} rows, layout has {kinds} kinds")
        for k, row in enumerate(week["cells"]):
            for d, cell in enumerate(row):
                for sid in cell:
                    shift(f"layout.weeks[{w}].cells[{k}][{d}]", sid)
                    if sid in placed:
                        found.append(f"layout: shift {sid!r} is in two cells")
                    placed[sid] = (week["days"][d]["date"], k)
    for sid, s in shifts.items():
        if sid not in placed:
            found.append(f"layout: shift {sid!r} is in no cell")
        elif placed[sid] != (s["date"], s["kind"]):
            found.append(f"layout: shift {sid!r} is in the cell of {placed[sid]}, not of its date and kind")

    for g in result["shortages"]:
        shift("shortages", g["shift"])
    for r, schedule in enumerate(result["schedules"]):
        at = f"schedules[{r}]"
        if schedule["rank"] != r + 1:
            found.append(f"{at}: rank {schedule['rank']}, expected {r + 1}")
        listed = [s["shift"] for s in schedule["shifts"]]
        if sorted(listed) != sorted(shifts):
            found.append(f"{at}.shifts: not exactly one entry per shift of the problem")
        for s in schedule["shifts"]:
            for person in s["staff"]:
                employee(f"{at}.shifts {s['shift']}", person["employee"])
        for e in schedule["employees"]:
            employee(f"{at}.employees", e["employee"])
            for sid in e["shifts"]:
                shift(f"{at}.employees {e['employee']}", sid)
        if len(schedule["weeks"]) != len(result["layout"]["weeks"]):
            found.append(f"{at}.weeks: {len(schedule['weeks'])} weeks, layout has {len(result['layout']['weeks'])}")
        for c in schedule.get("vsBest", {}).get("changes", []):
            shift(f"{at}.vsBest", c["shift"])
            for eid in c["out"] + c["in"]:
                employee(f"{at}.vsBest {c['shift']}", eid)
    return found


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip().splitlines()[-2])
    validator = Draft202012Validator(json.loads(SCHEMA.read_text(encoding="utf-8")))
    failed = 0
    for path in sys.argv[1:]:
        result = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
        problems = [f"{'/'.join(map(str, e.absolute_path)) or '(root)'}: {e.message[:160]}"
                    for e in validator.iter_errors(result)]
        if not problems:
            problems = references(result)
        for p in problems[:20]:
            print(f"{path}: {p}")
        failed += bool(problems)
    print(f"contract: {len(sys.argv) - 1 - failed} of {len(sys.argv) - 1} files match")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
