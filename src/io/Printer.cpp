#include "io/Printer.h"

#include <algorithm>
#include <vector>

#include <iomanip>
#include <string>

#include "solver/Availability.h"

namespace {

const char* weekdayName(const Date& d) {
    static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[d.weekday()];
}

void printOptional(std::ostream& out, const char* label, const std::optional<int>& v, const char* unit) {
    out << "  " << std::left << std::setw(20) << label;
    if (v) out << *v << unit << '\n';
    else out << "(no limit)\n";
}

}  // namespace

void printProblem(std::ostream& out, const Problem& p) {
    out << "Period: " << p.periodStart.toString() << " .. " << p.periodEnd.toString()
        << "  (" << (p.periodEnd - p.periodStart + 1) << " days)\n\n";

    out << "Employees (" << p.employees.size() << ")\n";
    for (const Employee& e : p.employees) {
        out << "  #" << e.id << ' ' << e.name;
        if (!e.roles.empty()) {
            out << "  roles:";
            for (const std::string& role : e.roles) out << ' ' << role;
        }
        if (e.minShifts) out << "  min " << *e.minShifts << " shifts";
        if (e.maxShifts) out << "  max " << *e.maxShifts << " shifts";
        out << '\n';
        for (const AvailabilitySlot& s : e.availability) {
            out << "      " << s.date.toString() << ' ' << weekdayName(s.date) << "  "
                << std::left << std::setw(12) << (s.time ? s.time->toString() : "all day")
                << toString(s.type) << (s.allowsDouble ? "  (double ok)" : "") << '\n';
        }
    }

    out << "\nShifts (" << p.shifts.size() << ", in chronological order)\n";
    for (const Shift& s : p.shifts) {
        out << "  " << std::left << std::setw(7) << s.id
            << s.date.toString() << ' ' << weekdayName(s.date) << "  " << s.time.toString()
            << "  staff " << s.minStaff << '-' << s.wantedStaff;
        if (!s.role.empty()) out << "  role: " << s.role;
        out << '\n';
    }

    out << "\nRules\n";
    out << "  " << std::left << std::setw(20) << "minRestHours" << p.rules.minRestHours << " h\n";
    out << "  " << std::left << std::setw(20) << "blockGapMinutes" << p.rules.blockGapMinutes << " min\n";
    out << "  " << std::left << std::setw(20) << "doubleAboveHours" << p.rules.doubleAboveHours << " h\n";
    out << "  " << std::left << std::setw(20) << "maxDoubleHours" << p.rules.maxDoubleHours << " h\n";
    printOptional(out, "maxConsecutiveDays", p.rules.maxConsecutiveDays, " days");
}

void printCandidates(std::ostream& out, const Problem& p, const Solver& solver) {
    out << "\nWho can work each shift\n";
    for (int s = 0; s < static_cast<int>(p.shifts.size()); ++s) {
        const Shift& shift = p.shifts[s];
        out << "  " << std::left << std::setw(7) << shift.id;
        const auto& candidates = solver.candidatesOf(s);
        if (candidates.empty()) out << " (nobody)";
        for (int e : candidates) {
            out << "  " << p.employees[e].name;
            if (availabilityFit(p.employees[e], shift) == Fit::Preferred) out << " (preferred)";
        }
        out << '\n';
    }
}

namespace {

// Employee names, comma separated.
std::string names(const Problem& p, const std::vector<int>& employees) {
    std::string result;
    for (int e : employees) result += (result.empty() ? "" : ", ") + p.employees[e].name;
    return result;
}

std::string scoreText(const Score& s) {
    return "Medium " + std::to_string(s.at(Level::Medium)) + ", Soft " + std::to_string(s.at(Level::Soft));
}

}  // namespace

// From here on, everything shown comes from the Report (D41): these
// functions only choose words and columns, they do not compute.

