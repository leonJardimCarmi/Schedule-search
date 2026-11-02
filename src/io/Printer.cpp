#include "io/Printer.h"

#include <iomanip>

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
        if (e.maxShifts) out << "  max " << *e.maxShifts << " shifts";
        out << '\n';
        for (const AvailabilitySlot& s : e.availability) {
            out << "      " << s.date.toString() << ' ' << weekdayName(s.date) << "  "
                << std::left << std::setw(12) << (s.time ? s.time->toString() : "all day")
                << toString(s.type) << '\n';
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
    printOptional(out, "minRestHours", p.rules.minRestHours, " h");
    printOptional(out, "maxConsecutiveDays", p.rules.maxConsecutiveDays, " days");
    out << "  " << std::left << std::setw(20) << "maxShiftsPerDay" << p.rules.maxShiftsPerDay << '\n';
}
