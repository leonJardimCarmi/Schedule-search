#!/usr/bin/env python3
"""Writes data/cafe_december.json: the main example of the contract (stage 8).

A café over three weeks of December, built by hand rather than at random:
each employee is a different kind of availability, each kind of shift a
different shape, and three shifts can never be fully staffed, one for each
reason (D25): nobody available (Friday 18/12 morning), too few available
(Saturday 19/12 morning needs 2), and the role missing (the guard on
Saturday 12/12, and the waiters of the event on 15/12). So one file shows everything an app has to display: partial
weeks, a row that appears only on Fridays, a night that crosses midnight, a
one-day event, roles, preferred shifts, approved doubles, a vacation, exam
days, a split day, a new hire, minShifts and maxShifts, and two people with
the same first name (D44). All names are made up.

    python3 tools/make_showcase.py              # writes data/cafe_december.json
"""
import datetime as dt
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from anonymize import fmt  # one shift / one availability line per line

START, END = dt.date(2026, 12, 2), dt.date(2026, 12, 22)   # Wednesday .. Tuesday
DAYS = [START + dt.timedelta(d) for d in range((END - START).days + 1)]
SUN, MON, TUE, WED, THU, FRI, SAT = 6, 0, 1, 2, 3, 4, 5      # date.weekday()
WEEKDAYS = {SUN, MON, TUE, WED, THU}                        # Sunday to Thursday
EVENT = dt.date(2026, 12, 15)


def iso(d):
    return d.isoformat()


def shifts():
    out = []
    for d in DAYS:
        tag, wd = d.strftime("%m%d"), d.weekday()
        def add(code, time, mn, wanted, role=None):
            s = {"id": f"{tag}-{code}", "date": iso(d), "time": time, "min": mn, "wanted": wanted}
            if role:
                s["role"] = role
            out.append(s)
        if wd == FRI:
            add("F", "07:00-13:00", 1, 2)                     # short Friday morning
        elif d == dt.date(2026, 12, 19):
            add("M", "06:30-14:30", 2, 2)                     # a busy Saturday: only 1 can come
        else:
            add("M", "06:30-14:30", 1, 1 if wd == SAT else 2)
        if wd in WEEKDAYS:
            add("D", "10:00-18:00", 1, 1, "manager")
        add("E", "14:30-22:30", 1, 3 if wd in (FRI, SAT) else 2)
        if wd in (THU, FRI, SAT):
            add("B", "18:00-23:00", 1, 1, "barista")
        add("N", "22:30-06:30", 1, 1, "guard")                # crosses midnight
        if d == EVENT:
            add("X", "17:00-23:00", 3, 4, "waiter")           # only 2 waiters exist: a hole
    return out


def line(d, time=None, kind="available", double=False):
    a = {"date": iso(d), "type": kind}
    if time:
        a["time"] = time
    if double:
        a["double"] = True
    return a


