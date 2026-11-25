#pragma once
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Problem.h"
#include "io/Diagnostics.h"
#include "report/ReportModel.h"
#include "solver/Score.h"

// The result as JSON (stage 8): the contract between the engine and an app
// (DECISIONS D40, D57-D62). Its shape is contract/result.schema.json, and
// contract/ describes every field. Like the console, this is a view: it
// writes what ReportModel computed and computes nothing itself (D41).
//
// Names and formats (D62): camelCase, fixed values as camelCase strings,
// dates "YYYY-MM-DD", times "HH:MM-HH:MM", durations in minutes, employees
// by id and shifts by id. A field with no value is left out.

// Raised only when a field changes or goes away, or its meaning changes.
// A new field keeps the version (D62).
constexpr int kResultFormatVersion = 1;

enum class RunStatus {
    Found,          // a schedule was found ("complete" says if it is proven best)
    NoSchedule,     // the whole search space was checked: no schedule exists
    LimitReached,   // stopped by a limit before any schedule was found
    InputError      // the input could not be loaded; see "diagnostics"
};

// How the run went: everything in "run" except what the report holds.
struct RunInfo {
    RunStatus status = RunStatus::Found;
    int exitCode = 0;
    bool firstOnly = false;          // --first
    // Search facts; not written for InputError. The last four only in the
    // default mode (the best schedule), where they have a meaning.
    long long nodes = 0;
    double ms = 0;
    bool complete = false;
    long long improvements = 0;
    long long pruned = 0;
    Score startBound;
    // The options the search ran with.
    int top = 1;
    int minDiff = 1;
    std::optional<double> timeLimit;
    std::optional<long long> maxNodes;
};

// The whole result. `problem` is null when the input could not be loaded.
// `shape` gives the layout (rows and weeks) and the shifts that cannot be
// staffed; it is reports[0] when there is a schedule, or the report of an
// empty schedule when there is none. `schedules` is best first (see
// buildReports); empty when no schedule was found.
nlohmann::ordered_json resultToJson(const Problem* problem, const Report* shape, const std::vector<Report>& schedules,
                                    const RunInfo& run, const std::vector<Diagnostic>& diagnostics);

// The text of the file: indented, ends with a newline.
std::string resultJsonText(const nlohmann::ordered_json& result);

// Writes the text to `path`, or to `stdoutStream` when path is "-" (D57).
// False (with an error in `problems`) if the file cannot be written.
bool writeResultJson(const std::string& path, const nlohmann::ordered_json& result, std::ostream& stdoutStream,
                     Diagnostics& problems);
