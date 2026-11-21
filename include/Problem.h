#pragma once
#include <optional>
#include <string>
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

    // For the command line (--explain): an employee by id ("102") or by full
    // name, ignoring upper and lower case ("dana levi"); a shift by its id.
    std::optional<std::size_t> findEmployee(const std::string& idOrName) const;
    std::optional<std::size_t> findShift(const std::string& id) const;
};
