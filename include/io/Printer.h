#pragma once
#include <optional>
#include <ostream>
#include <string>
#include <vector>

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

// The schedule: by day, then a summary per employee (--view list).
void printSchedule(std::ostream& out, const Problem& problem, const Report& report);

// The schedule as one table per week (--view grid, the default; D42):
// shift kinds down, Sunday..Saturday across. `onlyWeek` (1-based) prints
// one week. `colors` adds ANSI colors (D45): red below min, yellow below
// wanted, green preferred. Off by default, so files and tests get plain text.
void printWeeks(std::ostream& out, const Problem& problem, const Report& report, std::optional<int> onlyWeek,
                bool colors = false);
void printPeriodSummary(std::ostream& out, const Problem& problem, const Report& report);

// The answer to --explain (step 6.4): availability, role and every hard
// rule, each with its reason, then a one-line answer.
void printExplanation(std::ostream& out, const Problem& problem, const State& schedule, const Explanation& e);

// --check (stage 7.2, D51): each violation of a hand-made schedule, with its
// reason, and a note for each shift with more people than it wants.
void printViolations(std::ostream& out, const Problem& problem, const CheckReport& check);

// --check --compare (stage 7.3, D52): the score rule by rule, mine against
// the engine's, then every shift that differs. `proven`: the engine's
// schedule is proven the best (the search was not stopped by a limit).
// `myViolations`: how many hard rules the hand-made one breaks. The score
// does not count them (the engine never breaks one), so a schedule that
// breaks rules can score better; the verdict says so.
void printComparison(std::ostream& out, const Problem& problem, const ComparisonReport& comparison, bool proven,
                     std::size_t myViolations);

// Short names for narrow table cells: the first name, and when two people
// share it, also the first letter of the last name ("Noa A.", "Noa L.").
// If even that is shared, the full name.
std::vector<std::string> shortNames(const Problem& problem);

// Shifts that can never reach their min (DECISIONS D25), each with the reason.
void printShortages(std::ostream& out, const Problem& problem, const Report& report);

// "Medium 0, Soft 182".
std::string scoreText(const Score& score);

// The schedule's score and where it comes from, one line per soft rule.
// Lower is better; Medium is compared before Soft (D29).
void printScore(std::ostream& out, const Report& report);

// The other good schedules (step 5.4): one summary each, with its score and
// how it differs from the best (who leaves and who joins which shift).
void printAlternatives(std::ostream& out, const Problem& problem, const Report& report, int minDifferent);
