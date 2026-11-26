#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <vector>

#include "Problem.h"
#include "solver/Constraint.h"
#include "solver/Score.h"
#include "solver/SearchObserver.h"
#include "solver/State.h"

// Depth-first search with backtracking over the shifts.
//
// Shift order (DECISIONS D26, "MRV"): at every step the search fills the
// shift that is hardest right now: the fewest employees the rules still
// allow, compared with its min. If some shift can no longer reach its min,
// the branch is a dead end at once, without filling anything else first.
//
// Stage 1.4: each shift gets between `minStaff` and `wantedStaff` employees.
// Larger staffings are tried first (DECISIONS D10); fewer than minStaff is
// a dead end (D11). An employee is tried only if available for the shift
// and every hard rule in constraints() allows it (stage 2).
// When to give up (DECISIONS D23). Empty = no limit of that kind.
struct SearchLimits {
    std::optional<double> seconds;
    std::optional<long long> maxNodes;
    // Stop on request (stage 8.4, D67): when this becomes true, the search
    // stops at its next check, as if a limit was reached (anytime, D31).
    // Set from a signal handler or another thread, so it is atomic.
    const std::atomic<bool>* stop = nullptr;
};

// Why the search stopped before it checked everything (D67).
enum class StopReason { None, Time, Nodes, Request };

// A shift that can never reach its min: fewer employees are available for
// it (and have its role) than it needs (DECISIONS D25). Found once, before
// the search. Such a shift is staffed with as many people as possible and
// reported, instead of making the whole answer "no schedule".
struct Shortage {
    int shift;        // index into Problem::shifts
    int candidates;   // how many could work it
    int needed;       // its minStaff
};

// One schedule in the list of good schedules (step 5.4).
struct RankedSchedule {
    State schedule;
    Score score;
};

// How the last search ended.
enum class SearchStatus {
    Found,          // a schedule was found (findBest: see complete())
    NoSchedule,     // the whole tree was searched: no schedule exists
    LimitReached    // stopped by a limit before an answer (D24)
};

class Solver {
public:
    explicit Solver(const Problem& problem);

    void setLimits(const SearchLimits& limits) { limits_ = limits; }

    // The first legal schedule found, or nothing. status() tells whether
    // "nothing" means "none exists" or "stopped by a limit".
    std::optional<State> findFirst();
    SearchStatus status() const;

    // The legal schedule with the lowest score (stage 5, D2). Keeps
    // searching after each schedule it finds. If a limit stops it, the best
    // one found so far is returned (anytime, D31) and complete() is false.
    std::optional<State> findBest(const ScoreModel& model);

    // Keep the n best schedules instead of only one (D39). Two schedules in
    // the list differ in at least `minDifferent` assignments; a new schedule
    // that is too close to one in the list replaces it only if it is better.
    // Default: n = 1 (just the best).
    void setTop(int n, int minDifferent) { topN_ = std::max(1, n); minDifferent_ = std::max(1, minDifferent); }
    // After findBest: best first. top()[0] is the schedule findBest returned.
    const std::vector<RankedSchedule>& top() const { return top_; }
    const Score& bestScore() const { return bestScore_; }
    long long improvements() const { return improvements_; }   // how often the best changed
    long long pruned() const { return pruned_; }               // branches cut by the bound
    // The bound before anything is assigned: no schedule can score better.
    // The gap between it and bestScore() is how far from proven we are.
    const Score& startBound() const { return startBound_; }

    // Branch & Bound pruning (step 5.3), on by default. Turning it off gives
    // the same best score, only slower: the tests use that to check the bound.
    void setUseBound(bool on) { useBound_ = on; }

    // Someone to tell about every step of the search (D48), e.g. --trace.
    // Null (the default) = nobody. The Solver does not own the observer.
    void setObserver(SearchObserver* observer) { observer_ = observer; }

    // True when the last search explored everything it had to: with
    // findBest, the schedule returned is then proven to be the best.
    bool complete() const { return !limitHit_; }
    StopReason stopReason() const { return stopReason_; }

    // How many different legal schedules exist (explores the whole tree).
    // If a limit stops it, the count is partial and status() says so.
    long long countAll();

    // How many times dfs() was called in the last search: a measure of work.
    long long nodesVisited() const { return nodes_; }

    // Employees whose availability fits each shift, computed once.
    const std::vector<int>& candidatesOf(int shift) const { return candidates_[shift]; }

    // The hard rules this solver enforces.
    const ConstraintList& constraints() const { return constraints_; }

    // Shifts that cannot be staffed to their min, whatever else happens (D25).
    const std::vector<Shortage>& shortages() const { return shortages_; }

private:
    // Both return true when the search should stop.
    bool nextShift();
    bool onSchedule();
    struct Choice { int shift; int slack; };
    Choice chooseShift() const;
    int allowedCount(int shift) const { return allowedCount_[shift]; }   // cached, see below
    int countAllowedSlowly(int shift) const;   // the same, recomputed from scratch
    void refreshEmployee(int emp);             // update the cache after emp's shifts changed
    void refreshAll();
    bool fill(int shiftIdx, int nextCandidate, int target);
    void reset();
    void orderCandidates(int shiftIdx);
    bool allowed(int emp, int shift) const;   // every constraint says yes
    bool limitReached();                      // counts a node, checks the limits

    const Problem& problem_;
    std::vector<std::vector<int>> candidates_;   // [shift] -> employee indices
    std::vector<std::vector<int>> order_;        // [shift] -> candidates in the order to try
    std::vector<int> searchMin_;                 // [shift] -> min used by the search (D25)
    std::vector<Shortage> shortages_;
    std::vector<int> rank_;                      // scratch for orderCandidates()
    std::vector<std::vector<char>> prefers_;     // [shift][emp] -> marked the shift "preferred"
    std::vector<char> filled_;                   // [shift] -> already decided in this branch
    int filledCount_ = 0;

    // Cache for MRV (step 4.4). Every hard rule looks only at the shifts of
    // the employee it is asked about (DECISIONS D27). So when an employee is
    // assigned or unassigned, only THAT employee's "allowed?" answers can
    // change, and only for the shifts they are a candidate for.
    struct CandidateSlot { int shift; int pos; };            // pos in candidates_[shift]
    std::vector<std::vector<CandidateSlot>> shiftsOfCandidate_;  // [emp] -> where they are a candidate
    std::vector<std::vector<char>> allowedNow_;              // [shift][pos] -> rules allow it now
    std::vector<int> allowedCount_;                          // [shift] -> how many are allowed now
    ConstraintList constraints_;
    const bool tripsOn_;   // fewer-trips has a weight above 0: orderCandidates prefers continuing a trip (D49)

    State state_;
    // What to do with a complete schedule (one leaf, three uses).
    enum class Mode { First, Count, Best };
    Mode mode_ = Mode::First;
    const ScoreModel* model_ = nullptr;   // set only in Mode::Best
    Score bestScore_;
    long long improvements_ = 0;
    long long pruned_ = 0;
    Score startBound_;
    bool useBound_ = true;
    bool cannotBeatBest(Score* bound) const;
    int offer(const Score& score);   // its place in top_, or -1
    int topN_ = 1;
    int minDifferent_ = 1;
    std::vector<RankedSchedule> top_;      // best first, at most topN_
    std::optional<State> found_;
    long long solutions_ = 0;
    long long nodes_ = 0;

    SearchObserver* observer_ = nullptr;

    SearchLimits limits_;
    std::chrono::steady_clock::time_point startTime_;
    bool limitHit_ = false;
    StopReason stopReason_ = StopReason::None;
};
