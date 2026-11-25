#!/usr/bin/env python3
"""Copy a problem file with made-up names instead of real ones (DECISIONS D60).

The repository is public, so files with real names stay in data/private/,
which Git ignores. This script writes a copy that can be committed: each
employee gets the made-up name at their position in NAMES. Everything else
(ids, shifts, availability, rules, weights) is copied as it is, so the
engine finds the same schedule, and only the names in its output differ.

NAMES holds made-up names only, so this script names no one. A made-up
name that happens to be one of the real names in the file is skipped, so
no real name stays in the copy.

usage: python3 tools/anonymize.py data/private/real.json data/demo.json
"""
import json
import sys

NAMES = ["Adam", "Bar", "Carmel", "Dor", "Eden", "Gal", "Hadar", "Itai", "Kfir", "Lavi",
         "Maayan", "Nir", "Ofir", "Paz", "Ron", "Shaked", "Tomer", "Uri", "Yarden", "Ziv"]


def fmt(value, depth=0):
    """JSON with one object per line where the object holds only plain values,
    like the hand-written inputs: one availability line or one shift per line."""
    pad = "  " * depth
    flat = lambda v: not isinstance(v, (dict, list)) or (
        isinstance(v, list) and all(not isinstance(x, (dict, list)) for x in v))
    if isinstance(value, dict):
        if all(flat(v) for v in value.values()):
            return json.dumps(value, ensure_ascii=False)
        items = [f'{pad}  {json.dumps(k)}: {fmt(v, depth + 1)}' for k, v in value.items()]
        return "{\n" + ",\n".join(items) + f"\n{pad}}}"
    if isinstance(value, list):
        if flat(value):
            return json.dumps(value, ensure_ascii=False)
        items = [f"{pad}  {fmt(v, depth + 1)}" for v in value]
        return "[\n" + ",\n".join(items) + f"\n{pad}]"
    return json.dumps(value, ensure_ascii=False)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[-1])
    with open(sys.argv[1], encoding="utf-8") as f:
        problem = json.load(f)
    employees = problem.get("employees", [])
    real = {e.get("name", "").strip().lower() for e in employees}
    made_up = [n for n in NAMES if n.lower() not in real]
    if len(employees) > len(made_up):
        sys.exit(f"{len(employees)} employees, only {len(made_up)} made-up names: add names to NAMES")
    for employee, name in zip(employees, made_up):
        employee["name"] = name
    with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as f:
        f.write(fmt(problem) + "\n")
    print(f"{sys.argv[2]}: {len(employees)} names replaced")


if __name__ == "__main__":
    main()
