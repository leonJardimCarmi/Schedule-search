#include "io/CsvLoader.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) fields.push_back(trim(field));
    if (!line.empty() && line.back() == ',') fields.push_back("");   // "a,b," has 3 fields
    return fields;
}

// The "double" column (D20): empty means no.
std::optional<bool> parseYesNo(const std::string& s) {
    if (s.empty() || s == "no" || s == "false" || s == "0") return false;
    if (s == "yes" || s == "true" || s == "1") return true;
    return std::nullopt;
}

}  // namespace

void CsvLoader::loadAvailability(const std::string& path, Problem& problem, Diagnostics& diagnostics) {
    std::ifstream file(path);
    if (!file) {
        diagnostics.error(path, "cannot open file");
        return;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    loadAvailabilityString(buffer.str(), path, problem, diagnostics);
}

void CsvLoader::loadAvailabilityString(const std::string& text, const std::string& sourceName,
                                       Problem& problem, Diagnostics& diagnostics) {
    std::stringstream input(text);
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string where = sourceName + ":" + std::to_string(lineNumber);

        // Files saved from Excel often start with a UTF-8 "byte order mark"
        // (3 invisible bytes) and end lines with "\r\n". Remove both.
        if (lineNumber == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Blank lines, comments and the header row are skipped.
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const std::vector<std::string> f = splitFields(trimmed);
        if (f[0] == "employee_id") continue;

        if (f.size() < 4 || f.size() > 5) {
            diagnostics.error(where, "expected 4 or 5 fields (employee_id,date,time,type[,double]), got " +
                                         std::to_string(f.size()));
            continue;
        }

        // Each field is checked; every problem in the line is reported.
        bool ok = true;

        std::optional<std::size_t> emp;
        try {
            std::size_t used = 0;
            const int id = std::stoi(f[0], &used);
            if (used != f[0].size()) throw std::invalid_argument("trailing characters");
            emp = problem.findEmployee(id);
            if (!emp) { diagnostics.error(where, "no employee with id " + f[0] + " in the JSON file"); ok = false; }
        } catch (const std::exception&) {
            diagnostics.error(where, "employee_id is not a number: '" + f[0] + "'");
            ok = false;
        }

        std::optional<Date> date;
        try {
            date = Date::fromString(f[1]);
        } catch (const std::exception& e) {
            diagnostics.error(where, e.what());
            ok = false;
        }

        std::optional<TimePeriod> time;   // empty = the whole day
        if (!f[2].empty()) {
            try {
                time = TimePeriod::fromString(f[2]);
            } catch (const std::exception& e) {
                diagnostics.error(where, e.what());
                ok = false;
            }
        }

        const std::optional<AvailabilityType> type = parseAvailabilityType(f[3]);
        if (!type) { diagnostics.error(where, "type must be available, preferred or unavailable"); ok = false; }

        const std::optional<bool> allowsDouble = parseYesNo(f.size() == 5 ? f[4] : "");
        if (!allowsDouble) { diagnostics.error(where, "double must be yes or no (or empty)"); ok = false; }

        if (!ok) continue;

        if (*date < problem.periodStart || *date > problem.periodEnd)
            diagnostics.warning(where, date->toString() + " is outside the period, ignored");
        if (*allowsDouble && *type == AvailabilityType::Unavailable)
            diagnostics.warning(where, "double has no meaning on an unavailable line, ignored");

        problem.employees[*emp].availability.push_back(
            AvailabilitySlot{*date, time, *type, *allowsDouble && *type != AvailabilityType::Unavailable});
    }
}
