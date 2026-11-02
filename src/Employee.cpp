#include "Employee.h"

#include <algorithm>

bool Employee::hasRole(const std::string& role) const {
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

const char* toString(AvailabilityType type) {
    switch (type) {
        case AvailabilityType::Available:   return "available";
        case AvailabilityType::Preferred:   return "preferred";
        case AvailabilityType::Unavailable: return "unavailable";
    }
    return "?";
}
