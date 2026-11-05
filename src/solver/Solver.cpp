#include "solver/Solver.h"

#include <algorithm>

#include "solver/Availability.h"
#include "solver/WorkBlocks.h"

Solver::Solver(const Problem& problem)
    : problem_(problem),
      candidates_(problem.shifts.size()),
      order_(problem.shifts.size()),
      constraints_(makeConstraints(problem)),
      state_(static_cast<int>(problem.employees.size()), static_cast<int>(problem.shifts.size())) {
    // Availability and role never change during the search, so they are
    // checked once here instead of millions of times inside the search.
    for (std::size_t s = 0; s < problem.shifts.size(); ++s)
        for (std::size_t e = 0; e < problem.employees.size(); ++e) {
            const Employee& employee = problem.employees[e];
            const Shift& shift = problem.shifts[s];
            if (availabilityFit(employee, shift) != Fit::No && roleFits(employee, shift))
                candidates_[s].push_back(static_cast<int>(e));
        }
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

// Decides in which order to try this shift's candidates (DECISIONS D16):
// people for whom this shift would NOT be a double come first, so a double
// is used only when there is no other way. stable_partition keeps the
// original order inside each group, so the search stays predictable.
//
// It is computed when entering the shift, from the current state. Adding one
// employee to this shift does not change whether it is a double for another,
// so the order stays valid while this shift is being filled.
void Solver::orderCandidates(int shiftIdx) {
    const int doubleAbove = problem_.rules.doubleAboveHours * 60;
    const int gap = problem_.rules.blockGapMinutes;

    std::vector<int>& order = order_[shiftIdx];
    order = candidates_[shiftIdx];
    std::stable_partition(order.begin(), order.end(), [&](int emp) {
        return blockWith(problem_, state_, emp, shiftIdx, gap).minutes() <= doubleAbove;
    });
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
    orderCandidates(shiftIdx);

    // No point aiming for more people than there are candidates.
    const int largest = std::min(shift.wantedStaff, candidateCount);

    for (int target = largest; target >= shift.minStaff; --target)
        if (fill(shiftIdx, 0, target)) return true;

    // Not even minStaff could be reached: dead end, go back.
    return false;
}

// Choosing exactly `target` people for one shift, as a combination.
//
// nextCandidate - position in order_[shiftIdx] to continue from.
//                 Everyone before it was already decided for this shift,
//                 which is what makes the search pick combinations
//                 ({A,B}) and not permutations ({A,B} and {B,A}).
bool Solver::fill(int shiftIdx, int nextCandidate, int target) {
    ++nodes_;

    const int needed = target - state_.assignedCount(shiftIdx);
    if (needed == 0) return startShift(shiftIdx + 1);   // this shift is done

    const std::vector<int>& candidates = order_[shiftIdx];
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
