#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Problem.h"
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

// Builds the report of `schedule`. `top` is the solver's list of good
// schedules (top[0] is `schedule` itself); pass an empty list for none.
Report buildReport(const Problem& problem, const State& schedule, const std::vector<Shortage>& shortages,
                   const ScoreModel& model, const std::vector<RankedSchedule>& top);
