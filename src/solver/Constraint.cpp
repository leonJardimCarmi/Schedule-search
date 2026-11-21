#include "solver/Constraint.h"

#include <set>
#include <string>

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

namespace {

// "8h" or "7h30", for reasons.
std::string hoursText(int minutes) {
    std::string text = std::to_string(minutes / 60) + "h";
    if (minutes % 60) text += (minutes % 60 < 10 ? "0" : "") + std::to_string(minutes % 60);
    return text;
}

// Writes the reason only if someone asked for it (`why` is null in the search).
bool blocked(std::string* why, const std::string& reason) {
    if (why) *why = reason;
    return false;
}

}  // namespace

bool NoOverlapConstraint::check(const Problem& problem, const State& state, int emp, int shift,
                                std::string* why) const {
    const Shift& candidate = problem.shifts[shift];

    // Compare with every shift this employee already has. absoluteStart/End
    // put all shifts on one timeline, so overnight shifts need no special case.
    for (int other : state.shiftsOf(emp)) {
        const Shift& taken = problem.shifts[other];
        const bool overlap = candidate.absoluteStart() < taken.absoluteEnd() &&
                             taken.absoluteStart() < candidate.absoluteEnd();
        if (overlap)
            return why ? blocked(why, "overlaps " + taken.id + " (" + taken.date.toString() + " " +
                                          taken.time.toString() + ")")
                       : false;
    }
    return true;
}

bool MinRestConstraint::check(const Problem& problem, const State& state, int emp, int shift,
                              std::string* why) const {
    // The employee's shifts as they would be with the new one added.
    std::vector<int> shifts = state.shiftsOf(emp);
    shifts.push_back(shift);

    // Rest is measured between consecutive blocks, never inside one.
    const std::vector<WorkBlock> blocks = buildBlocks(problem, shifts, blockGapMinutes_);
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const int rest = blocks[i].start - blocks[i - 1].end;
        if (rest < minRestMinutes_) {
            if (!why) return false;
            // Blocks keep their shifts in time order: the last of one block
            // and the first of the next are the two that are too close.
            const Shift& before = problem.shifts[blocks[i - 1].shifts.back()];
            const Shift& after = problem.shifts[blocks[i].shifts.front()];
            return blocked(why, "only " + hoursText(rest) + " of rest between " + before.id + " and " + after.id +
                                    ", needs " + hoursText(minRestMinutes_));
        }
    }
    return true;
}

bool DoubleShiftConstraint::check(const Problem& problem, const State& state, int emp, int shift,
                                  std::string* why) const {
    const WorkBlock block = blockWith(problem, state, emp, shift, blockGapMinutes_);
    if (block.minutes() <= doubleAboveMinutes_) return true;    // not a double

    // Only for the reason: the shifts of the block, e.g. "SUN-M + SUN-E".
    auto blockText = [&] {
        std::string text;
        for (int s : block.shifts) text += (text.empty() ? "" : " + ") + problem.shifts[s].id;
        return text + " would be " + hoursText(block.minutes()) + " in a row";
    };
    if (block.minutes() > maxDoubleMinutes_)                    // too long, even with approval
        return why ? blocked(why, blockText() + ", more than maxDoubleHours (" + hoursText(maxDoubleMinutes_) + ")")
                   : false;
    if (approvedDouble(problem.employees[emp], block)) return true;   // a double, approved
    return why ? blocked(why, blockText() + " (a double), and no \"double\": true covers it in the availability")
               : false;
}

bool MaxShiftsConstraint::check(const Problem& problem, const State& state, int emp, int /*shift*/,
                                std::string* why) const {
    const std::optional<int>& limit = problem.employees[emp].maxShifts;
    if (!limit) return true;   // no personal limit
    const int has = static_cast<int>(state.shiftsOf(emp).size());
    if (has < *limit) return true;
    return why ? blocked(why, "already has " + std::to_string(has) + " shifts, maxShifts is " + std::to_string(*limit))
               : false;
}

bool MaxConsecutiveDaysConstraint::check(const Problem& problem, const State& state, int emp, int shift,
                                         std::string* why) const {
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

    const int run = last - first + 1;
    if (run <= maxDays_) return true;
    return why ? blocked(why, "would make " + std::to_string(run) + " days in a row (" +
                                  Date::fromDays(first).toString() + " .. " + Date::fromDays(last).toString() +
                                  "), the limit is " + std::to_string(maxDays_))
               : false;
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
