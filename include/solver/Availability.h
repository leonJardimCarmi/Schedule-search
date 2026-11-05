#pragma once
#include "Employee.h"
#include "Shift.h"

// How well an employee's availability fits a shift.
enum class Fit {
    No,          // cannot work this shift
    Yes,         // can work it ("available")
    Preferred    // can work it and would like to ("preferred")
};

// Looks only at the employee's own availability for the shift's date.
// Rules that depend on other assignments (rest, max shifts...) come later.
//
//   - A slot with no time covers the whole day: every shift that starts on it.
//   - "unavailable" wins: if it touches the shift at all, the answer is No,
//     even if another slot says available.
//   - "available"/"preferred" count only if the slot covers the WHOLE shift.
//   - No matching slot means No: availability is opt-in.
Fit availabilityFit(const Employee& employee, const Shift& shift);

const char* toString(Fit fit);

// Can this employee do the job the shift needs? A shift without a role can
// be filled by anyone; a shift with a role only by employees who have it.
// Like availability, this never changes during the search.
bool roleFits(const Employee& employee, const Shift& shift);
