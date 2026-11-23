#include "io/ScheduleFile.h"

#include <fstream>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string scheduleToJson(const Problem& p, const State& s) {
    // Written by hand rather than with json::dump(), to keep one shift per
    // line in the input's order: easy to read and to edit.
    std::ostringstream out;
    out << "{\n  \"assignments\": {\n";
    for (std::size_t i = 0; i < p.shifts.size(); ++i) {
        out << "    " << json(p.shifts[i].id).dump() << ": [";
        const std::vector<int>& staff = s.staffOf(static_cast<int>(i));
        for (std::size_t k = 0; k < staff.size(); ++k) out << (k ? ", " : "") << p.employees[staff[k]].id;
        out << "]" << (i + 1 < p.shifts.size() ? "," : "") << "\n";
    }
    out << "  }\n}\n";
    return out.str();
}

bool saveSchedule(const std::string& path, const Problem& p, const State& s, Diagnostics& diagnostics) {
    std::ofstream file(path);
    if (file) file << scheduleToJson(p, s);
    if (!file) {
        diagnostics.error(path, "cannot write the file");
        return false;
    }
    return true;
}

ScheduleLoadResult loadScheduleFile(const std::string& path, const Problem& p) {
    std::ifstream file(path);
    if (!file) {
        ScheduleLoadResult result;
        result.diagnostics.error(path, "cannot open file");
        return result;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return loadScheduleString(buffer.str(), path, p);
}

ScheduleLoadResult loadScheduleString(const std::string& text, const std::string& source, const Problem& p) {
    ScheduleLoadResult result;
    Diagnostics& d = result.diagnostics;
    auto at = [&](const std::string& path) { return source + ": " + path; };

    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& e) {
        d.error(at("(syntax)"), e.what());
        return result;
    }
    if (!root.is_object()) {
        d.error(at("(root)"), "expected a JSON object");
        return result;
    }
    for (const auto& item : root.items())
        if (item.key() != "assignments") d.warning(at(item.key()), "unknown field, ignored");
    const auto assignments = root.find("assignments");
    if (assignments == root.end() || !assignments->is_object()) {
        d.error(at("assignments"), "missing or not an object");
        return result;
    }

    State schedule(static_cast<int>(p.employees.size()), static_cast<int>(p.shifts.size()));
    // nlohmann::json keeps only the last of repeated keys, so a shift written
    // twice cannot be detected here; every shift seen is marked as listed.
    std::vector<char> listed(p.shifts.size(), 0);
    for (const auto& item : assignments->items()) {
        const std::string path = "assignments." + item.key();
        const auto shift = p.findShift(item.key());
        if (!shift) {
            d.error(at(path), "no shift with this id in the problem");
            continue;
        }
        listed[*shift] = 1;
        if (!item.value().is_array()) {
            d.error(at(path), "expected a list of employees");
            continue;
        }
        const json& staff = item.value();
        for (std::size_t k = 0; k < staff.size(); ++k) {
            const std::string where = at(path + "[" + std::to_string(k) + "]");
            std::optional<std::size_t> emp;
            if (staff[k].is_number_integer()) emp = p.findEmployee(staff[k].get<int>());
            else if (staff[k].is_string()) emp = p.findEmployee(staff[k].get<std::string>());
            else {
                d.error(where, "expected an employee id (a number) or full name (a string)");
                continue;
            }
            if (!emp) {
                d.error(where, "no employee " + staff[k].dump() + " in the problem");
                continue;
            }
            const int e = static_cast<int>(*emp), s = static_cast<int>(*shift);
            if (schedule.isAssigned(e, s)) {
                d.error(where, p.employees[e].name + " is listed twice in this shift");
                continue;
            }
            schedule.assign(e, s);
        }
    }

    std::string missing;
    int missingCount = 0;
    for (std::size_t i = 0; i < p.shifts.size(); ++i)
        if (!listed[i]) {
            if (missingCount < 5) missing += (missing.empty() ? "" : ", ") + p.shifts[i].id;
            ++missingCount;
        }
    if (missingCount > 0)
        d.warning(at("assignments"), std::to_string(missingCount) + " shift(s) not in the file, taken as empty: " +
                                         missing + (missingCount > 5 ? ", ..." : ""));
    result.schedule = std::move(schedule);
    return result;
}
