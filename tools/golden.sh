#!/usr/bin/env bash
# The golden output of the console (DECISIONS D63): a fixed set of runs, and
# for each one its stdout, stderr and exit code, compared with the files in
# tests/golden/expected/. Any change in what the program prints shows up
# here, line by line. The runs named *_json print the JSON result to stdout
# (--json -, stage 8); CI also checks them against the contract with
# tools/check_contract.py.
#
#   bash tools/golden.sh <schedule_search>         run and compare; exit 1 on a difference
#   bash tools/golden.sh <schedule_search> --update   write tests/golden/expected/ again
#
# It runs from the repository root with relative paths, so no path of a
# particular machine appears in the output. Update only when a change in
# the output is intended, and read the diff before committing it.
set -u
if [ $# -lt 1 ]; then
    echo "usage: bash tools/golden.sh <path to schedule_search> [--update]" >&2
    exit 2
fi
exe=$1
case "$exe" in /*) ;; *) exe="$PWD/$exe" ;; esac
mode=${2:-compare}
cd "$(dirname "$0")/.."

IN=tests/golden/inputs
OUT=tests/golden/actual
EXPECTED=tests/golden/expected
mkdir -p "$OUT" "$EXPECTED"

names=()
run() {
    local name=$1
    shift
    names+=("$name")
    "$exe" "$@" > "$OUT/$name.out" 2> "$OUT/$name.err"
    echo $? > "$OUT/$name.code"
    # Search times change from run to run.
    sed -i -E 's/ in [0-9.e+-]+ ms/ in X ms/; s/"ms": [0-9.e+-]+/"ms": 0/' "$OUT/$name.out"
}

run 01_week            data/example_week.json
run 02_week_list       data/example_week.json --view list
run 03_month           data/month_15x90.json --max-nodes 200000
run 04_month_list      data/month_15x90.json --max-nodes 200000 --view list
run 05_week_first      data/example_week.json --first
run 06_month_first     data/month_15x90.json --first
run 07_week_verbose    data/example_week.json -v
run 08_week_mindiff1   data/example_week.json --min-diff 1
run 09_week_csv        data/example_week.json --availability data/availability_example.csv
run 10_holes           $IN/holes.json
run 11_holes_list      $IN/holes.json --view list
run 12_explain_name    data/example_week.json --explain "Dana Levi" MON-M
run 13_explain_id      data/example_week.json --explain 104 TUE-E
run 14_save            data/example_week.json --save $OUT/saved_week.json
run 15_check_clean     data/example_week.json --check $OUT/saved_week.json
run 16_check_broken    data/example_week.json --check $IN/broken.json
run 17_compare_broken  data/example_week.json --check $IN/broken.json --compare
run 18_weights         data/example_week.json --weight double=50 --weight below-min-shifts=0
run 19_trace           data/example_week.json --trace
run 20_month_week2     data/month_15x90.json --max-nodes 50000 --week 2
run 21_bad_input       data/bad_example.json
run 22_medics          data/medics_december.json --max-nodes 100000
run 23_month_color     data/month_15x90.json --max-nodes 50000 --color always
run 24_week_json       data/example_week.json --json -
run 25_week_md1_json   data/example_week.json --min-diff 1 --json -
run 26_week_first_json data/example_week.json --first --json -
run 27_month_json      data/month_15x90.json --max-nodes 50000 --top 2 --json -
run 28_holes_json      $IN/holes.json --json -
run 29_medics_json     data/medics_december.json --max-nodes 100000 --json -
run 30_bad_input_json  data/bad_example.json --json -
run 31_limit_json      data/month_15x90.json --max-nodes 5 --json -
run 32_impossible_json $IN/impossible.json --json -
run 33_impossible      $IN/impossible.json
run 34_week_json_file  data/example_week.json --json $OUT/week.json
run 35_cafe            data/cafe_december.json --max-nodes 100000

files=()
for name in "${names[@]}"; do files+=("$name.out" "$name.err" "$name.code"); done

if [ "$mode" = "--update" ]; then
    for f in "${files[@]}"; do cp "$OUT/$f" "$EXPECTED/$f"; done
    echo "golden: wrote ${#names[@]} runs to $EXPECTED"
    exit 0
fi

failed=0
# --json <file> leaves the console as it is (D57): the same text as run 01.
if ! diff -q "$OUT/01_week.out" "$OUT/34_week_json_file.out" > /dev/null; then
    echo "golden: --json <file> changed the console output (34 vs 01)"
    failed=1
fi
for f in "${files[@]}"; do
    if [ ! -f "$EXPECTED/$f" ]; then
        echo "golden: $EXPECTED/$f is missing (new run? use --update)"
        failed=1
    # Windows writes \r\n: the same output for this check (D63).
    elif ! diff -u --strip-trailing-cr "$EXPECTED/$f" "$OUT/$f"; then
        failed=1
    fi
done
for f in "$EXPECTED"/*; do
    case " ${files[*]} " in *" $(basename "$f") "*) ;; *) echo "golden: $f belongs to no run (remove it)"; failed=1 ;; esac
done
lines=$(cat "${files[@]/#/$OUT/}" | wc -l)
if [ $failed -eq 0 ]; then
    echo "golden: ${#names[@]} runs ($lines lines) match"
else
    echo "golden: the output differs from $EXPECTED (see above)"
fi
exit $failed
