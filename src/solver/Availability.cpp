#include "solver/Availability.h"

Fit availabilityFit(const Employee& employee, const Shift& shift) {
    bool available = false;
    bool preferred = false;

    for (const AvailabilitySlot& slot : employee.availability) {
        if (slot.date != shift.date) continue;   // a different day: irrelevant

        const bool wholeDay = !slot.time.has_value();

        switch (slot.type) {
            case AvailabilityType::Unavailable:
                // Any overlap is enough to block the shift.
                if (wholeDay || slot.time->overlaps(shift.time)) return Fit::No;
                break;
            case AvailabilityType::Preferred:
                if (wholeDay || slot.time->contains(shift.time)) preferred = true;
                break;
            case AvailabilityType::Available:
                if (wholeDay || slot.time->contains(shift.time)) available = true;
                break;
        }
    }

    // Decided only after the loop, because an "unavailable" slot may come
    // after an "available" one in the list.
    if (preferred) return Fit::Preferred;
    if (available) return Fit::Yes;
    return Fit::No;
}

const char* toString(Fit fit) {
    switch (fit) {
        case Fit::No:        return "no";
        case Fit::Yes:       return "yes";
        case Fit::Preferred: return "preferred";
    }
    return "?";
}
