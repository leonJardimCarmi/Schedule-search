# Schedule Search

A C++ engine that builds the **best possible work schedule** for a business: who works which shift, given each employee's availability and the business's own rules.

The rules are not hard-coded. Each business describes its week in a JSON file: the shifts, how many people each shift needs, who is available when, and which rules apply (rest between shifts, consecutive days, double shifts…). The engine searches the space of possible schedules with **depth-first search (DFS) and backtracking**.

> **Status:** the search, all hard rules and the CSV import work (stages 0–3 of the [plan](PLAN.md)). Today the engine returns the first legal schedule it finds. Scoring and finding the *best* schedule (stage 5) are next on the roadmap.

---

## The idea

Scheduling is a search problem. Every shift is a decision ("which group of people works here?"), and every decision limits the ones after it. The engine works in two phases:

1. **Find a legal schedule.** DFS walks through the shifts in chronological order and tries groups of employees for each one. If a rule is broken, that branch is cut immediately and the search backtracks. *(done)*
2. **Find the best schedule.** Every legal schedule gets a score (preferred shifts honored, fair split of shifts, shifts staffed to the wanted level, few doubles…). A **Branch & Bound** search keeps the best score found so far and prunes any branch that cannot beat it. The output is the top-ranked schedule, or a list of the top N. *(planned)*

### Why C++

The search space grows exponentially with the number of shifts and employees: a month of shifts can mean millions of nodes. C++ keeps each node cheap:
- the search state is a few vectors of `int` indices, never copies of objects,
- assigning and un-assigning are O(1) stack-like operations,
- everything that never changes during the search (availability, roles) is computed **once** before the search starts.

---

## The hardest part: rules that are dynamic

Every business has different rules, and they change over time. Some places need a minimum rest of 10 hours, some don't count consecutive days at all, and next year someone will ask for a rule that doesn't exist yet. On top of that, rules come in **two kinds**:

| Kind | Meaning | Example | Effect on the search |
|---|---|---|---|
| **Hard** | Must never be broken | 10 hours of rest between shifts | The branch is cut immediately |
| **Soft** | Should be respected when possible | Give people their *preferred* shifts | Lowers the schedule's score (stage 5) |

The challenge is checking all of these inside a hot search loop without turning the solver into one giant `if` statement, while keeping rules easy to add, remove, or turn off per business.

### How it's solved

**1. Every hard rule is a class behind one interface.** The solver doesn't know which rules exist. It asks each one the same question:

```cpp
class Constraint {
public:
    virtual ~Constraint() = default;
    // May employee `emp` be added to shift `shift`, given what is already assigned?
    virtual bool allows(const Problem& problem, const State& state, int emp, int shift) const = 0;
    virtual const char* name() const = 0;
};
```

```cpp
bool Solver::allowed(int emp, int shift) const {
    for (const auto& constraint : constraints_)
        if (!constraint->allows(problem_, state_, emp, shift)) return false;
    return true;
}
```

**2. The input decides which rules are active.** A factory builds the list from the JSON file. Rules with sensible defaults are always on. Optional rules are added only when the business sets them:

```cpp
ConstraintList makeConstraints(const Problem& problem) {
    ConstraintList list;
    list.push_back(std::make_unique<NoOverlapConstraint>());
    list.push_back(std::make_unique<MinRestConstraint>(rules.minRestHours, rules.blockGapMinutes));
    list.push_back(std::make_unique<DoubleShiftConstraint>(...));
    list.push_back(std::make_unique<MaxShiftsConstraint>());
    if (rules.maxConsecutiveDays)    // optional: only if the file sets it
        list.push_back(std::make_unique<MaxConsecutiveDaysConstraint>(*rules.maxConsecutiveDays));
    return list;
}
```

Adding a new rule means writing one class and one line in the factory. The solver itself doesn't change.

**3. Static checks are moved out of the loop.** Availability and role never change while searching, so they aren't `Constraint`s at all. They're used once to build a *candidate table* (`shift → employees who could work it`), and the search only ever looks at those candidates.

**4. Soft rules will become a score, not a filter.** Hard rules answer yes or no. Soft rules will add penalties to a scoring function, and Branch & Bound will use the scores to find the best schedule (stage 5). Some soft preferences already shape the search order: for example, employees for whom a shift would create a double shift are tried **last**, so doubles are used only when there is no other way.

---

## How the search works

