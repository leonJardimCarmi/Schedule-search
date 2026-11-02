#pragma once
#include <optional>
#include <vector>

#include "Date.h"
#include "Employee.h"
#include "Rules.h"
#include "Shift.h"

// Everything the solver needs to know, loaded once and never modified.
struct Problem {
    Date periodStart;
    Date periodEnd;
    std::vector<Employee> employees;
    std::vector<Shift> shifts;   // sorted by absoluteStart()
    Rules rules;

    // Index into `employees` for an employee id, if it exists.
    std::optional<std::size_t> findEmployee(int id) const;
};
