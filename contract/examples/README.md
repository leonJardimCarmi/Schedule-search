# Example results

Results that `schedule_search --json` wrote, saved for anyone who works from
the contract without running the engine (the first screen of the app, D54).
`tools/check_contract.py` checks them against `../result.schema.json` in CI,
so they can never fall behind the contract unnoticed.

**The main example is the café** (D65): it was built so that one file shows
everything an app has to display. The medics are an edge case: two rows of
24-hour shifts, and a recipe made for a far site.

| File | Made from | Command |
|---|---|---|
| `cafe_december.result.json` | `data/cafe_december.json`, written by `tools/make_showcase.py`: a café over three weeks of December, 12 employees, each a different kind of availability, 7 kinds of shifts, and the three reasons a shift cannot be staffed. Made-up names | `schedule_search data/cafe_december.json --max-nodes 1000000 --time-limit 600 --json contract/examples/cafe_december.result.json` |
| `medics_december.result.json` | `data/medics_december.json`: 7 medics in December, 24-hour shifts, the far-site recipe, made-up names (D60) | `schedule_search data/medics_december.json --max-nodes 3000000 --time-limit 600 --json contract/examples/medics_december.result.json` |
| `example_week.result.json` | `data/example_week.json`: the small example week, with five different schedules | `schedule_search data/example_week.json --min-diff 1 --json contract/examples/example_week.result.json` |

**Why a node limit and not a time limit:** the search stops after exactly the
same steps on every machine, so the command gives the same file again (only
`run.ms` changes). For the café, 300,000 and 3,000,000 nodes give the same
best score (Medium 4 for the four holes, Soft 585), so 1,000,000 is plenty. For the medics, 3,000,000 nodes take about 50 seconds;
8,000,000 nodes found the same best schedule (Soft 362: `fairness` 322,
`fewer-trips` 40), not proven best (`"complete": false`).

When the contract changes, run the commands again from the repository root.
