#include "Employee.h"

#include <algorithm>

bool Employee::hasRole(const std::string& role) const {
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

std::optional<AvailabilityType> parseAvailabilityType(const std::string& text) {
    if (text == "available") return AvailabilityType::Available;
    if (text == "preferred") return AvailabilityType::Preferred;
    if (text == "unavailable") return AvailabilityType::Unavailable;
    return std::nullopt;
}

const char* toString(AvailabilityType type) {
    switch (type) {
        case AvailabilityType::Available:   return "available";
        case AvailabilityType::Preferred:   return "preferred";
        case AvailabilityType::Unavailable: return "unavailable";
    }
    return "?";
}
