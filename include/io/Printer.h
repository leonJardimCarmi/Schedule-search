#pragma once
#include <ostream>

#include "Problem.h"
#include "solver/Score.h"
#include "solver/Solver.h"
#include "solver/State.h"

// Human-readable output. Everything takes an std::ostream, so the same code
// can write to the console (std::cout), to a file, or to a string in tests.

// The loaded input, to check it was read correctly (--verbose).
void printProblem(std::ostream& out, const Problem& problem);

// Who could work each shift, before any assignment (--verbose).
void printCandidates(std::ostream& out, const Problem& problem, const Solver& solver);

// The schedule: by day, then a summary per employee.
void printSchedule(std::ostream& out, const Problem& problem, const State& schedule);

// Shifts that can never reach their min (DECISIONS D25), each with the reason:
// nobody available that day, or available people without the shift's role.
void printShortages(std::ostream& out, const Problem& problem, const Solver& solver);

// The schedule's score and where it comes from, one line per soft rule
// (stage 5). Lower is better; Medium is compared before Soft (D29).
void printScore(std::ostream& out, const Problem& problem, const ScoreModel& model, const State& schedule);

// The other good schedules (step 5.4): one summary each, with its score and
// how it differs from the best (who leaves and who joins which shift).
void printAlternatives(std::ostream& out, const Problem& problem, const std::vector<RankedSchedule>& top,
                       int minDifferent);
