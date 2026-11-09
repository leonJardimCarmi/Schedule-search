#pragma once
#include <string>

#include "Problem.h"
#include "io/Diagnostics.h"

// Imports availability from a CSV file into an already loaded Problem
// (DECISIONS section 2.2, D20). Employees are defined in the JSON file;
// the CSV only adds availability lines to them.
//
//   employee_id,date,time,type[,double]
//   102,2026-11-10,08:00-00:00,available,yes
//
// Bad lines are reported with the file name and line number, and skipped;
// the rest of the file still loads.
class CsvLoader {
public:
    static void loadAvailability(const std::string& path, Problem& problem, Diagnostics& diagnostics);

    // Same, from text in memory (used by the tests).
    static void loadAvailabilityString(const std::string& text, const std::string& sourceName,
                                       Problem& problem, Diagnostics& diagnostics);
};
