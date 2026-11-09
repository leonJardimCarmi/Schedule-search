#!/usr/bin/env python3
"""Generates a realistic, reproducible scheduling problem for performance tests.

Three shifts a day (morning, evening, night guard), employees with varied
requests: fixed day-off patterns, morning-only / evening-only people, night
guards, preferred shifts, a few approved doubles, minShifts / maxShifts.

The same --seed always produces the same file, so measurements before and
after an optimization compare the exact same problem.

    python3 tools/generate_problem.py --days 30 --employees 15 --seed 1 -o data/month.json
"""
import argparse
import datetime as dt
import json
import random

SHIFTS = [  # id suffix, time, min, wanted, role
    ("M", "07:00-15:00", 1, 2, None),
    ("E", "15:00-23:00", 1, 2, None),
    ("N", "23:00-07:00", 1, 1, "guard"),
]

# Kinds of people, as a manager would describe them.
PROFILES = [
    # name, share, roles, windows they offer on a working day
    ("morning person", 0.28, ["cashier"], ["07:00-15:00"]),
    ("evening person", 0.25, ["cashier"], ["15:00-23:00"]),
    ("flexible",       0.20, ["cashier"], ["07:00-23:00", "07:00-15:00", "15:00-23:00"]),
    ("night guard",    0.27, ["cashier", "guard"], ["23:00-07:00", "23:00-07:00", "15:00-23:00"]),
]

FIRST = ["Yossi", "Dana", "Avi", "Noa", "Moshe", "Tamar", "Eli", "Shira", "Omer",
         "Maya", "Ron", "Yael", "Itay", "Lior", "Gal", "Amit", "Roni", "Hila", "Ido", "Neta"]
LAST = ["Cohen", "Levi", "Mizrahi", "Shapira", "Peretz", "Biton", "Friedman", "Azoulay",
        "Katz", "Golan", "Ben-David", "Avraham", "Dahan", "Halevi", "Ohana"]


def profile_list(n):
    """Fixed quotas, not a random draw: a real business hires to cover its shifts.
    With 15 employees: 4 morning, 4 evening, 3 flexible, 4 night guards."""
    counts = [round(p[1] * n) for p in PROFILES]
    counts[2] += n - sum(counts)              # rounding leftovers go to "flexible"
    return [p for p, c in zip(PROFILES, counts) for _ in range(c)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", type=int, default=30)
    ap.add_argument("--employees", type=int, default=15)
    ap.add_argument("--start", default="2026-12-06")   # a Sunday
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--work-rate", type=float, default=0.7,
                    help="share of days each employee offers to work")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    start = dt.date.fromisoformat(a.start)
    days = [start + dt.timedelta(days=i) for i in range(a.days)]

    shifts = [{"id": f"{d.strftime('%m%d')}-{sfx}", "date": d.isoformat(), "time": t,
               "min": mn, "wanted": wn, **({"role": role} if role else {})}
              for d in days for (sfx, t, mn, wn, role) in SHIFTS]

    employees = []
    names = rng.sample([f"{f} {l}" for f in FIRST for l in LAST], a.employees)   # unique
    for i, (kind, _, roles, windows) in enumerate(profile_list(a.employees)):
        days_off = 1 if kind == "night guard" else rng.choice([1, 2])
        fixed_off = rng.sample(range(7), days_off)             # e.g. never on Fri/Sat
        slots = []
        for d in days:
            weekday = (d.weekday() + 1) % 7                   # 0 = Sunday
            if weekday in fixed_off:
                slots.append({"date": d.isoformat(), "type": "unavailable"})
                continue
            if rng.random() > a.work_rate:
                continue                                       # simply not offered
            slot = {"date": d.isoformat(), "time": rng.choice(windows),
                    "type": "preferred" if rng.random() < 0.25 else "available"}
            if slot["time"] == "07:00-23:00" and rng.random() < 0.3:
                slot["double"] = True                          # approves a double
            slots.append(slot)

        e = {"id": 100 + i, "name": names[i],
             "roles": roles, "availability": slots}
        if rng.random() < 0.3:
            e["maxShifts"] = rng.choice([12, 15, 18])
        if rng.random() < 0.3:
            e["minShifts"] = rng.choice([8, 10, 12])
        employees.append(e)

    problem = {
        "period": {"start": days[0].isoformat(), "end": days[-1].isoformat()},
        "employees": employees,
        "shifts": shifts,
        "rules": {"minRestHours": 10, "maxConsecutiveDays": 6},
    }
    with open(a.output, "w") as f:
        json.dump(problem, f, indent=1)
    print(f"{a.output}: {len(employees)} employees, {len(shifts)} shifts, "
          f"{sum(len(e['availability']) for e in employees)} availability lines")


if __name__ == "__main__":
    main()
