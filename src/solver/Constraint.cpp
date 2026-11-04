#include "solver/Constraint.h"

#include "solver/WorkBlocks.h"

bool NoOverlapConstraint::allows(const Problem& problem, const State& state, int emp, int shift) const {
    const Shift& candidate = problem.shifts[shift];

    // Compare with every shift this employee already has. absoluteStart/End
    // put all shifts on one timeline, so overnight shifts need no special case.
    for (int other : state.shiftsOf(emp)) {
        const Shift& taken = problem.shifts[other];
        const bool overlap = candidate.absoluteStart() < taken.absoluteEnd() &&
                             taken.absoluteStart() < candidate.absoluteEnd();
        if (overlap) return false;
    }
    return true;
}

bool MinRestConstraint::allows(const Problem& problem, const State& state, int emp, int shift) const {
    // The employee's shifts as they would be with the new one added.
    std::vector<int> shifts = state.shiftsOf(emp);
    shifts.push_back(shift);

    // Rest is measured between consecutive blocks, never inside one.
    const std::vector<WorkBlock> blocks = buildBlocks(problem, shifts, blockGapMinutes_);
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const int rest = blocks[i].start - blocks[i - 1].end;
        if (rest < minRestMinutes_) return false;
    }
    return true;
}

ConstraintList makeConstraints(const Problem& problem) {
    ConstraintList list;
    list.push_back(std::make_unique<NoOverlapConstraint>());
    list.push_back(std::make_unique<MinRestConstraint>(problem.rules.minRestHours, problem.rules.blockGapMinutes));
    // More rules from problem.rules are added here in the next steps.
    return list;
}
