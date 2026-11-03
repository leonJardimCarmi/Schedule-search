#include "solver/Solver.h"

#include <algorithm>

#include "solver/Availability.h"

Solver::Solver(const Problem& problem)
    : problem_(problem),
      candidates_(problem.shifts.size()),
      constraints_(makeConstraints(problem)),
      state_(static_cast<int>(problem.employees.size()), static_cast<int>(problem.shifts.size())) {
    // Availability never changes during the search, so it is checked once
    // here instead of millions of times inside dfs().
    for (std::size_t s = 0; s < problem.shifts.size(); ++s)
        for (std::size_t e = 0; e < problem.employees.size(); ++e)
            if (availabilityFit(problem.employees[e], problem.shifts[s]) != Fit::No)
                candidates_[s].push_back(static_cast<int>(e));
}

bool Solver::allowed(int emp, int shift) const {
    for (const auto& constraint : constraints_)
        if (!constraint->allows(problem_, state_, emp, shift)) return false;
    return true;
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
    startShift(0);
    return found_;
}

long long Solver::countAll() {
    reset();
    stopAtFirst_ = false;
    startShift(0);
    return solutions_;
}

// Entering a new shift: decide how many people it gets.
// Tries the largest size first (wanted), then one less, down to min.
bool Solver::startShift(int shiftIdx) {
    ++nodes_;

    // Every shift is staffed: this is a complete schedule.
    if (shiftIdx == static_cast<int>(problem_.shifts.size())) {
        ++solutions_;
        if (stopAtFirst_) {
            found_ = state_;   // copy it out before backtracking erases it
            return true;
        }
        return false;          // keep looking for more
    }

    const Shift& shift = problem_.shifts[shiftIdx];
    const int candidateCount = static_cast<int>(candidates_[shiftIdx].size());

    // No point aiming for more people than there are candidates.
    const int largest = std::min(shift.wantedStaff, candidateCount);

    for (int target = largest; target >= shift.minStaff; --target)
        if (fill(shiftIdx, 0, target)) return true;

    // Not even minStaff could be reached: dead end, go back.
    return false;
}

// Choosing exactly `target` people for one shift, as a combination.
//
// nextCandidate - position in candidates_[shiftIdx] to continue from.
//                 Everyone before it was already decided for this shift,
//                 which is what makes the search pick combinations
//                 ({A,B}) and not permutations ({A,B} and {B,A}).
bool Solver::fill(int shiftIdx, int nextCandidate, int target) {
    ++nodes_;

    const int needed = target - state_.assignedCount(shiftIdx);
    if (needed == 0) return startShift(shiftIdx + 1);   // this shift is done

    const std::vector<int>& candidates = candidates_[shiftIdx];
    for (int i = nextCandidate; i < static_cast<int>(candidates.size()); ++i) {
        // Fewer candidates left than people still needed: no combination
        // from here can reach the target, so stop trying.
        if (static_cast<int>(candidates.size()) - i < needed) break;

        const int emp = candidates[i];
        if (!allowed(emp, shiftIdx)) continue;              // a hard rule says no

        state_.assign(emp, shiftIdx);                       // write in pencil
        const bool stop = fill(shiftIdx, i + 1, target);    // same shift, later candidates only
        state_.unassign(emp, shiftIdx);                     // erase

        if (stop) return true;
    }
    return false;
}
