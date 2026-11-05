#pragma once
#include <optional>

// Global rules from the "rules" object of the input file.
// Every field has a default (DECISIONS section 2.4), so the file may leave
// any of them out. std::optional means "no default": not set = not enforced.
struct Rules {
    int minRestHours = 10;                  // D15: rest between work blocks
    int blockGapMinutes = 0;                // D14: shifts this close form one block
    int doubleAboveHours = 10;              // D16: a longer block is a double
    int maxDoubleHours = 16;                // D16: even an approved double is no longer
    std::optional<int> maxConsecutiveDays;  // step 2.5
};
