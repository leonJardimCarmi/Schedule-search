#pragma once
#include <optional>
#include <vector>

#include "Problem.h"
#include "solver/State.h"

// Depth-first search with backtracking over the shifts, in chronological order.
//
// Stage 1.3: every shift is staffed with exactly `minStaff` employees,
// chosen only by availability. Other rules come in later stages.
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

private:
    // Returns true when the search should stop.
    bool dfs(int shiftIdx, int nextCandidate);
    void reset();

    const Problem& problem_;
    std::vector<std::vector<int>> candidates_;   // [shift] -> employee indices

    State state_;
    bool stopAtFirst_ = true;
    std::optional<State> found_;
    long long solutions_ = 0;
    long long nodes_ = 0;
};