```
nextShift()                            fill(s, next, target)
  ├─ all shifts done? → schedule found   ├─ shift has `target` people? → nextShift()
  ├─ s = hardest open shift (MRV)        └─ for each candidate i ≥ next:
  │    dead end if it can't reach min         if every Constraint allows:
  ├─ order candidates (non-doubles first)        assign → fill(s, i + 1, target) → unassign
  └─ for target = wanted … min:
        fill(s, 0, target)
```

- **Hardest shift first (MRV).** The next shift is not the next one on the calendar but the one with the least slack right now: the fewest employees the rules still allow, compared with its minimum. A shift that can no longer be staffed is noticed at once, before any other work is done in that branch. On a month where one day is secretly impossible, this cut the search from over 23 million nodes (stopped after 60 s) to 4.

- **A shift is a group, not a sequence.** {Yossi, Dana} and {Dana, Yossi} are the same staffing. Candidates are picked in increasing index order (`next`), so each group is tried exactly once: combinations, not permutations.
- **Fullest first.** Each shift is first tried with its `wanted` number of people, then one less, down to `min`. The first schedule found is therefore the most fully staffed one along that path.
- **Early cut-off.** If fewer candidates are left than people still needed, the loop stops at once.
- **Problem vs. State.** `Problem` (employees, shifts, rules) is read-only. `State` is the only thing that changes: who is assigned where, stored from both sides (`staffOf(shift)`, `shiftsOf(employee)`) so every rule check is a direct lookup.

### Hard rules implemented

| Rule | What it enforces |
|---|---|
| `no-overlap` | Nobody works two shifts at the same time (always on) |
| `min-rest` | At least `minRestHours` between two work blocks. Back-to-back shifts form one block |
| `double-needs-approval` | A block longer than `doubleAboveHours` is a double: allowed only if the employee marked `"double": true` in their availability, and never longer than `maxDoubleHours` |
| `max-shifts` | Personal limit per employee (a double counts as two shifts) |
| `max-consecutive-days` | Optional. A shift counts on the day it starts, so 5 night shifts Sun–Thu are 5 days, not 6 |

