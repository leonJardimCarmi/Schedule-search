# The result contract

What `schedule_search` writes with `--json`: everything the engine knows about
a result, for an app that shows it without parsing text and without calling
the engine's code (DECISIONS D40, D41, D53), and what an app writes for the
engine: the problem and a hand-made schedule. This document describes every
field. The exact shapes are [`result.schema.json`](result.schema.json),
[`problem.schema.json`](problem.schema.json) and
[`schedule.schema.json`](schedule.schema.json); where a schema and this
document disagree, that is a bug in one of them.

- **Current version:** `formatVersion: 1`
- **Examples:** [`examples/`](examples/): the café (the main example, D65),
  the example week (small, several schedules), and the medics (an edge case)
- **Check a file:** `python3 tools/check_contract.py result.json`, or
  `--schema problem` / `--schema schedule` for the inputs
  (`pip install "jsonschema>=4.18"`)

## Getting a result

```bash
schedule_search problem.json --json result.json   # the console as usual, and the file
schedule_search problem.json --json -             # only the JSON, to stdout (for a server)
schedule_search problem.json --check mine.json --json -              # a hand-made schedule, checked
schedule_search problem.json --check mine.json --compare --json -    # ... and the engine's next to it
schedule_search problem.json --explain 102 MON-M --json -            # could 102 also work MON-M, and why not
```

All the other options work as usual (`--time-limit`, `--max-nodes`, `--top`,
`--min-diff`, `--first`, `--weight`, `--availability`). With `--json -`,
warnings and errors still go to stderr as text, and they are also in the
JSON (`diagnostics`). The exit code is the same as without `--json`:

| Exit code | `run.status` | Meaning |
|---|---|---|
| 0 | `found` | A schedule was found. `run.complete` says whether it is proven best |
| 1 | `inputError` | The input has errors; they are in `diagnostics` |
| 2 | `noSchedule` | The whole search space was checked: no schedule exists |
| 3 | `limitReached` | Stopped by `--time-limit` or `--max-nodes` before finding any schedule |
| 0 or 4 | `checked` | `--check` without `--compare`: 4 if the hand-made schedule breaks availability, a role or a hard rule |

With `--check --compare` the exit code is the check's (0 or 4), and `status`
is the engine's search (`found`, `noSchedule` or `limitReached`). With
`--explain` the exit code is 0 (the answer was given). A JSON is written in
every case (D61). It is **not** written when the
command line itself is wrong (exit code 1, a message on stderr), or when the
file cannot be written, or the employee or shift of `--explain` does not exist.
`--json -` does not work with `--verbose` or `--trace`, which print to stdout.

## Conventions

- **Ids, never positions inside the engine.** An employee is its `id` from the
  input (a number), a shift is its `id` (a string). The one exception is
  `kind`, which is a position in `layout.kinds` of the same file.
- **Dates** `"YYYY-MM-DD"`. **Times** `"HH:MM-HH:MM"`, as in the input. If the
  end is not after the start, the shift crosses midnight: `"22:30-06:30"` ends
  the next morning, and `"13:00-13:00"` is 24 hours. Use `minutes` for the
  length instead of working it out.
- **A field with no value is left out**, as in the input: an employee without
  `maxShifts` has no `maxShifts` key (in TypeScript, `maxShifts?: number`).
  A list is always written, even when empty.
- **Fixed values are camelCase strings:** `"belowWanted"`, `"nobodyAvailable"`.
- **Order:** shifts by start time (the engine's order, not the input's),
  employees in the input's order, schedules best first.
- **Scores: lower is better.** A score has two levels, compared in order:
  `medium` first, and `soft` only when `medium` is equal (D29).
- **Text for people is English and only a fallback:** the `description` of
  each rule, and `message` of a diagnostic. Show your own text by the rule's
  `name`.

## The file

