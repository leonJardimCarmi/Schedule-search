#include "solver/Constraint.h"

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

ConstraintList makeConstraints(const Problem& /*problem*/) {
    ConstraintList list;
    list.push_back(std::make_unique<NoOverlapConstraint>());
    // Rules from problem.rules are added here in the next steps.
    return list;
}
