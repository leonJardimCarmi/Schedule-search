#pragma once
#include <optional>
#include <vector>

#include "Problem.h"
#include "solver/Constraint.h"
#include "solver/State.h"

// Depth-first search with backtracking over the shifts, in chronological order.
//
// Stage 1.4: each shift gets between `minStaff` and `wantedStaff` employees.
// Larger staffings are tried first (DECISIONS D10); fewer than minStaff is
// a dead end (D11). An employee is tried only if available for the shift
// and every hard rule in constraints() allows it (stage 2).
class Solver {
public:
    explicit Solver(const Problem& problem);

    // The first legal schedule found, or nothing if none exists.
    std::optional<State> findFirst();

    // How many different legal schedules exist (explores the whole tree).
    long long countAll();

    // How many times dfs() was called in the last search: a measure of work.
    long long nodesVisited() const { return nodes_; }

    // Employees whose availability fits each shift, computed once.
    const std::vector<int>& candidatesOf(int shift) const { return candidates_[shift]; }

    // The hard rules this solver enforces.
    const ConstraintList& constraints() const { return constraints_; }

private:
    // Both return true when the search should stop.
    bool startShift(int shiftIdx);
    bool fill(int shiftIdx, int nextCandidate, int target);
    void reset();
    bool allowed(int emp, int shift) const;   // every constraint says yes

    const Problem& problem_;
    std::vector<std::vector<int>> candidates_;   // [shift] -> employee indices
    ConstraintList constraints_;

    State state_;
    bool stopAtFirst_ = true;
    std::optional<State> found_;
    long long solutions_ = 0;
    long long nodes_ = 0;
};
