#include "solver/Solver.h"

#include <algorithm>

#include "solver/Availability.h"
#include "solver/WorkBlocks.h"

Solver::Solver(const Problem& problem)
    : problem_(problem),
      candidates_(problem.shifts.size()),
      order_(problem.shifts.size()),
      searchMin_(problem.shifts.size()),
      filled_(problem.shifts.size(), 0),
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

    // Static check (DECISIONS D25): a shift with fewer candidates than its
    // min can never be fully staffed, no matter what the search does.
    // Searching for it anyway would only make the search try every
    // combination of the shifts before it, and fail every time.
    // So it is recorded, and the search aims for as many as possible.
    for (std::size_t s = 0; s < problem.shifts.size(); ++s) {
        const int available = static_cast<int>(candidates_[s].size());
        const int needed = problem.shifts[s].minStaff;
        searchMin_[s] = std::min(needed, available);
        if (available < needed)
            shortages_.push_back(Shortage{static_cast<int>(s), available, needed});
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
    std::fill(filled_.begin(), filled_.end(), 0);
    filledCount_ = 0;
    solutions_ = 0;
    nodes_ = 0;
    limitHit_ = false;
    startTime_ = std::chrono::steady_clock::now();
}

// Called once per node. Returns true when the search must stop.
bool Solver::limitReached() {
    ++nodes_;
    if (limitHit_) return true;

    if (limits_.maxNodes && nodes_ > *limits_.maxNodes) {
        limitHit_ = true;
    } else if (limits_.seconds && nodes_ % 1024 == 0) {
        // Reading the clock costs more than a whole node, so it is done
        // only once every 1024 nodes: a few microseconds late at most.
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - startTime_;
        if (elapsed.count() >= *limits_.seconds) limitHit_ = true;
    }
    return limitHit_;
}

SearchStatus Solver::status() const {
    if (found_) return SearchStatus::Found;
    return limitHit_ ? SearchStatus::LimitReached : SearchStatus::NoSchedule;
}

std::optional<State> Solver::findFirst() {
    reset();
    stopAtFirst_ = true;
    nextShift();
    return found_;
}

long long Solver::countAll() {
    reset();
    stopAtFirst_ = false;
    nextShift();
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

// How many of this shift's candidates the hard rules allow right now.
int Solver::allowedCount(int shift) const {
    int count = 0;
    for (int emp : candidates_[shift])
        if (allowed(emp, shift)) ++count;
    return count;
}

// MRV (DECISIONS D26): among the shifts not decided yet, pick the one with
// the least "slack" = allowed candidates minus its min. Slack 0 means there is
// exactly one way left to staff it, so do it now; negative slack means it can
// no longer be staffed at all. Ties go to the earliest shift (calendar order).
Solver::Choice Solver::chooseShift() const {
    Choice best{-1, 0};
    for (int s = 0; s < static_cast<int>(problem_.shifts.size()); ++s) {
        if (filled_[s]) continue;
        const int slack = allowedCount(s) - searchMin_[s];
        if (best.shift == -1 || slack < best.slack) best = {s, slack};
        if (best.slack < 0) break;   // already a dead end: no need to look further
    }
    return best;
}

// Choosing the next shift and how many people it gets.
// Tries the largest size first (wanted), then one less, down to min (D10).
bool Solver::nextShift() {
    if (limitReached()) return true;   // "stop": unwinds the whole search

    // Every shift is decided: this is a complete schedule.
    if (filledCount_ == static_cast<int>(problem_.shifts.size())) {
        ++solutions_;
        if (stopAtFirst_) {
            found_ = state_;   // copy it out before backtracking erases it
            return true;
        }
        return false;          // keep looking for more
    }

    const Choice choice = chooseShift();
    if (choice.slack < 0) return false;   // some shift can no longer reach its min

    const int shiftIdx = choice.shift;
    const Shift& shift = problem_.shifts[shiftIdx];
    const int candidateCount = static_cast<int>(candidates_[shiftIdx].size());
    orderCandidates(shiftIdx);

    // No point aiming for more people than there are candidates.
    const int largest = std::min(shift.wantedStaff, candidateCount);

    filled_[shiftIdx] = 1;     // decided in this branch (undone below)
    ++filledCount_;
    bool stop = false;
    for (int target = largest; target >= searchMin_[shiftIdx] && !stop; --target)
        stop = fill(shiftIdx, 0, target);
    filled_[shiftIdx] = 0;
    --filledCount_;

    // stop == false: not even min could be reached here, go back.
    return stop;
}

// Choosing exactly `target` people for one shift, as a combination.
//
// nextCandidate - position in order_[shiftIdx] to continue from.
//                 Everyone before it was already decided for this shift,
//                 which is what makes the search pick combinations
//                 ({A,B}) and not permutations ({A,B} and {B,A}).
bool Solver::fill(int shiftIdx, int nextCandidate, int target) {
    if (limitReached()) return true;

    const int needed = target - state_.assignedCount(shiftIdx);
    if (needed == 0) return nextShift();   // this shift is done: choose the next one

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
