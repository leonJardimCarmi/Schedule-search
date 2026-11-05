#include "solver/Constraint.h"

#include <set>

#include "solver/WorkBlocks.h"

namespace {

// Did the employee mark "double": true on a slot that covers the whole block?
bool approvedDouble(const Employee& employee, const WorkBlock& block) {
    for (const AvailabilitySlot& slot : employee.availability) {
        if (!slot.allowsDouble) continue;

        // The slot on the same absolute timeline as the block.
        // A slot without time covers its whole day.
        const int dayStart = slot.date.daysSinceEpoch() * TimePeriod::MINUTES_PER_DAY;
        const int slotStart = dayStart + (slot.time ? slot.time->getStart() : 0);
        const int slotEnd = dayStart + (slot.time ? slot.time->getEnd() : TimePeriod::MINUTES_PER_DAY);

        if (slotStart <= block.start && block.end <= slotEnd) return true;
    }
    return false;
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

bool DoubleShiftConstraint::allows(const Problem& problem, const State& state, int emp, int shift) const {
    const WorkBlock block = blockWith(problem, state, emp, shift, blockGapMinutes_);

    if (block.minutes() <= doubleAboveMinutes_) return true;    // not a double
    if (block.minutes() > maxDoubleMinutes_) return false;      // too long, even with approval
    return approvedDouble(problem.employees[emp], block);       // a double: needs approval
}

bool MaxShiftsConstraint::allows(const Problem& problem, const State& state, int emp, int /*shift*/) const {
    const std::optional<int>& limit = problem.employees[emp].maxShifts;
    if (!limit) return true;   // no personal limit
    return static_cast<int>(state.shiftsOf(emp).size()) < *limit;
}

bool MaxConsecutiveDaysConstraint::allows(const Problem& problem, const State& state, int emp, int shift) const {
    // 1. Every day the employee would work, as day numbers. A shift counts
    //    on the day it starts, also a night shift (DECISIONS D13).
    std::set<int> workDays;
    for (int other : state.shiftsOf(emp)) workDays.insert(problem.shifts[other].date.daysSinceEpoch());
    const int day = problem.shifts[shift].date.daysSinceEpoch();
    workDays.insert(day);

    // 2. The run of consecutive days that contains the new shift: start at
    //    its day and grow in both directions while the next day is worked.
    //    Other runs were already checked when they were built.
    int first = day;
    int last = day;
    while (workDays.count(first - 1)) --first;
    while (workDays.count(last + 1)) ++last;

    return last - first + 1 <= maxDays_;
}

ConstraintList makeConstraints(const Problem& problem) {
    ConstraintList list;
    list.push_back(std::make_unique<NoOverlapConstraint>());
    const Rules& rules = problem.rules;
    list.push_back(std::make_unique<MinRestConstraint>(rules.minRestHours, rules.blockGapMinutes));
    list.push_back(std::make_unique<DoubleShiftConstraint>(rules.doubleAboveHours, rules.maxDoubleHours,
                                                           rules.blockGapMinutes));
    list.push_back(std::make_unique<MaxShiftsConstraint>());

    // Optional rules: added only when the input sets them.
    if (rules.maxConsecutiveDays)
        list.push_back(std::make_unique<MaxConsecutiveDaysConstraint>(*rules.maxConsecutiveDays));
    // More rules from problem.rules are added here in the next steps.
    return list;
}
