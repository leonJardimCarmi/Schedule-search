#include "solver/Solver.h"

#include "solver/Availability.h"

Solver::Solver(const Problem& problem)
    : problem_(problem),
      candidates_(problem.shifts.size()),
      state_(static_cast<int>(problem.employees.size()), static_cast<int>(problem.shifts.size())) {
    // Availability never changes during the search, so it is checked once
    // here instead of millions of times inside dfs().
    for (std::size_t s = 0; s < problem.shifts.size(); ++s)
        for (std::size_t e = 0; e < problem.employees.size(); ++e)
            if (availabilityFit(problem.employees[e], problem.shifts[s]) != Fit::No)
                candidates_[s].push_back(static_cast<int>(e));
}

void Solver::reset() {
    state_ = State(static_cast<int>(problem_.employees.size()), static_cast<int>(problem_.shifts.size()));
    found_.reset();
    solutions_ = 0;
    nodes_ = 0;
}

std::optional<State> Solver::findFirst() {
    reset();
    stopAtFirst_ = true;
    dfs(0, 0);
    return found_;
}

long long Solver::countAll() {
    reset();
    stopAtFirst_ = false;
    dfs(0, 0);
    return solutions_;
}

// shiftIdx      - the shift being filled now
// nextCandidate - position in candidates_[shiftIdx] to continue from.
//                 Everyone before it was already decided for this shift,
//                 which is what makes the search pick combinations
//                 ({A,B}) and not permutations ({A,B} and {B,A}).
bool Solver::dfs(int shiftIdx, int nextCandidate) {
    ++nodes_;

    // 1. Every shift is staffed: this is a complete schedule.
    if (shiftIdx == static_cast<int>(problem_.shifts.size())) {
        ++solutions_;
        if (stopAtFirst_) {
            found_ = state_;   // copy it out before backtracking erases it
            return true;
        }
        return false;          // keep looking for more
    }

    // 2. This shift has enough people: move on to the next shift.
    const Shift& shift = problem_.shifts[shiftIdx];
    if (state_.assignedCount(shiftIdx) == shift.minStaff)
        return dfs(shiftIdx + 1, 0);

    // 3. Try each remaining candidate: assign, go deeper, undo.
    const std::vector<int>& candidates = candidates_[shiftIdx];
    for (int i = nextCandidate; i < static_cast<int>(candidates.size()); ++i) {
        const int emp = candidates[i];

        state_.assign(emp, shiftIdx);              // write in pencil
        const bool stop = dfs(shiftIdx, i + 1);    // same shift, later candidates only
        state_.unassign(emp, shiftIdx);            // erase

        if (stop) return true;
    }

    // 4. Ran out of candidates before reaching minStaff: dead end, go back.
    return false;
}
