#pragma once
#include <optional>
#include <string>

#include "Problem.h"
#include "io/Diagnostics.h"
#include "solver/State.h"

// A schedule as a file (stage 7, DECISIONS D51): who works in which shift.
//
//   { "assignments": { "SUN-M": [101, "Dana Levi"], "SUN-E": [] } }
//
// Shifts by id. Employees by id (a number) or by full name (a string, upper
// and lower case alike), the same as --explain. The problem file stays the
// one place for employees, shifts and rules: this file only says who works
// where. --save writes it (ids, every shift, in the input's order) and
// --check reads it.

// The text of the file.
std::string scheduleToJson(const Problem& problem, const State& schedule);

// Writes the file. False (with an error in `diagnostics`) if it cannot.
bool saveSchedule(const std::string& path, const Problem& problem, const State& schedule,
                  Diagnostics& diagnostics);

struct ScheduleLoadResult {
    std::optional<State> schedule;   // empty when the file could not be read at all
    Diagnostics diagnostics;

    bool ok() const { return schedule.has_value() && !diagnostics.hasErrors(); }
};

// Reads a schedule of `problem`. Unknown shifts or employees, or someone
// listed twice in one shift, are errors. A shift missing from the file is
// taken as empty, with a warning. Availability, roles and the hard rules
// are NOT checked here: a hand-made schedule may break them (D18), and that
// is what --check reports.
ScheduleLoadResult loadScheduleFile(const std::string& path, const Problem& problem);
ScheduleLoadResult loadScheduleString(const std::string& text, const std::string& sourceName,
                                      const Problem& problem);
