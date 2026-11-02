#include "solver/State.h"

#include <algorithm>
#include <cassert>

State::State(int employeeCount, int shiftCount)
    : staffOf_(shiftCount), shiftsOf_(employeeCount) {}

void State::assign(int emp, int shift) {
    assert(!isAssigned(emp, shift) && "employee is already in this shift");
    staffOf_[shift].push_back(emp);
    shiftsOf_[emp].push_back(shift);
}

void State::unassign(int emp, int shift) {
    // Backtracking undoes the last step first, so the pair we remove
    // is always at the back of both lists. pop_back() is O(1).
    assert(!staffOf_[shift].empty() && staffOf_[shift].back() == emp);
    assert(!shiftsOf_[emp].empty() && shiftsOf_[emp].back() == shift);
    staffOf_[shift].pop_back();
    shiftsOf_[emp].pop_back();
}

bool State::isAssigned(int emp, int shift) const {
    const std::vector<int>& staff = staffOf_[shift];
    return std::find(staff.begin(), staff.end(), emp) != staff.end();
}
