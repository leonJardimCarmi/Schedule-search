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

By default the schedule is shown as one table per week (shift kinds down, Sunday to Saturday across), followed by a summary of the whole period. `--week <n>` shows one week; `--view list` shows the older list by day. In the table: `*` the employee preferred the shift, `D` part of a double, `! -1` one person below wanted, `!! -1` below min. On a terminal these are also colored (green, yellow, red); `--color always|never` overrides that, and so does the `NO_COLOR` environment variable.

To ask why someone is not in a shift: `--explain <employee> <shift>` (employee by id or full name, shift by id). It checks availability, the role and every hard rule against the schedule found, and shows each reason, e.g.

```
schedule_search data/example_week.json --explain "Dana Levi" MON-M
  min-rest              BLOCKED  only 8h of rest between SUN-E and MON-M, needs 10h
```

To watch the search itself: `--trace` prints it step by step (first 200 lines; `--trace-lines <n>` for more), one level of indentation per shift already decided. Each line is one step: the shift MRV chose and why (allowed, min, slack, and who is blocked by which rule), the number of people tried, who is assigned (`+`) and erased again on backtracking (`-`), dead ends, branches cut by the bound, and each complete schedule with its score. Meant for small inputs:

```
schedule_search data/example_week.json --first --trace
MON-M: 1 allowed, min 1, slack 0 (the least of 6 open); blocked: Dana (min-rest)
  MON-M: try 2 people
  + Yossi
  - Yossi
  MON-M: try 1 person
```

`--save <file.json>` also writes the schedule found to a file, one shift per line: `{"assignments": {"SUN-M": [104, 101], "SUN-E": [102]}}` (employees by id; when writing one by hand, a full name works too). This is also the format `--check` reads.

To check a schedule made or edited by hand: `schedule_search problem.json --check my_schedule.json`. There is no search: the schedule is shown in the same table with the same score, every assignment that breaks availability, a role or a hard rule is marked ` x` in its cell, and a list below the score gives each one with its reason. Nothing is blocked: a manager may make exceptions, the program only points them out. Each violation is listed once, at the later of the shifts involved, and a shift with more people than it wants gets a note. `--explain` works against the hand-made schedule too.

Add `--compare` to also run the search and set the two side by side: the penalty of every soft rule in yours and in the engine's, both scores, and every shift that differs (`out:` only in yours, `in:` only in the engine's). Search options work with `--compare`, and `--save` then saves the engine's schedule. The score does not count hard rules (the engine never breaks one), so when a hand-made schedule scores better only by breaking rules, the verdict says that instead of calling it better.

```
Rule violations (4): nothing was blocked, each is listed at the later of the shifts involved
  MON-M      Dana Levi          min-rest              only 8h of rest between SUN-E and MON-M, needs 10h
  MON-N      Yossi Cohen        availability          not available for 2026-11-09 22:00-06:00
  MON-N      Yossi Cohen        role                  does not have the role 'guard'
  MON-N      Yossi Cohen        min-rest              only 6h of rest between MON-M and MON-N, needs 10h
  note: TUE-E has 2 people, it wants 1
```

`--verbose` also prints the loaded input, the candidates for each shift, and the number of search nodes.
`--time-limit <seconds>` (default 60) and `--max-nodes <n>` stop a search that takes too long.

By default the program looks for the **best** schedule: every legal schedule gets a score (lower is better), printed with one line per soft rule. The weights can be changed in the input:

```json
"weights": { "double": 8, "not-preferred": 0 }
```

Weights can also be changed for one run, without editing the file: `--weight fairness=0 --weight double=8` (overrides `"weights"` in the file).

