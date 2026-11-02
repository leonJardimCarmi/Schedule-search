#pragma once
#include <ostream>

#include "Problem.h"
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
