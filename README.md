# Schedule Search

A C++ engine that builds the **best possible work schedule** for a business: who works which shift, given each employee's availability and the business's own rules.

The rules are not hard-coded. Each business describes its week in a JSON file: the shifts, how many people each shift needs, who is available when, and which rules apply (rest between shifts, consecutive days, double shifts…). The engine searches the space of possible schedules with **depth-first search (DFS) and backtracking**.

> **Status:** the search and all hard rules work (stages 0–2 of the [plan](PLAN.md)). Today the engine returns the first legal schedule it finds. Scoring and finding the *best* schedule (stage 5) are next on the roadmap.

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
startShift(s)                          fill(s, next, target)
  ├─ all shifts done? → schedule found   ├─ shift has `target` people? → startShift(s + 1)
  ├─ order candidates (non-doubles first)└─ for each candidate i ≥ next:
  └─ for target = wanted … min:               if every Constraint allows:
        fill(s, 0, target)                       assign → fill(s, i + 1, target) → unassign
```

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

`--verbose` also prints the loaded input, the candidates for each shift, and the number of search nodes.
Exit codes: `0` schedule found · `1` bad arguments or input · `2` valid input but no schedule exists.

---

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
DECISIONS.md        design decisions D1–D21 and the reasoning behind them (Hebrew)
```

## Roadmap

| Stage | | Status |
|---|---|---|
| 0 | CMake, JSON loading, data model | ✅ |
| 1 | DFS with backtracking, min/wanted staffing | ✅ |
| 2 | Hard rules: overlap, rest, doubles, max shifts, consecutive days | ✅ |
| 3 | More flexible user input: CSV availability import ✅, minimum shifts per employee | 🔶 |
| 4 | Pruning: most-constrained shift first, forward checking, node and time limits | ⬜ |
| 5 | **Optimization**: scoring with soft rules, Branch & Bound, top-N schedules | ⬜ |
| 6 | Reports, CSV export, explaining *why* no schedule exists | ⬜ |
| 7 | Manual edits: check a hand-edited schedule against all the rules and flag violations | ⬜ |