Soft rules: `below-min` (Medium level: holes that can't be filled) · `below-wanted` (10) · `double` (5) · `below-min-shifts` (5) · `not-preferred` (1) · `consecutive-nights` (3; a night is a shift with at least `nightMinHours` inside `nightWindow`, both in `rules`) · `fairness` (1; how uneven the number of shifts per employee is, in squares) · `fewer-trips` (0 = off; for a far site, where every arrival is a long trip: a trip is a run of days in a row, and each day it falls short of `tripMinDays` or goes past `tripMaxDays` counts, both in `rules`, default 3..7. A short trip at the very start or end of the period is not counted, since it may go on outside it. Such a business usually also sets `double` and `consecutive-nights` to 0). Any difference at the Medium level decides before the Soft level is compared. A weight of `0` turns a rule off.
If the time limit stops the search, the best schedule found so far is printed, with a note that a better one may exist. `--first` skips the optimization and prints the first legal schedule.

The program also lists the next best schedules (`--top <n>`, default 5), each differing from the others in at least `--min-diff <k>` assignments (default 3), with a short summary of who leaves and who joins which shift. Keeping a longer list means less pruning, so with a time limit the best schedule found may be a little worse; `--top 1` gives the fastest search for a single schedule.

### Recipe: a far site with 24-hour shifts

For example, medics who travel 4 hours each way and stay on site for several days, working 13:00-13:00 shifts back to back. Two things have to change, and neither needs new code:

1. **Several days in a row must not count as a double.** Back-to-back 24-hour shifts form one long work block (D14), and a block longer than `doubleAboveHours` is a double, which needs one availability line with `"double": true` covering the whole block (D16). Availability is filled in day by day, so no line covers several days, and the engine falls back to "a day on, a day off". Raise both limits to the longest stay, here a week:

   ```json
   "rules": { "minRestHours": 10, "doubleAboveHours": 168, "maxDoubleHours": 168,
              "tripMinDays": 3, "tripMaxDays": 7 }
   ```
   If there is a real upper limit on days in a row, add it as a hard rule too (`"maxConsecutiveDays": 5`).

2. **Prefer long trips over single days.** Turn `fewer-trips` on, and turn off the two rules that would punish exactly what it asks for:

   ```json
   "weights": { "fewer-trips": 20, "double": 0, "consecutive-nights": 0 }
   ```
   or, for one run: `--weight fewer-trips=20 --weight double=0 --weight consecutive-nights=0`.

On a real month of this kind (7 medics, 62 shifts, 20 s search), the days outside the 3..7 ideal fell from 174 (everyone a day on, a day off) to 2. While `fewer-trips` is on, the search also tries first, inside each shift, someone who works the day before or after (after "no double", before "preferred"), and its bound knows when a trip can no longer grow long enough, so it prunes. Fairness did not suffer here (346 with the rule off, 322 with it on), but the two can pull against each other, so tune their weights.

**JSON for apps** (stage 8, DECISIONS D57–D62): `--json <file>` also writes the whole result as JSON, and `--json -` writes only the JSON, to stdout. It holds the problem (employees and shifts), the layout of the weekly table, the shifts that cannot be staffed, every schedule of the top list in full, and how the search went. Every field is described in [`contract/CONTRACT.md`](contract/CONTRACT.md); the exact shape is `contract/result.schema.json`, and `tools/check_contract.py` checks a file against it:
```bash
./build/schedule_search data/medics_december.json --json result.json
python3 tools/check_contract.py result.json      # needs: pip install "jsonschema>=4.18"
```
A JSON is written also when there is no schedule (`"status": "noSchedule"` or `"limitReached"`) or when the input has errors (`"inputError"`, with the errors in `"diagnostics"`).

Exit codes: `0` schedule found (with a note if it is the best found in time, not proven best) · `1` bad arguments or input · `2` valid input, and no schedule exists (the whole search space was checked) · `3` stopped by a limit before finding a schedule (one may still exist) · `4` (`--check`) the schedule breaks a rule.

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

**Golden output** (DECISIONS D63): 23 fixed runs of the program, and for each one its stdout, stderr and exit code, compared with `tests/golden/expected/`:
```bash
bash tools/golden.sh build/schedule_search            # compare; exit 1 on a difference
bash tools/golden.sh build/schedule_search --update   # after an intended change: write it again, then read the diff
```

**Real data** (D60): the repository is public, so files with real names stay in `data/private/`, which Git ignores. `tools/anonymize.py` writes a copy with made-up names, and the engine finds the same schedule on it:
```bash
python3 tools/anonymize.py data/private/real.json data/demo.json
```
`data/medics_december.json` is such a copy: 7 medics in December, 24-hour shifts, with the far-site recipe above.

**The main example** (D65): `data/cafe_december.json`, written by `python3 tools/make_showcase.py`: a café over three weeks of December, 12 employees with every kind of availability the engine knows (evenings only, weekends only, mornings only, night guards, a vacation, exam days, a split day, a new hire, approved doubles, preferred shifts, minimum and maximum shifts), 7 kinds of shifts (a Friday-only row, a night across midnight, a one-day event), and one shift for each reason it cannot be staffed. Its result as JSON is `contract/examples/cafe_december.result.json`.

## Tech stack

- **C++17**, CMake
- [nlohmann/json](https://github.com/nlohmann/json) for parsing input
- [Catch2](https://github.com/catchorg/Catch2) for unit tests (time periods across midnight, dates, loader diagnostics, availability, state, every constraint, the solver on small cases with known answers)

## Project structure

```
include/            headers
  Date.h, TimePeriod.h, Employee.h, Shift.h, Rules.h, Problem.h   the problem (read-only)
  solver/           State, Solver, Constraint, Availability, WorkBlocks
  io/               JsonLoader, CsvLoader, Diagnostics, Printer, ResultJson
src/                implementations, same layout
tests/              Catch2 unit tests
  golden/           the golden output: inputs/ and expected/ (tools/golden.sh)
data/               example inputs (data/private/: real data, not in Git)
tools/              generate_problem.py, benchmark.py, anonymize.py, golden.sh, check_contract.py, make_showcase.py
contract/           the JSON result for apps: result.schema.json, examples/ (stage 8)
PLAN.md             roadmap and status of each stage (Hebrew)
DECISIONS.md        design decisions D1–D63 and the reasoning behind them (Hebrew)
```

## Roadmap

| Stage | | Status |
|---|---|---|
| 0 | CMake, JSON loading, data model | ✅ |
| 1 | DFS with backtracking, min/wanted staffing | ✅ |
| 2 | Hard rules: overlap, rest, doubles, max shifts, consecutive days | ✅ |
| 3 | More flexible user input: CSV availability import, minimum shifts per employee (a soft wish) | ✅ |
| 4 | Pruning: time/node limits, static shortage check, most-constrained shift first (MRV) with dead-end detection, incremental cache | ✅ |
| 5 | **Optimization**: pluggable soft-rule scoring, best-so-far search (anytime), Branch & Bound with per-rule lower bounds, top-N distinct schedules | ✅ |
| 6 | Output and tools to explore the engine: a table per week with colors, `--explain` (why someone can't work a shift), `--trace` (the search step by step), `--weight` (CSV/HTML export postponed) | ✅ |
| 7 | Manual edits: `--save` a schedule, `--check` a hand-made one against every rule (marked, not blocked), and `--compare` it with the engine's | ✅ |
| 8 | The contract for apps: the whole result as JSON (`--json`), with a JSON Schema and a checker | 🔶 |

## Continuous integration

Every push is built and tested on Windows, Linux and macOS by GitHub Actions (`.github/workflows/build.yml`): a Release build with CMake, all unit tests with `ctest`, the golden output (`tools/golden.sh`), the JSON results against the contract (on Linux), and one run of the program on the example week. The results are in the repository's **Actions** tab.
