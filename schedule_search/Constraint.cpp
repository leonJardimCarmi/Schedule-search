#include "Constraint.h"


bool NoDoubleShiftConstraint::isSatisfied(const Person& p, const Shift& s) const {
    if (s.isAssigned(p)) {
        return false;
    }

    //for (int assigned_day_idx : p.getAssignedShiftDays()) {
    //    if (assigned_day_idx == s.getDayIdx()) {
    //        return false; // ??? ??? ???? ???? ???!
    //    }
    //}
    return true;
}