`minShifts` (an employee's wish for at least N shifts) is deliberately **not** a hard rule: a "must reach at least" rule can't be checked at each assignment, and one person's wish should not turn the answer into "no schedule". It is reported in the output today (`(below minimum 3)`) and becomes a penalty in the score in stage 5.

---

## Input

One JSON file describes the whole problem. Dates are real ISO dates, and shifts may cross midnight (`22:00-06:00`).

```json
{
  "period": { "start": "2026-11-08", "end": "2026-11-14" },
  "employees": [
    {
      "id": 102, "name": "Dana Levi", "roles": ["cashier"],
      "availability": [
        { "date": "2026-11-08", "time": "16:00-00:00", "type": "preferred" },
        { "date": "2026-11-10", "time": "08:00-00:00", "type": "available", "double": true }
      ]
    }
  ],
  "shifts": [
    { "id": "SUN-N", "date": "2026-11-08", "time": "22:00-06:00", "min": 1, "wanted": 1, "role": "guard" }
  ],
  "rules": { "minRestHours": 10, "maxConsecutiveDays": 5 }
}
```

- Availability types: `available`, `preferred`, `unavailable`. An entry without `time` covers the whole day.
- Availability is opt-in: no matching entry means the employee can't work that shift.
- Every field in `rules` is optional and has a default.
- Loading errors report the file, the JSON path, and the reason, and loading continues so all problems are shown at once (see `data/bad_example.json`).

### Availability from a spreadsheet

Availability can also come from a CSV file, which is what a manager usually exports from Excel. Employees are still defined in the JSON file; the CSV only adds availability lines to them.

```csv
employee_id,date,time,type,double
104,2026-11-09,08:00-00:00,available,yes
102,2026-11-10,,unavailable
```

- `time` may be empty (= the whole day). `double` is optional: `yes` approves a double shift.
- Header rows, `#` comments, blank lines, Excel's byte-order mark and `\r\n` line endings are all handled.
- Errors report the file and **line number** (`avail.csv:7: no employee with id 999`), and every bad line is reported in one run.

## Output

```
$ schedule_search data/example_week.json

Sun 2026-11-08
  SUN-M  08:00-16:00  2/2  Yossi Cohen, Noa Shapira
  SUN-E  16:00-00:00  1/1  Dana Levi
  SUN-N  22:00-06:00  1/1  Avi Mizrahi  [guard]

Mon 2026-11-09
  MON-M  08:00-16:00  1/2  Yossi Cohen  (below wanted)
  ...

Per employee
  Yossi Cohen      2 shifts  16 h   SUN-M MON-M
  Dana Levi        2 shifts  16 h   SUN-E TUE-M
  ...
```

Dana is available Monday morning, but she isn't assigned to it: she finished Sunday's evening shift at midnight, and 8 hours of rest is below the 10-hour minimum.

**Shifts nobody can work don't block the rest.** Before searching, the engine finds shifts that have fewer available people (with the right role) than their minimum. Those are impossible no matter what, so instead of answering "no schedule" for the whole month, it staffs them as well as it can, schedules everything else, and explains each hole:

```
  1208-N 23:00-07:00  0/1  -  [guard]  (BELOW MIN 1: see below)
...
Shifts that cannot be fully staffed (fill them by hand)
  1208-N 2026-12-08 23:00-07:00  needs 1, can get 0: 2 available, but only 0 with role 'guard'
```

`--verbose` also prints the loaded input, the candidates for each shift, and the number of search nodes.
`--time-limit <seconds>` (default 60) and `--max-nodes <n>` stop a search that takes too long.

Exit codes: `0` schedule found · `1` bad arguments or input · `2` valid input, and no schedule exists (the whole search space was checked) · `3` stopped by a limit before finding a schedule (one may still exist).

---

## Performance

Measured with `tools/benchmark.py` on a generated month (15 employees, 90 shifts, `data/month_15x90.json`), Release build, search time only:

| Case | Calendar-order DFS | Now |
|---|---|---|
| Solvable month | 330 nodes, 0.3 ms | 330 nodes, 3.6 ms |
| One night nobody can work (day 3) | 10,275,056 nodes, 26 s, "no schedule" | 329 nodes, 3.6 ms, **full schedule + the hole explained** |
| A hidden conflict on day 4 (impossible, but no single shift shows it) | over 23,900,000 nodes, stopped after 60 s | **4 nodes, 0.1 ms**, "no schedule" |

What made the difference: a static check before the search (shifts that can never be staffed), **MRV** (fill the hardest shift first, and stop as soon as any open shift can no longer reach its minimum), and a cache that, after each assignment, re-checks only the employee who changed. The price is a slightly more expensive node, which the solvable month barely notices.

Measure in **Release**: the same search is about 13× slower in a Debug build.

```bash
python3 tools/generate_problem.py --days 30 --employees 15 --seed 4 --work-rate 0.85 -o data/month.json
python3 tools/benchmark.py build/schedule_search data/month.json
```

## Build and run

Requires a C++17 compiler and CMake ≥ 3.20. Dependencies are downloaded automatically.

**Visual Studio 2022:** File → Open → Folder, and select this folder.

**Command line:**
```bash
cmake -S . -B build
cmake --build build
./build/schedule_search data/example_week.json [--availability data/availability_example.csv] [--verbose]
```

**Tests** (Catch2):
```bash
ctest --test-dir build
```

## Tech stack

- **C++17**, CMake
- [nlohmann/json](https://github.com/nlohmann/json) for parsing input
- [Catch2](https://github.com/catchorg/Catch2) for unit tests (time periods across midnight, dates, loader diagnostics, availability, state, every constraint, the solver on small cases with known answers)

## Project structure

```
include/            headers
  Date.h, TimePeriod.h, Employee.h, Shift.h, Rules.h, Problem.h   the problem (read-only)
  solver/           State, Solver, Constraint, Availability, WorkBlocks
  io/               JsonLoader, CsvLoader, Diagnostics, Printer
src/                implementations, same layout
tests/              Catch2 unit tests
data/               example inputs
PLAN.md             roadmap and status of each stage (Hebrew)
DECISIONS.md        design decisions D1–D27 and the reasoning behind them (Hebrew)
```

## Roadmap

| Stage | | Status |
|---|---|---|
| 0 | CMake, JSON loading, data model | ✅ |
| 1 | DFS with backtracking, min/wanted staffing | ✅ |
| 2 | Hard rules: overlap, rest, doubles, max shifts, consecutive days | ✅ |
| 3 | More flexible user input: CSV availability import, minimum shifts per employee (a soft wish) | ✅ |
| 4 | Pruning: time/node limits, static shortage check, most-constrained shift first (MRV) with dead-end detection, incremental cache | ✅ |
| 5 | **Optimization**: scoring with soft rules, Branch & Bound, top-N schedules | ⬜ |
| 6 | Reports, CSV export, explaining *why* no schedule exists | ⬜ |
| 7 | Manual edits: check a hand-edited schedule against all the rules and flag violations | ⬜ |
