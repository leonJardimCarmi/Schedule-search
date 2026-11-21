#pragma once
#include <ostream>
#include <string>
#include <vector>

#include "Problem.h"
#include "solver/Constraint.h"
#include "solver/SearchObserver.h"

// --trace (step 6.5, DECISIONS D48): prints the search step by step.
//
// One level of indentation per shift already decided. Under each shift MRV
// chose: the sizes tried, who is assigned (+) and erased again (-), and
// the shifts chosen inside that branch. Example:
//
//   SUN-M: 2 allowed, min 1, slack 1 (the least of 14 open); blocked: Avi (no-overlap)
//     try 2 people
//     + Dana
//     + Yossi
//     SUN-E: ...
//
// After `maxLines` lines it prints nothing more, but keeps counting, and
// finish() says how many steps were left out. The search itself goes on.
class TracePrinter : public SearchObserver {
public:
    TracePrinter(std::ostream& out, const Problem& problem, const ConstraintList& constraints, int maxLines);

    void shiftChosen(const State& state, int depth, const ShiftChoice& choice) override;
    void deadEnd(const State& state, int depth, const ShiftChoice& choice) override;
    void sizeTried(int depth, int shift, int target) override;
    void assigned(int depth, int emp, int shift) override;
    void unassigned(int depth, int emp, int shift) override;
    void pruned(int depth, const Score& bound, const Score& toBeat) override;
    void scheduleFound(int depth, const Score* score, int place) override;

    // The last line: the whole search, or where the trace was cut.
    void finish();

    long long linesPrinted() const { return lines_; }
    long long stepsLeftOut() const { return leftOut_; }

    // Deeper than this, the indentation stops growing and the line shows
    // the depth as a number instead, so a month does not run off the screen.
    static constexpr int kMaxIndentLevels = 20;

private:
    // Starts a line at this depth, or returns false when the limit is reached.
    bool startLine(int depth);
    // "Avi (no-overlap), Dana (min-rest)": every candidate a hard rule blocks now.
    std::string blocked(const State& state, const ShiftChoice& choice) const;
    std::string choiceText(const ShiftChoice& choice) const;

    std::ostream& out_;
    const Problem& problem_;
    const ConstraintList& constraints_;
    std::vector<std::string> names_;   // short names, as in the table (D44)
    int maxLines_;
    long long lines_ = 0;
    long long leftOut_ = 0;
    long long schedules_ = 0;
};
