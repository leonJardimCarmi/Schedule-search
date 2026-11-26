#include "io/ResultJson.h"

#include <cmath>
#include <fstream>

using json = nlohmann::ordered_json;

namespace {

const char* statusName(RunStatus status) {
    switch (status) {
    case RunStatus::Checked: return "checked";
    case RunStatus::Found: return "found";
    case RunStatus::NoSchedule: return "noSchedule";
    case RunStatus::LimitReached: return "limitReached";
    case RunStatus::InputError: return "inputError";
    }
    return "";
}

const char* shiftStatusName(ShiftReport::Status status) {
    switch (status) {
    case ShiftReport::Status::Full: return "full";
    case ShiftReport::Status::BelowWanted: return "belowWanted";
    case ShiftReport::Status::BelowMin: return "belowMin";
    }
    return "";
}

const char* reasonName(ShortageReport::Reason reason) {
    switch (reason) {
    case ShortageReport::Reason::NobodyAvailable: return "nobodyAvailable";
    case ShortageReport::Reason::MissingRole: return "missingRole";
    case ShortageReport::Reason::TooFewAvailable: return "tooFewAvailable";
    }
    return "";
}

const char* levelName(Level level) { return level == Level::Medium ? "medium" : "soft"; }

json scoreJson(const Score& score) {
    return json{{"medium", score.at(Level::Medium)}, {"soft", score.at(Level::Soft)}};
}

// Indices of the model -> ids of the contract.
json shiftIds(const Problem& p, const std::vector<int>& shifts) {
    json ids = json::array();
    for (int s : shifts) ids.push_back(p.shifts[s].id);
    return ids;
}
json employeeIds(const Problem& p, const std::vector<int>& employees) {
    json ids = json::array();
    for (int e : employees) ids.push_back(p.employees[e].id);
    return ids;
}

json runJson(const RunInfo& run) {
    json r;
    r["status"] = statusName(run.status);
    r["exitCode"] = run.exitCode;
    if (run.status == RunStatus::InputError) return r;   // there was no search
    if (run.checkOnly) {
        r["mode"] = "check";
        return r;
    }
    r["mode"] = run.firstOnly ? "first" : "best";
    if (!run.firstOnly) r["complete"] = run.complete;
    r["nodes"] = run.nodes;
    r["ms"] = std::round(run.ms * 10) / 10;
    if (run.stopped != StopReason::None) r["stopped"] = stopReasonName(run.stopped);
    if (!run.firstOnly) {
        r["improvements"] = run.improvements;
        r["pruned"] = run.pruned;
        r["startBound"] = scoreJson(run.startBound);
    }
    json options;
    if (!run.firstOnly) {
        options["top"] = run.top;
        options["minDiff"] = run.minDiff;
    }
    if (run.timeLimit) options["timeLimit"] = *run.timeLimit;
    if (run.maxNodes) options["maxNodes"] = *run.maxNodes;
    r["options"] = options.is_null() ? json::object() : options;
    return r;
}

json problemJson(const Problem& p, const Report& shape) {
    json employees = json::array();
    for (const Employee& e : p.employees) {
        json item{{"id", e.id}, {"name", e.name}, {"roles", e.roles}};
        if (e.minShifts) item["minShifts"] = *e.minShifts;
        if (e.maxShifts) item["maxShifts"] = *e.maxShifts;
        employees.push_back(item);
    }
    json shifts = json::array();
    for (std::size_t s = 0; s < p.shifts.size(); ++s) {
        const Shift& shift = p.shifts[s];
        json item{{"id", shift.id}, {"date", shift.date.toString()}, {"time", shift.time.toString()},
                  {"minutes", shift.time.duration()}, {"min", shift.minStaff}, {"wanted", shift.wantedStaff}};
        if (!shift.role.empty()) item["role"] = shift.role;
        item["kind"] = shape.kindOf[s];
        shifts.push_back(item);
    }
    return json{{"period", {{"start", p.periodStart.toString()}, {"end", p.periodEnd.toString()}}},
                {"employees", employees},
                {"shifts", shifts}};
}

json layoutJson(const Problem& p, const Report& shape) {
    json kinds = json::array();
    for (const ShiftKind& kind : shape.kinds) {
        json item{{"time", kind.time.toString()}};
        if (!kind.role.empty()) item["role"] = kind.role;
        kinds.push_back(item);
    }
    json weeks = json::array();
    for (const WeekReport& week : shape.weeks) {
        json days = json::array();
        for (int d = 0; d < 7; ++d) {
            const Date date = week.sunday + d;
            days.push_back(json{{"date", date.toString()},
                                {"inPeriod", p.periodStart <= date && date <= p.periodEnd}});
        }
        json cells = json::array();
        for (const auto& row : week.cells) {
            json r = json::array();
            for (const std::vector<int>& cell : row) r.push_back(shiftIds(p, cell));
            cells.push_back(r);
        }
        weeks.push_back(json{{"start", week.sunday.toString()}, {"end", (week.sunday + 6).toString()},
                             {"days", days}, {"cells", cells}});
    }
    return json{{"kinds", kinds}, {"weeks", weeks}};
}

json shortagesJson(const Problem& p, const Report& shape) {
    json list = json::array();
    for (const ShortageReport& gap : shape.shortages)
        list.push_back(json{{"shift", p.shifts[gap.shift].id}, {"needed", gap.needed},
                            {"candidates", gap.candidates}, {"available", gap.available},
                            {"reason", reasonName(gap.reason)}});
    return list;
}

json changesJson(const Problem& p, const std::vector<ShiftChange>& changes) {
    json list = json::array();
    for (const ShiftChange& c : changes)
        list.push_back(json{{"shift", p.shifts[c.shift].id}, {"out", employeeIds(p, c.out)},
                            {"in", employeeIds(p, c.in)}});
    return list;
}

// One schedule. rank 0 = the hand-made one of --check: no rank, and each
// person in a shift says whether that assignment breaks a rule.
json scheduleJson(const Problem& p, const Report& r, int rank, const AlternativeReport* vsBest) {
    json rules = json::array();
    for (const ScoreLine& line : r.scoreLines)
        rules.push_back(json{{"name", line.rule}, {"level", levelName(line.level)},
                             {"violations", line.violations}, {"weight", line.weight},
                             {"penalty", line.penalty}, {"description", line.description}});
    json shifts = json::array();
    for (const ShiftReport& row : r.shifts) {
        json staff = json::array();
        for (std::size_t k = 0; k < row.staff.size(); ++k)
        {
            json person{{"employee", p.employees[row.staff[k]].id},
                        {"preferred", row.preferred[k] != 0}, {"double", row.inDouble[k] != 0}};
            if (rank == 0) person["breaksRule"] = row.breaksRule[k] != 0;
            staff.push_back(person);
        }
        shifts.push_back(json{{"shift", p.shifts[row.shift].id}, {"status", shiftStatusName(row.status)},
                              {"staff", staff}});
    }
    json employees = json::array();
    for (const EmployeeReport& e : r.employees) {
        json item{{"employee", p.employees[e.employee].id}, {"shifts", shiftIds(p, e.shifts)}, {"minutes", e.minutes}};
        if (e.belowMinShifts) item["belowMinShifts"] = *e.belowMinShifts;
        employees.push_back(item);
    }
    json weeks = json::array();
    for (const WeekReport& week : r.weeks) {
        json perEmployee = json::array();
        for (std::size_t e = 0; e < week.shiftsPerEmployee.size(); ++e)
            perEmployee.push_back(json{{"employee", p.employees[e].id}, {"shifts", week.shiftsPerEmployee[e]}});
        weeks.push_back(json{{"shifts", week.shifts}, {"full", week.full}, {"belowWanted", week.belowWanted},
                             {"belowMin", week.belowMin}, {"perEmployee", perEmployee}});
    }
    json item;
    if (rank > 0) item["rank"] = rank;
    item["score"] = scoreJson(r.score);
    item["rules"] = rules;
    item["shifts"] = shifts;
    item["employees"] = employees;
    item["weeks"] = weeks;
    if (vsBest)
        item["vsBest"] = json{{"differences", vsBest->differences}, {"changes", changesJson(p, vsBest->changes)}};
    return item;
}

const char* violationKind(Violation::Kind kind) {
    switch (kind) {
    case Violation::Kind::Availability: return "availability";
    case Violation::Kind::Role: return "role";
    case Violation::Kind::Rule: return "rule";
    }
    return "";
}

// --check: the hand-made schedule, in the same form as any other, and what it breaks.
json manualJson(const Problem& p, const Report& r, const CheckReport& check) {
    json item = scheduleJson(p, r, 0, nullptr);
    json violations = json::array();
    for (const Violation& v : check.violations) {
        json one{{"shift", p.shifts[v.shift].id}, {"employee", p.employees[v.employee].id},
                 {"kind", violationKind(v.kind)}};
        if (v.kind == Violation::Kind::Rule) {
            one["rule"] = v.rule;
            one["why"] = v.why;
        }
        violations.push_back(one);
    }
    json over = json::array();
    for (const OverStaffed& o : check.overStaffed)
        over.push_back(json{{"shift", p.shifts[o.shift].id}, {"staff", o.staff}, {"wanted", o.wanted}});
    item["violations"] = violations;
    item["overStaffed"] = over;
    return item;
}

json comparisonJson(const Problem& p, const ComparisonReport& c) {
    json rules = json::array();
    for (const RuleComparison& r : c.rules)
        rules.push_back(json{{"name", r.rule}, {"level", levelName(r.level)}, {"weight", r.weight},
                             {"mine", r.mine}, {"engine", r.engine}});
    return json{{"mine", scoreJson(c.mine)}, {"engine", scoreJson(c.engine)}, {"differences", c.differences},
                {"rules", rules}, {"changes", changesJson(p, c.changes)}};
}

const char* fitName(Fit fit) {
    switch (fit) {
    case Fit::No: return "no";
    case Fit::Yes: return "yes";
    case Fit::Preferred: return "preferred";
    }
    return "";
}

json explanationJson(const Problem& p, const Explanation& e, bool manual) {
    json rules = json::array();
    for (const RuleCheck& c : e.rules) {
        json one{{"name", c.rule}};
        if (c.whyNot) one["whyNot"] = *c.whyNot;
        rules.push_back(one);
    }
    return json{{"employee", p.employees[e.employee].id}, {"shift", p.shifts[e.shift].id},
                {"against", manual ? "manual" : "best"}, {"canWork", e.canWork()},
                {"alreadyWorks", e.alreadyWorks}, {"availability", fitName(e.availability)},
                {"roleOk", e.roleOk}, {"rules", rules}, {"staff", employeeIds(p, e.staff)}};
}

}  // namespace

