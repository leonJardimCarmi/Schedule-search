#include "io/Printer.h"

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

void printSchedule(std::ostream& out, const Problem& p, const State& schedule) {
    out << "Schedule " << p.periodStart.toString() << " .. " << p.periodEnd.toString() << '\n';

    // 1. By day. Shifts are already in chronological order, so a new day
    //    header is printed whenever the date changes.
    for (int s = 0; s < static_cast<int>(p.shifts.size()); ++s) {
        const Shift& shift = p.shifts[s];
        if (s == 0 || shift.date != p.shifts[s - 1].date)
            out << '\n' << weekdayName(shift.date) << ' ' << shift.date.toString() << '\n';

        const int staffed = schedule.assignedCount(s);
        out << "  " << std::left << std::setw(7) << shift.id << shift.time.toString() << "  "
            << staffed << '/' << shift.wantedStaff << "  ";

        const auto& staff = schedule.staffOf(s);
        for (std::size_t i = 0; i < staff.size(); ++i)
            out << (i ? ", " : "") << p.employees[staff[i]].name;
        if (staff.empty()) out << "-";
        if (!shift.role.empty()) out << "  [" << shift.role << ']';
        if (staffed < shift.wantedStaff) out << "  (below wanted)";
        out << '\n';
    }

    // 2. Per employee: how many shifts and hours each person got.
    out << "\nPer employee\n";
    for (int e = 0; e < static_cast<int>(p.employees.size()); ++e) {
        const auto& shifts = schedule.shiftsOf(e);
        int minutes = 0;
        for (int s : shifts) minutes += p.shifts[s].time.duration();

        out << "  " << std::left << std::setw(16) << p.employees[e].name
            << std::right << std::setw(2) << shifts.size() << " shifts "
            << std::setw(3) << minutes / 60 << " h  ";
        for (int s : shifts) out << ' ' << p.shifts[s].id;

        // minShifts is only a wish (D22), so it is reported, not enforced.
        const auto& minShifts = p.employees[e].minShifts;
        if (minShifts && static_cast<int>(shifts.size()) < *minShifts)
            out << "  (below minimum " << *minShifts << ")";
        out << '\n';
    }
}
