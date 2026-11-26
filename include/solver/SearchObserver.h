#pragma once
#include <vector>

#include "solver/Score.h"
#include "solver/State.h"

// Someone who wants to watch the search step by step (DECISIONS D48).
//
// The Solver reports what it does through these calls: which shift MRV chose,
// who was assigned and erased again, where a branch ended and why. It does
// not know what the observer does with them: --trace prints them
// (TracePrinter), a test can just record them.
//
// The Solver holds a pointer that is null by default. Without an observer
// each step costs one pointer check and nothing else, the same idea as the
// `why` pointer of Constraint::check (D47).
//
// `depth` = how many shifts were already decided when the step happened,
// so the first shift chosen is at depth 0. Every method does nothing by
// default: an observer overrides only what it cares about.
class SearchObserver {
public:
    virtual ~SearchObserver() = default;

    // Why MRV (D26) picked this shift: it had the least slack among the open ones.
    struct ShiftChoice {
        int shift;                          // index into Problem::shifts
        int allowed;                        // candidates the hard rules allow now
        int min;                            // the min the search aims for (D25)
        int slack;                          // allowed - min
        int open;                           // shifts still undecided, this one included
        const std::vector<int>* candidates; // everyone available for it (allowed or not)
    };

    // MRV chose the next shift to fill.
    virtual void shiftChosen(const State& /*state*/, int /*depth*/, const ShiftChoice& /*choice*/) {}
    // The shift with the least slack cannot reach its min any more: go back.
    virtual void deadEnd(const State& /*state*/, int /*depth*/, const ShiftChoice& /*choice*/) {}
    // The search now tries to staff the shift with exactly `target` people (D10).
    virtual void sizeTried(int /*depth*/, int /*shift*/, int /*target*/) {}
    virtual void assigned(int /*depth*/, int /*emp*/, int /*shift*/) {}
    // Backtracking: the assignment is erased again.
    virtual void unassigned(int /*depth*/, int /*emp*/, int /*shift*/) {}
    // Branch & Bound (step 5.3): even the best completion of this branch,
    // `bound`, is not better than `toBeat` (the last schedule in the list).
    virtual void pruned(int /*depth*/, const Score& /*bound*/, const Score& /*toBeat*/) {}
    // Every shift is decided. With findBest, `score` is its score and
    // `place` its place in the list of best schedules (0 = the new best),
    // or -1 if it did not get in. Otherwise `score` is null and `place` -1.
    virtual void scheduleFound(int /*depth*/, const Score* /*score*/, int /*place*/) {}

    // Once every 1024 nodes, where the search checks its limits (stage 8.4,
    // D67): a heartbeat for --progress. `nodes` = nodes visited so far.
    virtual void tick(long long /*nodes*/) {}
};
