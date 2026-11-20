#pragma once
#include <ostream>

#include "Problem.h"
#include "report/ReportModel.h"
#include "solver/Score.h"
#include "solver/Solver.h"
#include "solver/State.h"

// Human-readable output. Everything takes an std::ostream, so the same code
// can write to the console (std::cout), to a file, or to a string in tests.

// The loaded input, to check it was read correctly (--verbose).
void printProblem(std::ostream& out, const Problem& problem);

// Who could work each shift, before any assignment (--verbose).
void printCandidates(std::ostream& out, const Problem& problem, const Solver& solver);

// The result, from the report model (D41). These only display.

// The schedule: by day, then a summary per employee.
void printSchedule(std::ostream& out, const Problem& problem, const Report& report);

// Shifts that can never reach their min (DECISIONS D25), each with the reason.
void printShortages(std::ostream& out, const Problem& problem, const Report& report);

// The schedule's score and where it comes from, one line per soft rule.
// Lower is better; Medium is compared before Soft (D29).
void printScore(std::ostream& out, const Report& report);

// The other good schedules (step 5.4): one summary each, with its score and
// how it differs from the best (who leaves and who joins which shift).
void printAlternatives(std::ostream& out, const Problem& problem, const Report& report, int minDifferent);
