#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Problem.h"
#include "solver/Availability.h"
#include "solver/Constraint.h"
#include "solver/Score.h"
#include "solver/Solver.h"
#include "solver/State.h"

// Everything the program shows about a result, computed once (DECISIONS D41).
//
// The model holds facts only: indices into Problem, counts, scores. Turning
// an index into a name, choosing columns and colors is the job of a view
// (today Printer for the console; later JSON, CSV or an app). So every view
// shows the same numbers, and tests check the numbers without parsing text.

// One shift in the schedule.
struct ShiftReport {
    enum class Status { Full, BelowWanted, BelowMin };
    int shift;                 // index into Problem::shifts
    std::vector<int> staff;    // employee indices, in the order they were assigned
    Status status;
    // One flag per person in `staff`, same order (step 6.3, D42):
    std::vector<char> preferred;   // they marked this shift "preferred"
    std::vector<char> inDouble;    // this shift is part of a double for them (D16)
    std::vector<char> breaksRule;  // --check (D51): this assignment has a violation; all 0 otherwise
};

// A kind of shift = a row of the weekly table (step 6.3): the same hours and
// the same role, e.g. "07:00-15:00" or "23:00-07:00 [guard]".
struct ShiftKind {
    TimePeriod time;
    std::string role;   // empty = any role
};

// One full week, Sunday to Saturday (D42). Days outside the period stay empty.
struct WeekReport {
    Date sunday;
    // [kind][weekday] -> shift indices (almost always 0 or 1 shift per cell)
    std::vector<std::array<std::vector<int>, 7>> cells;
    int shifts = 0;        // shifts in this week
    int full = 0;          // ... staffed up to wanted
    int belowWanted = 0;   // ... at or above min, below wanted
    int belowMin = 0;      // ... below min (only the holes of D25)
    std::vector<int> shiftsPerEmployee;   // [employee] -> shifts this week
};

// One employee over the whole period.
struct EmployeeReport {
    int employee;
    std::vector<int> shifts;           // shift indices, in calendar order
    int minutes;                       // total working time
    std::optional<int> belowMinShifts; // their minShifts, only if they got fewer (D22)
};

// A shift that can never reach its min (D25), and why.
struct ShortageReport {
    enum class Reason {
        NobodyAvailable,   // no one is available at that time
        MissingRole,       // people are available, but too few have the role
        TooFewAvailable    // not enough people are available at all
    };
    int shift;
    int needed;       // its min
    int candidates;   // available and with the role
    int available;    // available, role or not
    Reason reason;
};

// How one other good schedule differs from the best (step 5.4).
struct ShiftChange {
    int shift;
    std::vector<int> out;   // in the best schedule, not in this one
    std::vector<int> in;    // in this one, not in the best
};
struct AlternativeReport {
    Score score;
    int differences;                   // differentAssignments() with the best
    std::vector<ShiftChange> changes;  // every changed shift, in calendar order
};

struct Report {
    std::vector<ShiftReport> shifts;           // calendar order, every shift
    std::vector<EmployeeReport> employees;     // input order, every employee
    std::vector<ShortageReport> shortages;     // empty when everything can be staffed
    Score score;
    std::vector<ScoreLine> scoreLines;         // one per soft rule
    std::vector<AlternativeReport> alternatives;   // the other schedules of the top list, best first

    // The weekly view (step 6.3, D42).
    std::vector<ShiftKind> kinds;   // sorted by start time, then end time, then role
    std::vector<int> kindOf;        // [shift] -> index into kinds
    std::vector<WeekReport> weeks;  // week 1 is the week the period starts in
};

// --explain (step 6.4, D43, D47): could `employee` also work `shift`, in the
// schedule that was found? Each check answers on its own, so the user sees
// every reason at once, not only the first.
struct RuleCheck {
    std::string rule;                    // e.g. "min-rest"
    std::optional<std::string> whyNot;   // empty = allowed
};
struct Explanation {
    int employee;
    int shift;
    bool alreadyWorks;              // they work it in the schedule; the rules are checked without it
    Fit availability;               // No / Yes / Preferred
    bool roleOk;
    std::vector<RuleCheck> rules;   // every hard rule, in the solver's order
    std::vector<int> staff;         // who works the shift in the schedule
    bool canWork() const;           // available, has the role, and every rule allows it
};
Explanation explainAssignment(const Problem& problem, const State& schedule, const ConstraintList& rules,
                              int employee, int shift);

// --check (stage 7.2, D51): what a hand-made schedule breaks. Nothing is
// blocked (D18): every assignment stays, and each problem is listed.
struct Violation {
    enum class Kind { Availability, Role, Rule };
    int employee;
    int shift;
    Kind kind;
    std::string rule;   // Kind::Rule: the hard rule's name, e.g. "min-rest"
    std::string why;    // Kind::Rule: the rule's own reason (D47)
};
// More people than the shift wants: not a rule, wanted is a target. A note only.
struct OverStaffed {
    int shift;
    int staff;
    int wanted;
};
struct CheckReport {
    std::vector<Violation> violations;   // by the shift's start time
    std::vector<OverStaffed> overStaffed;
    bool clean() const { return violations.empty(); }
};

// Each assignment is checked against the ones that start before it, the way
// the search assigns. So a rule that involves two shifts (an overlap, too
// little rest) is reported once, at the later shift (D51). For each hard
// rule, the earlier assignments that already broke that rule are left out,
// so one violation is not found again at every later shift.
CheckReport checkSchedule(const Problem& problem, const State& schedule, const ConstraintList& rules);

// --check --compare (stage 7.3, D52): a hand-made schedule next to the
// schedule the engine finds for the same problem.
struct RuleComparison {
    std::string rule;
    Level level;
    int weight;
    long long mine;     // weighted penalty of the hand-made schedule
    long long engine;   // ... of the engine's
};
struct ComparisonReport {
    Score mine;
    Score engine;
    std::vector<RuleComparison> rules;   // every soft rule, in the model's order
    int differences;                     // differentAssignments()
    // Every shift that differs, in calendar order. `out` = only in the
    // hand-made schedule, `in` = only in the engine's: what to change in
    // the hand-made one to get the engine's.
    std::vector<ShiftChange> changes;
};
ComparisonReport compareSchedules(const Problem& problem, const State& mine, const State& engine,
                                  const ScoreModel& model);

// Builds the report of `schedule`. `top` is the solver's list of good
// schedules (top[0] is `schedule` itself); pass an empty list for none.
// `check` (--check) marks the assignments that break a rule.
Report buildReport(const Problem& problem, const State& schedule, const std::vector<Shortage>& shortages,
                   const ScoreModel& model, const std::vector<RankedSchedule>& top,
                   const CheckReport* check = nullptr);
