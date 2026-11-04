#include "solver/Constraint.h"

namespace {

// Does shift `s` count on calendar day `day`? (DECISIONS D13)
bool touchesDay(const Shift& s, const Date& day) {
    return s.date <= day && day <= s.lastDay();
}

}  // namespace

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

bool MaxShiftsPerDayConstraint::allows(const Problem& problem, const State& state, int emp, int shift) const {
    const Shift& candidate = problem.shifts[shift];

    // Check every day the new shift touches: one day, or two for a night shift.
    for (Date day = candidate.date; day <= candidate.lastDay(); day = day + 1) {
        int count = 0;
        for (int other : state.shiftsOf(emp))
            if (touchesDay(problem.shifts[other], day)) ++count;

        if (count + 1 > max_) return false;   // +1 for the shift being added
    }
    return true;
}

ConstraintList makeConstraints(const Problem& problem) {
    ConstraintList list;
    list.push_back(std::make_unique<NoOverlapConstraint>());
    list.push_back(std::make_unique<MaxShiftsPerDayConstraint>(problem.rules.maxShiftsPerDay));
    // More rules from problem.rules are added here in the next steps.
    return list;
}