json resultToJson(const Problem* problem, const Report* shape, const std::vector<Report>& schedules,
                  const RunInfo& run, const std::vector<Diagnostic>& diagnostics, const ResultExtras& extras) {
    json result;
    result["formatVersion"] = kResultFormatVersion;
    result["run"] = runJson(run);
    if (problem && shape) {
        result["problem"] = problemJson(*problem, *shape);
        result["layout"] = layoutJson(*problem, *shape);
        result["shortages"] = shortagesJson(*problem, *shape);
    }
    json list = json::array();
    for (std::size_t k = 0; problem && k < schedules.size(); ++k) {
        // How schedule k differs from the best is in the best one's report.
        const std::vector<AlternativeReport>& alts = schedules.front().alternatives;
        const AlternativeReport* vsBest = k > 0 && k - 1 < alts.size() ? &alts[k - 1] : nullptr;
        list.push_back(scheduleJson(*problem, schedules[k], static_cast<int>(k) + 1, vsBest));
    }
    result["schedules"] = list;
    if (problem && extras.manual && extras.check) result["manual"] = manualJson(*problem, *extras.manual, *extras.check);
    if (problem && extras.comparison) result["comparison"] = comparisonJson(*problem, *extras.comparison);
    if (problem && extras.explanation)
        result["explanation"] = explanationJson(*problem, *extras.explanation, extras.manual != nullptr);
    json notes = json::array();
    for (const Diagnostic& d : diagnostics)
        notes.push_back(json{{"severity", d.severity == Severity::Error ? "error" : "warning"},
                             {"where", d.where}, {"message", d.message}});
    result["diagnostics"] = notes;
    return result;
}

const char* stopReasonName(StopReason reason) {
    switch (reason) {
    case StopReason::Time: return "time";
    case StopReason::Nodes: return "nodes";
    case StopReason::Request: return "request";
    case StopReason::None: break;
    }
    return "";
}

std::string resultJsonText(const json& result) {
    // "replace": text from the input that is not valid UTF-8 cannot stop the
    // output; it shows up as U+FFFD instead.
    return result.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool writeResultJson(const std::string& path, const json& result, std::ostream& stdoutStream,
                     Diagnostics& problems) {
    const std::string text = resultJsonText(result);
    if (path == "-") {
        stdoutStream << text << std::flush;
        if (stdoutStream) return true;
        problems.error("--json -", "cannot write to stdout");
        return false;
    }
    std::ofstream file(path, std::ios::binary);   // binary: \n on every system, like the schema's examples
    if (file) file << text;
    if (!file) {
        problems.error(path, "cannot write the file");
        return false;
    }
    return true;
}
