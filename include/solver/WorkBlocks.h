#pragma once
#include <vector>

#include "Problem.h"
#include "solver/State.h"

// A stretch of continuous work by one employee (DECISIONS D14): one shift,
// or several shifts with at most `blockGapMinutes` between them.
struct WorkBlock {
    int start;                 // absolute minutes (see Shift::absoluteStart)
    int end;
    std::vector<int> shifts;   // shift indices, in time order

    int minutes() const { return end - start; }
};

// Groups the given shifts into work blocks, in time order.
// The input may be in any order.
std::vector<WorkBlock> buildBlocks(const Problem& problem, std::vector<int> shifts, int maxGapMinutes);

// The block that `shift` would be part of if `emp` also worked it.
// Used to ask "would this assignment make a double?" before making it.
WorkBlock blockWith(const Problem& problem, const State& state, int emp, int shift, int maxGapMinutes);
