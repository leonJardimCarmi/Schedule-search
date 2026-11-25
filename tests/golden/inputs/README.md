Inputs used only by `tools/golden.sh` (DECISIONS D63).

- `holes.json`: the example week plus three shifts that can never reach their
  `min`, one for each reason (D25): nobody available (`WED-M`), nobody with
  the role (`MON-G`), and too few available (`THU-M`).
- `broken.json`: a hand-made schedule for the example week that breaks
  availability, a role and the rest rule, and over-staffs a shift, for
  `--check` and `--compare` (D51, D52).
- `impossible.json`: two overlapping shifts and one worker. Each shift alone
  has someone, so the check before the search finds nothing, but no schedule
  exists (exit code 2, `"status": "noSchedule"` in the JSON, D61).
