#!/usr/bin/env python3
"""Measures the solver on a problem file, the same way every time, so that
an optimization can be compared before and after.

Three experiments:
  growing   - the problem cut to its first N days (a solvable problem growing)
  shortage  - ONE night shift on day k that nobody can work (a role nobody
              has). Visible before the search starts (DECISIONS D25).
  conflict  - an extra noon shift on day k that needs EVERYONE available at
              noon, while the morning shift (whose people are all among them)
              needs someone too. Impossible, but no single shift shows it:
              the search only finds out when it reaches noon on day k.

    python3 tools/benchmark.py build/schedule_search data/month_15x90.json

Use a Release build: a Debug build is about 13 times slower and includes
the debug-only self-checks (assert), so its times say little.
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
        t = re.search(r"nodes in ([\d.]+) ms", p.stdout)
        if t:
            seconds = float(t.group(1)) / 1000   # the search alone, not process start + JSON
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


def minutes(t):
    a, b = t.split("-")
    start = int(a[:2]) * 60 + int(a[3:])
    end = int(b[:2]) * 60 + int(b[3:])
    return start, end + 1440 if end <= start else end


def available_at(employee, date, time):
    """Same idea as availabilityFit: an available slot covering the whole time."""
    s, e = minutes(time)
    slots = [x for x in employee["availability"] if x["date"] == date]
    if any(x["type"] == "unavailable" for x in slots):
        return False
    return any("time" not in x or (minutes(x["time"])[0] <= s and e <= minutes(x["time"])[1]) for x in slots)


def add_conflict(problem, day):
    p = copy.deepcopy(problem)
    date = (dt.date.fromisoformat(p["period"]["start"]) + dt.timedelta(days=day - 1)).isoformat()
    everyone = sum(available_at(e, date, "12:00-13:00") for e in p["employees"])
    p["shifts"].append({"id": f"NOON{day}", "date": date, "time": "12:00-13:00",
                        "min": everyone, "wanted": everyone})
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
    print(f"  {'':<12}{'nodes':>12}{'search ms':>11}  result")
    for label, (nodes, seconds, result) in rows:
        print(f"  {label:<12}{nodes if nodes is not None else '?':>12}{seconds * 1000:>11.1f}  {result}")


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

    for title, make in [("shortage: a night nobody can work, on day k", break_night),
                        ("conflict: impossible, but only found when the search gets there", add_conflict)]:
        rows = []
        for k in range(1, 8):
            r = run(a.solver, make(problem, k), a.limit)
            rows.append((f"day {k}", r))
            if r[2].startswith(("TIMEOUT", "LIMIT")):
                break
        table(title, rows)


if __name__ == "__main__":
    main()