void printSchedule(std::ostream& out, const Problem& p, const Report& report) {
    out << "Schedule " << p.periodStart.toString() << " .. " << p.periodEnd.toString() << '\n';

    // 1. By day. Shifts are in chronological order, so a new day header is
    //    printed whenever the date changes.
    for (std::size_t k = 0; k < report.shifts.size(); ++k) {
        const ShiftReport& row = report.shifts[k];
        const Shift& shift = p.shifts[row.shift];
        if (k == 0 || shift.date != p.shifts[report.shifts[k - 1].shift].date)
            out << '\n' << weekdayName(shift.date) << ' ' << shift.date.toString() << '\n';

        out << "  " << std::left << std::setw(7) << shift.id << shift.time.toString() << "  "
            << row.staff.size() << '/' << shift.wantedStaff << "  ";
        out << (row.staff.empty() ? "-" : names(p, row.staff));
        if (!shift.role.empty()) out << "  [" << shift.role << ']';
        if (row.status == ShiftReport::Status::BelowMin) out << "  (BELOW MIN " << shift.minStaff << ": see below)";
        else if (row.status == ShiftReport::Status::BelowWanted) out << "  (below wanted)";
        out << '\n';
    }

    // 2. Per employee: how many shifts and hours each person got.
    out << "\nPer employee\n";
    for (const EmployeeReport& row : report.employees) {
        out << "  " << std::left << std::setw(16) << p.employees[row.employee].name
            << std::right << std::setw(2) << row.shifts.size() << " shifts "
            << std::setw(3) << row.minutes / 60 << " h  ";
        for (int s : row.shifts) out << ' ' << p.shifts[s].id;
        if (row.belowMinShifts) out << "  (below minimum " << *row.belowMinShifts << ")";
        out << '\n';
    }
}

void printShortages(std::ostream& out, const Problem& p, const Report& report) {
    if (report.shortages.empty()) return;

    out << "\nShifts that cannot be fully staffed (fill them by hand)\n";
    for (const ShortageReport& gap : report.shortages) {
        const Shift& shift = p.shifts[gap.shift];
        out << "  " << std::left << std::setw(7) << shift.id << shift.date.toString() << ' '
            << shift.time.toString() << "  needs " << gap.needed << ", can get " << gap.candidates << ": ";
        switch (gap.reason) {
        case ShortageReport::Reason::NobodyAvailable:
            out << "nobody is available at this time";
            break;
        case ShortageReport::Reason::MissingRole:
            out << gap.available << " available, but only " << gap.candidates << " with role '" << shift.role << "'";
            break;
        case ShortageReport::Reason::TooFewAvailable:
            out << "only " << gap.available << " available";
            break;
        }
        out << '\n';
    }
}

void printScore(std::ostream& out, const Report& report) {
    out << "\nScore (lower is better): " << scoreText(report.score) << "\n";
    for (const ScoreLine& line : report.scoreLines) {
        out << "  " << std::left << std::setw(18) << line.rule << std::right
            << std::setw(7) << toString(line.level) << "  ";
        if (line.weight == 0) {
            out << "off (weight 0)\n";
            continue;
        }
        out << std::setw(4) << line.violations << " x " << std::setw(3) << line.weight
            << " = " << std::setw(5) << line.penalty << "   " << line.description << "\n";
    }
}

void printAlternatives(std::ostream& out, const Problem& p, const Report& report, int minDifferent) {
    if (report.alternatives.empty()) return;
    constexpr std::size_t kMaxShiftLines = 6;   // a month can differ in many shifts: show the first few

    out << "\nOther good schedules (each differs from the others in at least " << minDifferent
        << " assignments):\n";
    for (std::size_t k = 0; k < report.alternatives.size(); ++k) {
        const AlternativeReport& alt = report.alternatives[k];
        out << "  #" << k + 2 << "  " << scoreText(alt.score);
        if (alt.score.at(Level::Medium) == report.score.at(Level::Medium))
            out << "  (+" << alt.score.at(Level::Soft) - report.score.at(Level::Soft) << ")";
        out << ", " << alt.differences << " assignments differ from #1\n";

        for (std::size_t c = 0; c < alt.changes.size() && c < kMaxShiftLines; ++c) {
            const ShiftChange& change = alt.changes[c];
            const Shift& shift = p.shifts[change.shift];
            out << "      " << std::left << std::setw(8) << shift.id << std::right << shift.date.toString();
            if (!change.out.empty()) out << "  out: " << names(p, change.out);
            if (!change.in.empty()) out << "  in: " << names(p, change.in);
            out << "\n";
        }
        if (alt.changes.size() > kMaxShiftLines)
            out << "      ... and " << alt.changes.size() - kMaxShiftLines << " more shifts\n";
    }
}
