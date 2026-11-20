#include "report/ReportModel.h"

#include <algorithm>

#include "solver/Availability.h"

namespace {

// People in `from` who are not in `other`.
std::vector<int> missingFrom(const std::vector<int>& from, const std::vector<int>& other) {
    std::vector<int> result;
    for (int e : from)
        if (std::find(other.begin(), other.end(), e) == other.end()) result.push_back(e);
    return result;
}

ShiftReport shiftReport(const Problem& p, const State& schedule, int s) {
    const Shift& shift = p.shifts[s];
    const int staffed = schedule.assignedCount(s);
    ShiftReport::Status status = ShiftReport::Status::Full;
    if (staffed < shift.minStaff) status = ShiftReport::Status::BelowMin;
    else if (staffed < shift.wantedStaff) status = ShiftReport::Status::BelowWanted;
    return ShiftReport{s, schedule.staffOf(s), status};
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
    for (int s = 0; s < static_cast<int>(problem.shifts.size()); ++s)
        report.shifts.push_back(shiftReport(problem, schedule, s));
    for (int e = 0; e < static_cast<int>(problem.employees.size()); ++e)
        report.employees.push_back(employeeReport(problem, schedule, e));
    for (const Shortage& gap : shortages)
        report.shortages.push_back(shortageReport(problem, gap));

    report.score = model.score(problem, schedule);
    report.scoreLines = model.breakdown(problem, schedule);

    for (std::size_t k = 1; k < top.size(); ++k)
        report.alternatives.push_back(alternativeReport(schedule, top[k]));
    return report;
}
