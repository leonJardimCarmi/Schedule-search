#pragma once
#include <optional>
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
};

// Builds the report of `schedule`. `top` is the solver's list of good
// schedules (top[0] is `schedule` itself); pass an empty list for none.
Report buildReport(const Problem& problem, const State& schedule, const std::vector<Shortage>& shortages,
                   const ScoreModel& model, const std::vector<RankedSchedule>& top);
