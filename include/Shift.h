#pragma once
#include <string>

#include "Date.h"
#include "TimePeriod.h"

// Fixed input data: who is assigned lives in the solver's State, not here.
struct Shift {
    std::string id;
    Date date;
    TimePeriod time;
    int minStaff = 0;
    int wantedStaff = 0;
    std::string role;   // empty = any employee can fill it

    // Minutes since 1970-01-01 00:00. One timeline for all shifts, so
    // "do these overlap?" and "how many hours of rest between them?"
    // work across days and across midnight with plain subtraction.
    int absoluteStart() const { return date.daysSinceEpoch() * TimePeriod::MINUTES_PER_DAY + time.getStart(); }
    int absoluteEnd() const { return date.daysSinceEpoch() * TimePeriod::MINUTES_PER_DAY + time.getEnd(); }
};
