#pragma once
#include <vector>

// The part of the search that changes: who is assigned to which shift.
//
// Employees and shifts are referred to by their index in Problem::employees
// and Problem::shifts, never by copies of the objects.
//
// Every assignment is stored twice, once from each side, so that both
// questions the solver asks are a direct lookup:
//   "who works in shift s?"         -> staffOf(s)
//   "which shifts does e work?"     -> shiftsOf(e)
class State {
public:
    State(int employeeCount, int shiftCount);

    void assign(int emp, int shift);
    // Undoes the most recent assign() of this employee to this shift.
    // The DFS always undoes in reverse order, like a stack.
    void unassign(int emp, int shift);

    bool isAssigned(int emp, int shift) const;
    int assignedCount(int shift) const { return static_cast<int>(staffOf_[shift].size()); }

    const std::vector<int>& staffOf(int shift) const { return staffOf_[shift]; }
    const std::vector<int>& shiftsOf(int emp) const { return shiftsOf_[emp]; }
    int shiftCount() const { return static_cast<int>(staffOf_.size()); }

private:
    std::vector<std::vector<int>> staffOf_;    // [shift]    -> employee indices
    std::vector<std::vector<int>> shiftsOf_;   // [employee] -> shift indices
};

// How many assignments differ between two schedules of the same problem
// (step 5.4, D39). Replacing one person in one shift counts as 1: per shift,
// the people only in `a` and the people only in `b` are counted separately,
// and the larger total is the answer.
int differentAssignments(const State& a, const State& b);
