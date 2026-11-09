#pragma once
#include <optional>
#include <string>
#include <vector>

#include "Date.h"
#include "TimePeriod.h"

enum class AvailabilityType { Available, Preferred, Unavailable };

// One line of an employee's availability.
struct AvailabilitySlot {
    Date date;
    std::optional<TimePeriod> time;   // empty = the whole day
    AvailabilityType type;
    bool allowsDouble = false;          // "double": true (DECISIONS D16)
};

// Fixed input data: nothing here changes while the solver searches.
struct Employee {
    int id = 0;
    std::string name;
    std::optional<int> maxShifts;            // empty = no personal limit
    std::vector<std::string> roles;          // jobs this person can do
    std::vector<AvailabilitySlot> availability;

    bool hasRole(const std::string& role) const;
};

const char* toString(AvailabilityType type);

// "available" / "preferred" / "unavailable" -> the enum. Empty if unknown.
// Shared by the JSON and the CSV loaders.
std::optional<AvailabilityType> parseAvailabilityType(const std::string& text);
