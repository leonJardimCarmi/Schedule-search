#!/usr/bin/env python3
"""Measures the solver on a problem file, the same way every time, so that
an optimization can be compared before and after.

Two experiments:
  growing  - the problem cut to its first N days (a solvable problem growing)
  broken   - the full problem with ONE night shift made impossible on day k
             (a role nobody has): how long until the solver says "no schedule"

    python3 tools/benchmark.py build/schedule_search data/month_15x90.json
"""
import argparse
import copy
import datetime as dt
import json
import os
import re
import subprocess
import tempfile
import time


def run(solver, problem, limit):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(problem, f)
        path = f.name
    start = time.perf_counter()
    try:
        # The solver enforces the limit itself (--time-limit) and still reports
        # its node count. The subprocess timeout is only a safety net.
        p = subprocess.run([solver, path, "--verbose", "--time-limit", str(limit)],
                           capture_output=True, text=True, timeout=limit + 10)
        seconds = time.perf_counter() - start
        m = re.search(r"(?:visited|after) (\d+) nodes", p.stdout)
        nodes = int(m.group(1)) if m else None
        result = {0: "found", 2: "no schedule", 3: f"LIMIT ({limit:g}s)"}.get(p.returncode, f"exit {p.returncode}")
    except subprocess.TimeoutExpired:
        seconds, nodes, result = limit, None, f"TIMEOUT ({limit}s)"
    finally:
        os.unlink(path)
    return nodes, seconds, result


def cut(problem, days):
    p = copy.deepcopy(problem)
    end = (dt.date.fromisoformat(p["period"]["start"]) + dt.timedelta(days=days - 1)).isoformat()
    p["period"]["end"] = end
    p["shifts"] = [s for s in p["shifts"] if s["date"] <= end]
    for e in p["employees"]:
        e["availability"] = [x for x in e["availability"] if x["date"] <= end]
        e.pop("minShifts", None)          # soft, and may exceed maxShifts on a short cut
    return p


def break_night(problem, day):
    p = copy.deepcopy(problem)
    date = (dt.date.fromisoformat(p["period"]["start"]) + dt.timedelta(days=day - 1)).isoformat()
    for s in p["shifts"]:
        if s["date"] == date and s["id"].endswith("-N"):
            s["role"] = "nobody-has-this-role"
    return p


def table(title, rows):
    print(f"\n{title}")
    print(f"  {'':<12}{'nodes':>12}{'seconds':>10}  result")
    for label, (nodes, seconds, result) in rows:
        print(f"  {label:<12}{nodes if nodes is not None else '?':>12}{seconds:>10.2f}  {result}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("solver")
    ap.add_argument("problem")
    ap.add_argument("--limit", type=float, default=60, help="seconds per run (passed as --time-limit)")
    a = ap.parse_args()
    problem = json.load(open(a.problem))
    total = len({s["date"] for s in problem["shifts"]})

    rows = []
    for n in [d for d in (3, 7, 14, 21, 30) if d <= total]:
        rows.append((f"{n} days", run(a.solver, cut(problem, n), a.limit)))
    table("growing: first N days of the problem", rows)

    rows = []
    for k in range(1, 8):
        r = run(a.solver, break_night(problem, k), a.limit)
        rows.append((f"day {k}", r))
        if r[2].startswith(("TIMEOUT", "LIMIT")):
            break
    table("broken: one impossible night on day k", rows)


if __name__ == "__main__":
    main()
