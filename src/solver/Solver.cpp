#include "solver/Solver.h"

#include <algorithm>
#include <cassert>

#include "solver/Availability.h"
#include "solver/WorkBlocks.h"

Solver::Solver(const Problem& problem)
    : problem_(problem),
      candidates_(problem.shifts.size()),
      order_(problem.shifts.size()),
      searchMin_(problem.shifts.size()),
      prefers_(problem.shifts.size(), std::vector<char>(problem.employees.size(), 0)),
      filled_(problem.shifts.size(), 0),
      constraints_(makeConstraints(problem)),
      state_(static_cast<int>(problem.employees.size()), static_cast<int>(problem.shifts.size())) {
    // Availability and role never change during the search, so they are
    // checked once here instead of millions of times inside the search.
    for (std::size_t s = 0; s < problem.shifts.size(); ++s)
        for (std::size_t e = 0; e < problem.employees.size(); ++e) {
            const Employee& employee = problem.employees[e];
            const Shift& shift = problem.shifts[s];
            const Fit fit = availabilityFit(employee, shift);
            if (fit != Fit::No && roleFits(employee, shift))
                candidates_[s].push_back(static_cast<int>(e));
            prefers_[s][e] = fit == Fit::Preferred;
        }

    // Where each employee is a candidate, for refreshEmployee().
    shiftsOfCandidate_.resize(problem.employees.size());
    allowedNow_.resize(problem.shifts.size());
    allowedCount_.resize(problem.shifts.size());
    for (std::size_t s = 0; s < problem.shifts.size(); ++s) {
        allowedNow_[s].resize(candidates_[s].size());
        for (std::size_t pos = 0; pos < candidates_[s].size(); ++pos)
            shiftsOfCandidate_[candidates_[s][pos]].push_back({static_cast<int>(s), static_cast<int>(pos)});
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
    improvements_ = 0;
    pruned_ = 0;
    bestScore_ = Score{};
    nodes_ = 0;
    limitHit_ = false;
    startTime_ = std::chrono::steady_clock::now();
    refreshAll();   // nobody is assigned yet: the cache starts from scratch
}

// Recomputes the cached "allowed?" answers of ONE employee, for every shift
// they are a candidate for, and adjusts the per-shift counts.
// About 20 checks instead of all shifts x all candidates.
void Solver::refreshEmployee(int emp) {
    for (const CandidateSlot& slot : shiftsOfCandidate_[emp]) {
        const char now = allowed(emp, slot.shift) ? 1 : 0;
        char& before = allowedNow_[slot.shift][slot.pos];
        allowedCount_[slot.shift] += now - before;
        before = now;
    }
}

void Solver::refreshAll() {
    for (std::size_t s = 0; s < problem_.shifts.size(); ++s) {
        std::fill(allowedNow_[s].begin(), allowedNow_[s].end(), 0);
        allowedCount_[s] = 0;
    }
    for (int e = 0; e < static_cast<int>(problem_.employees.size()); ++e) refreshEmployee(e);
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
    mode_ = Mode::First;
    nextShift();
    return found_;
}

std::optional<State> Solver::findBest(const ScoreModel& model) {
    reset();
    mode_ = Mode::Best;
    model_ = &model;
    nextShift();
    model_ = nullptr;
    return found_;
}

long long Solver::countAll() {
    reset();
    mode_ = Mode::Count;
    nextShift();
    return solutions_;
}

bool Solver::cannotBeatBest() const {
    if (mode_ != Mode::Best || !useBound_ || !found_) return false;
    const SearchView view{filled_, candidates_, allowedNow_, allowedCount_, searchMin_};
    return !(model_->lowerBound(problem_, state_, view) < bestScore_);
}

// A complete schedule was reached. Returns true to stop the search.
bool Solver::onSchedule() {
    ++solutions_;
    switch (mode_) {
    case Mode::First:
        found_ = state_;   // copy it out before backtracking erases it
        return true;
    case Mode::Count:
        return false;      // keep looking for more
    case Mode::Best: {
        // Basic Branch & Bound, without the "bound" yet (step 5.3): score
        // every schedule, keep the best, and go on looking.
        const Score score = model_->score(problem_, state_);
        if (!found_ || score < bestScore_) {
            found_ = state_;
            bestScore_ = score;
            ++improvements_;
        }
        return false;
    }
    }
    return false;
}

// Decides in which order to try this shift's candidates (DECISIONS D16, D36):
//   1. people for whom this shift would NOT be a double, so a double is used
//      only when there is no other way;
//   2. inside each group, people who marked the shift "preferred".
// A good first schedule gives the bound (step 5.3) a strong target early.
// stable_sort keeps the input order among equals, so the search stays
// predictable.
//
// It is computed when entering the shift, from the current state. Adding one
// employee to this shift does not change whether it is a double for another,
// so the order stays valid while this shift is being filled.
void Solver::orderCandidates(int shiftIdx) {
    const int doubleAbove = problem_.rules.doubleAboveHours * 60;
    const int gap = problem_.rules.blockGapMinutes;

    // Rank: 0 = no double + preferred, 1 = no double, 2 = double + preferred, 3 = double.
    // Computed once per candidate: blockWith() is too costly to call inside the sort.
    rank_.resize(problem_.employees.size());
    for (int emp : candidates_[shiftIdx]) {
        const bool isDouble = blockWith(problem_, state_, emp, shiftIdx, gap).minutes() > doubleAbove;
        rank_[emp] = (isDouble ? 2 : 0) + (prefers_[shiftIdx][emp] ? 0 : 1);
    }
    std::vector<int>& order = order_[shiftIdx];
    order = candidates_[shiftIdx];
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rank_[a] < rank_[b]; });
}

// The slow way: ask every rule about every candidate. Used only to check
// the cache in debug builds (see chooseShift).
int Solver::countAllowedSlowly(int shift) const {
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
        // Debug builds double-check the cache against the slow count. If a
        // future rule breaks D27 (looks at other employees), this fires.
        assert(allowedCount(s) == countAllowedSlowly(s) && "MRV cache out of date (D27)");
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
    if (filledCount_ == static_cast<int>(problem_.shifts.size())) return onSchedule();

    // Branch & Bound (step 5.3): if even the best possible completion of
    // this branch is not better than the best schedule found, skip it.
    if (cannotBeatBest()) {
        ++pruned_;
        return false;
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
        refreshEmployee(emp);                               // only emp's answers changed
        const bool stop = fill(shiftIdx, i + 1, target);    // same shift, later candidates only
        state_.unassign(emp, shiftIdx);                     // erase
        refreshEmployee(emp);

        if (stop) return true;
    }
    return false;
}