def employees():
    people = []
    def person(pid, name, roles, availability, **limits):
        e = {"id": pid, "name": name, "roles": roles}
        e.update(limits)
        e["availability"] = availability
        people.append(e)

    # 1. Full-time manager: Sunday to Thursday, prefers the manager shift,
    #    a doctor's appointment one morning (partial unavailability).
    av = []
    for d in DAYS:
        if d.weekday() in WEEKDAYS:
            av += [line(d, "06:00-18:00"), line(d, "10:00-18:00", "preferred")]
    av.append(line(dt.date(2026, 12, 16), "09:00-12:00", "unavailable"))   # #2 covers that day
    person(201, "Michal Avrahami", ["manager", "cashier"], av, maxShifts=15)

    # 2. Part-time manager: Mondays and Wednesdays, and Friday mornings
    #    except the last Friday.
    av = [line(d, "10:00-18:00") for d in DAYS if d.weekday() in (MON, WED)]
    av += [line(d, "07:00-15:00", "preferred") for d in DAYS if d.weekday() == FRI]
    av.append(line(dt.date(2026, 12, 18), kind="unavailable"))
    person(202, "Yoav Ben-Ami", ["manager", "cashier"], av, maxShifts=6)

    # 3. Student: evenings only, Thursdays preferred, two exam days off.
    av = []
    for d in DAYS:
        if d.weekday() in WEEKDAYS:
            av.append(line(d, "14:00-23:00", "preferred" if d.weekday() == THU else "available"))
    av += [line(dt.date(2026, 12, 9), kind="unavailable"), line(dt.date(2026, 12, 10), kind="unavailable")]
    person(203, "Shir Levinson", ["cashier"], av, maxShifts=8)

    # 4. Weekends only, the whole day, wants at least 4 shifts. The last
    #    Friday only from noon (a family event in the morning), so nobody
    #    can open that Friday.
    av = []
    for d in DAYS:
        if d.weekday() in (FRI, SAT):
            av.append(line(d, "12:00-23:59") if d == dt.date(2026, 12, 18) else line(d))
    person(204, "Tom Harel", ["cashier", "barista"], av, minShifts=4)

    # 5. Parent: mornings Sunday to Thursday, all preferred; never Fridays.
    av = [line(d, "06:00-15:00", "preferred") for d in DAYS if d.weekday() in WEEKDAYS]
    av += [line(d, kind="unavailable") for d in DAYS if d.weekday() == FRI]
    person(205, "Orly Segal", ["cashier"], av)

    # 6. Night guard, Sunday to Wednesday nights. Approved one double on
    #    Tuesday 8/12: the evening and the night after it, 16 hours (D16).
    av = []
    for d in DAYS:
        if d == dt.date(2026, 12, 8):
            av.append(line(d, "14:00-07:00", double=True))
        elif d.weekday() in (SUN, MON, TUE, WED):
            av.append(line(d, "22:00-07:00", "preferred"))
    person(206, "Boris Klein", ["guard", "cashier"], av)

    # 7. Night guard, Thursday to Saturday nights; on vacation 12/12-16/12.
    av = [line(d, "22:00-07:00") for d in DAYS if d.weekday() in (THU, FRI, SAT)]
    av += [line(dt.date(2026, 12, 12) + dt.timedelta(k), kind="unavailable") for k in range(5)]
    person(207, "Ruth Amar", ["guard"], av)

    # 8. Veteran: every day, many roles, wants 10 to 12 shifts. Weekends from
    #    the afternoon through the night (not a guard, so a missing guard is
    #    "missingRole", not "nobodyAvailable"). Away on Sunday 20/12.
    av = []
    for d in DAYS:
        if d == dt.date(2026, 12, 20):
            av.append(line(d, kind="unavailable"))
        elif d.weekday() in (FRI, SAT):
            av.append(line(d, "13:00-07:00"))
        else:
            av.append(line(d, "06:00-23:30"))
    person(208, "David Navon", ["cashier", "barista", "waiter"], av, minShifts=10, maxShifts=12)

    # 9. Barista: Thursday to Saturday evenings, prefers the short evening.
    av = []
    for d in DAYS:
        if d.weekday() in (THU, FRI, SAT):
            av += [line(d, "14:00-23:30"), line(d, "18:00-23:30", "preferred")]
    person(209, "Lia Kaplan", ["barista", "cashier"], av)

    # 10. Waiter: a split day on weekends (early morning and evening, neither
    #     covers a whole shift but the evening ones), and the event night.
    av = []
    for d in DAYS:
        if d.weekday() in (FRI, SAT):
            av += [line(d, "06:00-11:00"), line(d, "17:00-23:30")]
    av.append(line(EVENT, "17:00-23:00", "preferred"))
    person(210, "Nadav Elkayam", ["waiter", "cashier"], av)

    # 11. Likes doubles: Sunday to Tuesday, all day, every day approved.
    av = [line(d, "06:00-23:00", double=True) for d in DAYS if d.weekday() in (SUN, MON, TUE)]
    person(211, "Hen Barak", ["cashier"], av)

    # 12. New hire from Monday 14/12, Sunday to Thursday; the same first
    #     name as #4, so the table shows "Tom H." and "Tom S." (D44).
    av = [line(d, "06:00-23:00") for d in DAYS if d >= dt.date(2026, 12, 14) and d.weekday() in WEEKDAYS]
    person(212, "Tom Stern", ["cashier"], av, minShifts=3)
    return people


def main():
    problem = {
        "period": {"start": iso(START), "end": iso(END)},
        "employees": employees(),
        "shifts": shifts(),
        "rules": {"minRestHours": 10, "maxConsecutiveDays": 6},
    }
    out = pathlib.Path(__file__).resolve().parent.parent / "data" / "cafe_december.json"
    out.write_text(fmt(problem) + "\n", encoding="utf-8", newline="\n")
    print(f"{out}: {len(problem['employees'])} employees, {len(problem['shifts'])} shifts")


if __name__ == "__main__":
    main()
