#pragma once
#include <optional>

// Global rules from the "rules" object of the input file.
// std::optional means "not set": the rule is simply not enforced.
struct Rules {
    std::optional<int> minRestHours;
    std::optional<int> maxConsecutiveDays;
    int maxShiftsPerDay = 1;   // the common case, so it is on by default
};
