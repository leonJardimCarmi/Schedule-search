#include "solver/WorkBlocks.h"

#include <algorithm>

std::vector<WorkBlock> buildBlocks(const Problem& problem, std::vector<int> shifts, int maxGapMinutes) {
    // 1. Time order. `shifts` is taken by value, so sorting it here does not
    //    change the caller's list.
    std::sort(shifts.begin(), shifts.end(), [&](int a, int b) {
        return problem.shifts[a].absoluteStart() < problem.shifts[b].absoluteStart();
    });

    // 2. Walk through them: a shift that starts close enough to the end of
    //    the current block joins it, otherwise it opens a new block.
    std::vector<WorkBlock> blocks;
    for (int s : shifts) {
        const Shift& shift = problem.shifts[s];
        if (!blocks.empty() && shift.absoluteStart() - blocks.back().end <= maxGapMinutes) {
            WorkBlock& current = blocks.back();
            current.end = std::max(current.end, shift.absoluteEnd());
            current.shifts.push_back(s);
        } else {
            blocks.push_back(WorkBlock{shift.absoluteStart(), shift.absoluteEnd(), {s}});
        }
    }
    return blocks;
}

WorkBlock blockWith(const Problem& problem, const State& state, int emp, int shift, int maxGapMinutes) {
    std::vector<int> shifts = state.shiftsOf(emp);
    shifts.push_back(shift);

    for (const WorkBlock& block : buildBlocks(problem, shifts, maxGapMinutes))
        if (std::find(block.shifts.begin(), block.shifts.end(), shift) != block.shifts.end())
            return block;
    return WorkBlock{};   // not reached: `shift` is always in one of the blocks
}
