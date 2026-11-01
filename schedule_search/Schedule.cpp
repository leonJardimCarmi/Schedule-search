#include "Schedule.h"

bool Schedule::canAssignPersonToShift(Person& p, Shift& s) { 
  /*  for (auto& constraint : constraints) {
        if (!constraint->isSatisfied(p, s)) {
            return false; 
        }
    }
    return true;*/
    return true; // TODO: replace with constraint checks (stage 2)
}

bool Schedule::solve(size_t shift_idx) {
    if (shift_idx >= shifts.size()) return true;

    Shift& current_shift = shifts[shift_idx];

    if (current_shift.getAssignedCount() >= current_shift.getWantedStaff()) {
        return solve(shift_idx + 1);
    }

    for (auto& s : staff) {
        auto& p = s.second;
        if (canAssignPersonToShift(p, current_shift)) {
            current_shift.assign(p);

            if (solve(shift_idx)) return true;

            current_shift.unassign(p);
        }
    }

    if (current_shift.getAssignedCount() >= current_shift.getMinStaff()) {
        return solve(shift_idx + 1);
    }

    return false;
}