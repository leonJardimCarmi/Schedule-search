#include "report/ReportModel.h"

#include <algorithm>

#include "solver/Availability.h"
#include "solver/WorkBlocks.h"

namespace {

// People in `from` who are not in `other`.
std::vector<int> missingFrom(const std::vector<int>& from, const std::vector<int>& other) {
    std::vector<int> result;
    for (int e : from)
        if (std::find(other.begin(), other.end(), e) == other.end()) result.push_back(e);
    return result;
}

// [shift][employee] -> the shift is part of a double for that employee.
// Blocks are built once per employee from their whole schedule.
std::vector<std::vector<char>> doublesOf(const Problem& p, const State& schedule) {
    std::vector<std::vector<char>> result(p.shifts.size(), std::vector<char>(p.employees.size(), 0));
    const int limit = p.rules.doubleAboveHours * 60;
    for (int e = 0; e < static_cast<int>(p.employees.size()); ++e)
        for (const WorkBlock& block : buildBlocks(p, schedule.shiftsOf(e), p.rules.blockGapMinutes))
            if (block.minutes() > limit)
                for (int s : block.shifts) result[s][e] = 1;
    return result;
}

ShiftReport shiftReport(const Problem& p, const State& schedule, int s,
                        const std::vector<std::vector<char>>& doubles) {
    const Shift& shift = p.shifts[s];
    const int staffed = schedule.assignedCount(s);
    ShiftReport::Status status = ShiftReport::Status::Full;
    if (staffed < shift.minStaff) status = ShiftReport::Status::BelowMin;
    else if (staffed < shift.wantedStaff) status = ShiftReport::Status::BelowWanted;

    ShiftReport report{s, schedule.staffOf(s), status, {}, {}};
    for (int e : report.staff) {
        report.preferred.push_back(availabilityFit(p.employees[e], shift) == Fit::Preferred);
        report.inDouble.push_back(doubles[s][e]);
    }
    return report;
}

// The rows of the weekly table: one per distinct (hours, role).
void buildKinds(const Problem& p, Report& report) {
    auto before = [](const ShiftKind& a, const ShiftKind& b) {
        if (a.time.getStart() != b.time.getStart()) return a.time.getStart() < b.time.getStart();
        if (a.time.getEnd() != b.time.getEnd()) return a.time.getEnd() < b.time.getEnd();
        return a.role < b.role;
    };
    auto same = [&](const ShiftKind& a, const ShiftKind& b) { return !before(a, b) && !before(b, a); };

    for (const Shift& shift : p.shifts) report.kinds.push_back(ShiftKind{shift.time, shift.role});
    std::sort(report.kinds.begin(), report.kinds.end(), before);
    report.kinds.erase(std::unique(report.kinds.begin(), report.kinds.end(), same), report.kinds.end());

    for (const Shift& shift : p.shifts) {
        const ShiftKind key{shift.time, shift.role};
        auto it = std::lower_bound(report.kinds.begin(), report.kinds.end(), key, before);
        report.kindOf.push_back(static_cast<int>(it - report.kinds.begin()));
    }
}

// Full weeks from the Sunday on or before the period start.
void buildWeeks(const Problem& p, Report& report) {
    const Date firstSunday = Date::fromDays(p.periodStart.daysSinceEpoch() - p.periodStart.weekday());
    const int weekCount = (p.periodEnd - firstSunday) / 7 + 1;
    for (int w = 0; w < weekCount; ++w) {
        WeekReport week{firstSunday + 7 * w, {}, 0, 0, 0, 0, {}};
        week.cells.resize(report.kinds.size());
        week.shiftsPerEmployee.assign(p.employees.size(), 0);
        report.weeks.push_back(std::move(week));
    }
    for (const ShiftReport& row : report.shifts) {
        const int day = p.shifts[row.shift].date - firstSunday;
        WeekReport& week = report.weeks[day / 7];
        week.cells[report.kindOf[row.shift]][day % 7].push_back(row.shift);
        ++week.shifts;
        switch (row.status) {
        case ShiftReport::Status::Full: ++week.full; break;
        case ShiftReport::Status::BelowWanted: ++week.belowWanted; break;
        case ShiftReport::Status::BelowMin: ++week.belowMin; break;
        }
        for (int e : row.staff) ++week.shiftsPerEmployee[e];
    }
}

EmployeeReport employeeReport(const Problem& p, const State& schedule, int e) {
    // In the order they were assigned; with MRV (D26) that is not calendar
    // order. Shift indices are sorted by time (the loader does that), so
    // sorting the indices puts them back in calendar order.
    std::vector<int> shifts = schedule.shiftsOf(e);
    std::sort(shifts.begin(), shifts.end());
    int minutes = 0;
    for (int s : shifts) minutes += p.shifts[s].time.duration();

    std::optional<int> below;
    const auto& minShifts = p.employees[e].minShifts;
    if (minShifts && static_cast<int>(shifts.size()) < *minShifts) below = *minShifts;
    return EmployeeReport{e, shifts, minutes, below};
}

ShortageReport shortageReport(const Problem& p, const Shortage& gap) {
    const Shift& shift = p.shifts[gap.shift];
    // Why: count who is available at all, and who of those has the role.
    int available = 0;
    for (const Employee& e : p.employees)
        if (availabilityFit(e, shift) != Fit::No) ++available;

    ShortageReport::Reason reason = ShortageReport::Reason::TooFewAvailable;
    if (available == 0) reason = ShortageReport::Reason::NobodyAvailable;
    else if (!shift.role.empty() && gap.candidates < available) reason = ShortageReport::Reason::MissingRole;
    return ShortageReport{gap.shift, gap.needed, gap.candidates, available, reason};
}

AlternativeReport alternativeReport(const State& best, const RankedSchedule& alt) {
    AlternativeReport report{alt.score, differentAssignments(best, alt.schedule), {}};
    for (int s = 0; s < alt.schedule.shiftCount(); ++s) {
        std::vector<int> out = missingFrom(best.staffOf(s), alt.schedule.staffOf(s));
        std::vector<int> in = missingFrom(alt.schedule.staffOf(s), best.staffOf(s));
        if (!out.empty() || !in.empty()) report.changes.push_back(ShiftChange{s, std::move(out), std::move(in)});
    }
    return report;
}

}  // namespace

Report buildReport(const Problem& problem, const State& schedule, const std::vector<Shortage>& shortages,
                   const ScoreModel& model, const std::vector<RankedSchedule>& top) {
    Report report;
    const auto doubles = doublesOf(problem, schedule);
    for (int s = 0; s < static_cast<int>(problem.shifts.size()); ++s)
        report.shifts.push_back(shiftReport(problem, schedule, s, doubles));
    for (int e = 0; e < static_cast<int>(problem.employees.size()); ++e)
        report.employees.push_back(employeeReport(problem, schedule, e));
    for (const Shortage& gap : shortages)
        report.shortages.push_back(shortageReport(problem, gap));

    report.score = model.score(problem, schedule);
    report.scoreLines = model.breakdown(problem, schedule);

    for (std::size_t k = 1; k < top.size(); ++k)
        report.alternatives.push_back(alternativeReport(schedule, top[k]));

    buildKinds(problem, report);
    buildWeeks(problem, report);
    return report;
}