```jsonc
{
  "formatVersion": 1,
  "run":         { ... },   // how the search went
  "problem":     { ... },   // the period, employees and shifts   (not with inputError)
  "layout":      { ... },   // the rows and weeks of the table    (not with inputError)
  "shortages":   [ ... ],   // shifts that can never be staffed   (not with inputError)
  "schedules":   [ ... ],   // the engine's, best first; empty unless "found"
  "manual":      { ... },   // --check: the hand-made schedule and what it breaks
  "comparison":  { ... },   // --check --compare: the hand-made schedule vs the engine's
  "explanation": { ... },   // --explain: one employee, one shift
  "diagnostics": [ ... ]    // warnings and errors from loading the input files
}
```

### `run`

```json
{ "status": "found", "exitCode": 0, "mode": "best", "complete": true,
  "nodes": 43, "ms": 0.1, "improvements": 2, "pruned": 0,
  "startBound": { "medium": 0, "soft": 4 },
  "options": { "top": 5, "minDiff": 1, "timeLimit": 60.0 } }
```

| Field | Meaning |
|---|---|
| `status`, `exitCode` | See the table above. With `inputError` these are the only two fields |
| `mode` | `"best"` (the default: search for the best schedule), `"first"` (`--first`: the first legal one), or `"check"` (`--check` without `--compare`: no search, so the fields below are not written) |
| `complete` | `best` only. `true`: the whole search space was checked, so `schedules[0]` is proven best. `false`: the search stopped at a limit, and a better schedule may exist (the "anytime" result, D31) |
| `nodes`, `ms` | How many steps the search took, and how long, in milliseconds |
| `stopped` | Only when the search stopped before checking everything: `"time"` (`--time-limit`), `"nodes"` (`--max-nodes`) or `"request"` (stopped by the user, see [Progress and stopping](#progress-and-stopping)). Never with `complete: true` |
| `improvements`, `pruned` | `best` only. How often the best schedule improved; how many branches were cut by the bound |
| `startBound` | `best` only. No schedule can score better than this. The gap to `schedules[0].score` shows how far from proven the result is |
| `options` | What the search ran with: `top` and `minDiff` (`best` only), `timeLimit` (seconds), `maxNodes` (only if given) |

### `problem`

What is needed to show the result (D59); not the availability, not the rules.

```json
{ "period": { "start": "2026-11-08", "end": "2026-11-14" },
  "employees": [ { "id": 102, "name": "Dana Levi", "roles": ["cashier"], "minShifts": 3 } ],
  "shifts": [ { "id": "SUN-N", "date": "2026-11-08", "time": "22:00-06:00", "minutes": 480,
                "min": 1, "wanted": 1, "role": "guard", "kind": 2 } ] }
```

- An employee: `id`, `name`, `roles` (may be empty), and `minShifts` / `maxShifts` if set.
  `minShifts` is a wish (soft, D22); `maxShifts` is a hard limit.
- A shift: `id`, `date` (the day it **starts**, D13), `time`, `minutes`, `min`
  (a hard minimum, except for a shortage), `wanted` (a target), `role` if the
  shift needs one, and `kind`: its row in `layout.kinds`.

### `layout`: the weekly table

The table is part of the contract, so an app draws it without computing it
(D41, D42, D44). It depends only on the problem, so it is the same for every
schedule.

- **`kinds`**: the rows. One per distinct pair of hours and role, sorted by
  start time, then end time, then role: `{ "time": "22:00-06:00", "role": "guard" }`.
  A row may have shifts on a few days only (in the café, the short Friday
  morning, or a one-day event).
- **`weeks`**: full weeks, Sunday to Saturday, from the Sunday on or before
  the period's start. Each has `start` and `end` (Sunday and Saturday), seven
  `days` (`date`, and `inPeriod`: false for the days before the start or
  after the end, which are always empty), and `cells`.
- **`cells[k][d]`**: the ids of the shifts in row `k` on day `d` (0 = Sunday).
  Almost always zero or one shift; more only if the input has two shifts with
  the same hours and role on the same day. Every shift is in exactly one cell.

To draw week `w` of schedule `s`: a row per `layout.kinds[k]`, a column per
`layout.weeks[w].days[d]`, and in each cell, for each shift id, the people of
that shift from `schedules[s].shifts` (see below).

### `shortages`

Shifts that can never reach their `min`, found before the search: too few
people could work them at all (D25). The rest is still scheduled, and these
are filled as much as possible; the manager completes them by hand.

```json
{ "shift": "1215-X", "needed": 3, "candidates": 2, "available": 5, "reason": "missingRole" }
```

| `reason` | Meaning |
|---|---|
| `nobodyAvailable` | No one is available for the whole shift |
| `missingRole` | People are available, but too few of them have the shift's role |
| `tooFewAvailable` | Fewer people are available than the shift needs |

`needed` is the shift's `min`; `candidates`: available and with the role;
`available`: available, with the role or not. Each shortage adds to the
`below-min` rule in the score (Medium level).

### `schedules`

The list of good schedules (D39): `schedules[0]` is the best, and each other
one differs from all the others in at least `run.options.minDiff`
assignments. **Every schedule is complete** (D58), so an app can show any of
them in the same way.

```json
{ "rank": 2,
  "score": { "medium": 0, "soft": 36 },
  "rules": [ { "name": "below-wanted", "level": "soft", "violations": 2, "weight": 10,
               "penalty": 20, "description": "person missing below a shift's wanted staff" } ],
  "shifts": [ { "shift": "TUE-M", "status": "belowWanted",
                "staff": [ { "employee": 102, "preferred": false, "double": true } ] } ],
  "employees": [ { "employee": 102, "shifts": ["SUN-E", "TUE-M", "TUE-E"], "minutes": 1440 } ],
  "weeks": [ { "shifts": 8, "full": 6, "belowWanted": 2, "belowMin": 0,
               "perEmployee": [ { "employee": 101, "shifts": 2 } ] } ],
  "vsBest": { "differences": 1, "changes": [ { "shift": "TUE-E", "out": [102], "in": [104] } ] } }
```

- **`rank`**: 1, 2, 3... in the order of the list.
- **`score`** and **`rules`**: the score and where it comes from, one entry per
  soft rule, always the same rules in the same order. `penalty` = `violations`
  × `weight`, and the penalties of each level add up to the score. A rule with
  `weight` 0 is turned off (its penalty is 0). The rules today: `below-min`
  (medium), `below-wanted`, `double`, `below-min-shifts`, `not-preferred`,
  `consecutive-nights`, `fairness`, `fewer-trips` (soft); see DECISIONS
  D30–D38 and D49 for what each counts. A new rule may appear without a new
  version: show it by its `description` until the app knows its name.
- **`shifts`**: one entry per shift of the problem, in the same order.
  `status`: `full` (at least `wanted`), `belowWanted` (at least `min`), or
  `belowMin` (only the shifts in `shortages`). `staff`: who works it, and for
  each person `preferred` (they marked this shift "preferred") and `double`
  (it is part of a double for them, D16).
- **`employees`**: one entry per employee, in the problem's order: their
  shifts in calendar order, the total `minutes`, and `belowMinShifts`, their
  `minShifts`, only when they got fewer.
- **`weeks`**: one entry per week of `layout.weeks`, in the same order: how
  many shifts the week has, how many are `full`, `belowWanted` and
  `belowMin`, and how many shifts each employee works that week (every
  employee, also with 0).
- **`vsBest`**: every schedule but the first. `differences`: how many
  assignments differ from the best (one person replaced in one shift is one).
  `changes`: every shift that differs, in calendar order; `out`: in the best
  schedule but not in this one, `in`: in this one but not in the best.

### `manual` (`--check`)

The hand-made schedule (D51), **in exactly the form of a schedule** above
(`score`, `rules`, `shifts`, `employees`, `weeks`), without `rank` and
`vsBest`, and with:

- **`breaksRule`** on every person in `shifts[].staff`: this assignment breaks
  something listed in `violations` (the console marks it ` x`). Only here,
  never in `schedules`.
- **`violations`**: every problem, each listed once, at the later of the shifts
  involved (D51): `{ "shift": "SUN-N", "employee": 104, "kind": "rule",
  "rule": "min-rest", "why": "only 6h of rest between SUN-M and SUN-N, needs 10h" }`.
  `kind` is `availability` (not available for the whole shift), `role` (does
  not have the shift's role) or `rule` (a hard rule: `no-overlap`, `min-rest`,
  `double-needs-approval`, `max-shifts`, `max-consecutive-days`). Only a `rule`
  has `rule` and `why`; `why` is English text for people (D47), so show your
  own text by `rule`, with `why` as the detail.
- **`overStaffed`**: shifts with more people than `wanted`. A note, not a
  violation: `{ "shift": "SUN-N", "staff": 2, "wanted": 1 }`.

Nothing is blocked (D18): the score counts the soft rules only, so a schedule
that breaks hard rules can score better than the engine's. Always show
`violations` next to the score.

### `comparison` (`--check --compare`)

The hand-made schedule next to the engine's best, which is `schedules[0]`
(D52).

```json
{ "mine": { "medium": 0, "soft": 21 }, "engine": { "medium": 0, "soft": 33 }, "differences": 3,
  "rules": [ { "name": "below-wanted", "level": "soft", "weight": 10, "mine": 0, "engine": 20 } ],
  "changes": [ { "shift": "SUN-N", "out": [104], "in": [] } ] }
```

`rules`: the penalty of each soft rule in both (already multiplied by the
weight). `changes`: every shift that differs; `out`: only in the hand-made
schedule, `in`: only in the engine's, that is, what to change in the
hand-made one to get the engine's. If the engine found no schedule there is
no `comparison` (and `status` says why).

### `explanation` (`--explain <employee> <shift>`)

Could this employee also work this shift, in the schedule found (`against:
"best"`, `schedules[0]`) or in the hand-made one (`against: "manual"`)? Every
check answers on its own, so all the reasons show at once (D47).

```json
{ "employee": 102, "shift": "MON-M", "against": "best", "canWork": false, "alreadyWorks": false,
  "availability": "yes", "roleOk": true,
  "rules": [ { "name": "no-overlap" }, { "name": "min-rest", "whyNot": "only 8h of rest between SUN-E and MON-M, needs 10h" } ],
  "staff": [101] }
```

`availability`: `no`, `yes` or `preferred`. `rules`: every hard rule, with
`whyNot` only when it says no. `canWork`: available, with the role, and no
`whyNot`. `alreadyWorks`: they work it in that schedule, and the rules were
checked as if they did not (otherwise they would overlap themselves).
`staff`: who works the shift in that schedule. A shift that is already full
can still say `canWork: true`: the rules allow it, `wanted` is a target.

### `diagnostics`

Every warning and error from loading the input, the same lines the console
prints to stderr: `{ "severity": "error", "where": "problem.json: employees[0].availability[0].date", "message": "date does not exist: 2026-02-30" }`.
`where` starts with the file name and points into it: the problem file, the
availability CSV, or the schedule file of `--check`. With `inputError` at
least one has `severity: "error"`; otherwise there are only warnings.

## The inputs

What an app writes for the engine (D56, D66). Both schemas are stricter than
the engine, which only warns about an unknown field: a file an app writes
should have none.

- **The problem** ([`problem.schema.json`](problem.schema.json)): `period`,
  `employees` (with their `availability`), `shifts`, and the optional `rules`
  and `weights`, as in DECISIONS section 2. What a schema cannot check, the
  engine checks when it loads the file, and reports in `diagnostics` (with
  `inputError` for an error): a date that does not exist (2026-02-30), a time
  such as 25:00, a duplicate id, `min` above `wanted`, a shift outside the
  period, `minShifts` above `maxShifts`.
- **A hand-made schedule** ([`schedule.schema.json`](schedule.schema.json)),
  for `--check` (D51): `{ "assignments": { "SUN-M": [104, "Dana Levi"] } }`,
  shifts by id, employees by id or full name. A shift that is left out is
  empty (a warning). An unknown shift or employee, or the same person twice
  in a shift, is an error (`inputError`, in `diagnostics`).

## Progress and stopping

For a search that takes a while (a month can take a minute), the app can
show it moving and let the manager stop it (D56, D67).

**`--progress`** writes one JSON object per line to **stderr** while the
search runs; stdout stays for the result (`--json -`). Read only the lines
that start with `{`: warnings on stderr stay plain text (they are also in
`diagnostics`).

```
{"event":"start","mode":"best","shifts":88,"employees":12}
{"event":"improved","nodes":342,"ms":2.0,"score":{"medium":4,"soft":590},"place":0}
{"event":"tick","nodes":51200,"ms":1000.3,"best":{"medium":4,"soft":585}}
{"event":"done","status":"found","nodes":386048,"ms":2501.5,"complete":false,"stopped":"time"}
```

| `event` | When | Fields |
|---|---|---|
| `start` | The search starts | `mode` (`best` or `first`), `shifts`, `employees` |
| `improved` | A schedule entered the list of good schedules | `nodes`, `ms`, `score`, `place` (0 = the new best) |
| `found` | `--first`: the first legal schedule; the search ends | `nodes`, `ms` |
| `tick` | About once a second while searching | `nodes`, `ms`, `best` (the best score so far, if any) |
| `done` | The search ended; the result follows | `status` (as `run.status`), `nodes`, `ms`, `complete` (`best` only), `stopped` (if it stopped early) |

`--progress` works with `--check --compare`, but not with `--trace`.

**Stopping.** The search stops at its next check, within 1024 nodes (a few
milliseconds), and ends like a time limit: the result is the best schedule
so far, with `complete: false` and `stopped: "request"`, and exit code 0. If
no schedule was found yet, it is `limitReached` (exit code 3). Two ways:

- **Ctrl+C, or SIGTERM:** always, during the search. A second Ctrl+C quits at
  once, without a result.
- **`--stop-on-stdin`:** the line `stop` on the engine's stdin, or the end of
  stdin, stops the search. This is the way for a server on Windows, where
  Node cannot send Ctrl+C to a child process (`kill()` ends it at once,
  without a result). Do not use it with an empty stdin: it would stop at once.

A search that ends before its first check (under 1024 nodes) is complete,
even if it was asked to stop.

## What an app computes itself

The contract gives facts; how to show them is the app's choice (D41, D44):
short names in a cell ("Tom H.", "Tom S."), the names of days and months,
colors and icons, and the text of rules and reasons in the app's language.
Anything the engine knows and the screen needs, but the contract lacks,
should be added to the contract rather than computed in the app.

## Versions and changes

- `formatVersion` is a whole number. **It does not change** when a field is
  added: an app that does not know a new field ignores it. **It goes up**
  when a field is removed or renamed, or its meaning or type changes (D62).
- The schema is strict (an unknown field is an error), to catch mistakes in
  the engine's writer. The engine and the app live in one repository (A1),
  so both always use the schema of the same commit.
- A change to the contract changes, in one commit: the writer
  (`src/io/ResultJson.cpp`) or the loader, the schema, this document, the
  examples (`examples/README.md` has the commands), and the golden output
  (`bash tools/golden.sh <exe> --update`).

## Not in the contract yet

- Reasons as data and not only text (`why`, `whyNot`): when an app needs
  them in its own language, a `details` field is added next to the text,
  without a new version (D66)
- Availability in the result (D59): read the problem file, until an app needs
  it here
