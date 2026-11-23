#include "io/Printer.h"

#include <algorithm>
#include <vector>

#include <iomanip>
#include <sstream>
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

}  // namespace

std::string scoreText(const Score& s) {
    return "Medium " + std::to_string(s.at(Level::Medium)) + ", Soft " + std::to_string(s.at(Level::Soft));
}

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

// ---- The weekly table (--view grid, step 6.3, D42) ----------------------------

std::vector<std::string> shortNames(const Problem& p) {
    auto firstWord = [](const std::string& name) { return name.substr(0, name.find(' ')); };
    auto withInitial = [&](const std::string& name) {
        const std::size_t space = name.rfind(' ');
        if (space == std::string::npos || space + 1 >= name.size()) return name;
        return firstWord(name) + ' ' + name[space + 1] + '.';
    };
    auto countOf = [&](auto shorten, const std::string& value) {
        return std::count_if(p.employees.begin(), p.employees.end(),
                             [&](const Employee& e) { return shorten(e.name) == value; });
    };

    std::vector<std::string> result;
    for (const Employee& e : p.employees) {
        std::string name = firstWord(e.name);
        if (countOf(firstWord, name) > 1) name = withInitial(e.name);
        if (countOf(withInitial, name) > 1) name = e.name;
        result.push_back(name);
    }
    return result;
}

namespace {

std::string kindLabel(const ShiftKind& kind) {
    return kind.time.toString() + (kind.role.empty() ? "" : " [" + kind.role + "]");
}

// "2026-12-06" -> "06/12"
std::string dayMonth(const Date& d) {
    const std::string iso = d.toString();
    return iso.substr(8, 2) + "/" + iso.substr(5, 2);
}

// ANSI colors (D45). A code is a few characters the terminal does not show,
// so widths are always computed from the text alone, never from the codes.
enum class Color { None, Red, Yellow, Green };

const char* colorCode(Color c) {
    switch (c) {
    case Color::Red: return "\033[1;31m";   // bold red
    case Color::Yellow: return "\033[33m";
    case Color::Green: return "\033[32m";
    case Color::None: break;
    }
    return "";
}

// One line of a cell: what is shown, and in which color.
struct CellLine {
    std::string text;
    Color color = Color::None;
};

// `text` padded to `width` visible characters; the color covers the text
// only, not the padding, so a trimmed row never ends inside a color.
std::string padded(const CellLine& line, std::size_t width, bool colors) {
    const std::string pad(width > line.text.size() ? width - line.text.size() : 0, ' ');
    if (!colors || line.color == Color::None) return line.text + pad;
    return colorCode(line.color) + line.text + "\033[0m" + pad;
}

// The lines inside one cell: a person per line, with their marks, then the
// people still missing. Usually one shift per cell; more are listed in turn.
std::vector<CellLine> cellLines(const Problem& p, const Report& report, const std::vector<int>& shifts,
                                const std::vector<std::string>& names) {
    std::vector<CellLine> lines;
    for (int s : shifts) {
        const ShiftReport& row = report.shifts[s];
        for (std::size_t i = 0; i < row.staff.size(); ++i) {
            const bool breaks = i < row.breaksRule.size() && row.breaksRule[i];   // --check (D51)
            lines.push_back({names[row.staff[i]] + (row.preferred[i] ? "*" : "") + (row.inDouble[i] ? " D" : "") +
                                 (breaks ? " x" : ""),
                             breaks ? Color::Red : row.preferred[i] ? Color::Green : Color::None});
        }
        const int missing = p.shifts[s].wantedStaff - static_cast<int>(row.staff.size());
        if (row.status == ShiftReport::Status::BelowMin) lines.push_back({"!! -" + std::to_string(missing), Color::Red});
        else if (row.status == ShiftReport::Status::BelowWanted) lines.push_back({"! -" + std::to_string(missing), Color::Yellow});
        else if (row.staff.empty()) lines.push_back({"-", Color::None});
    }
    return lines;
}

void printOneWeek(std::ostream& out, const Problem& p, const Report& report, std::size_t w,
                  const std::vector<std::string>& names, bool colors) {
    const WeekReport& week = report.weeks[w];
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // The cells first, to know how wide the columns must be.
    std::vector<std::array<std::vector<CellLine>, 7>> cells(report.kinds.size());
    std::size_t width = 9;   // "Sun 06/12"
    for (std::size_t k = 0; k < report.kinds.size(); ++k)
        for (int d = 0; d < 7; ++d) {
            cells[k][d] = cellLines(p, report, week.cells[k][d], names);
            for (const CellLine& line : cells[k][d]) width = std::max(width, line.text.size());
        }
    width += 2;
    std::size_t labelWidth = 0;
    for (const ShiftKind& kind : report.kinds) labelWidth = std::max(labelWidth, kindLabel(kind).size());
    labelWidth += 2;

    out << "\nWeek " << w + 1 << "  " << week.sunday.toString() << " .. " << (week.sunday + 6).toString() << '\n';
    // Each row is built in a string first, so the padding after the last
    // column can be cut off (no trailing spaces when the text is copied).
    auto emit = [&](const std::string& row) { out << row.substr(0, row.find_last_not_of(' ') + 1) << '\n'; };
    std::ostringstream header;
    header << std::string(labelWidth, ' ');
    for (int d = 0; d < 7; ++d)
        header << std::left << std::setw(static_cast<int>(width)) << (std::string(days[d]) + ' ' + dayMonth(week.sunday + d));
    emit(header.str());
    const std::string rule(labelWidth + 7 * width, '-');
    out << rule << '\n';

    for (std::size_t k = 0; k < report.kinds.size(); ++k) {
        std::size_t height = 1;
        for (int d = 0; d < 7; ++d) height = std::max(height, cells[k][d].size());
        for (std::size_t line = 0; line < height; ++line) {
            std::ostringstream row;
            row << std::left << std::setw(static_cast<int>(labelWidth)) << (line == 0 ? kindLabel(report.kinds[k]) : "");
            for (int d = 0; d < 7; ++d)
                row << padded(line < cells[k][d].size() ? cells[k][d][line] : CellLine{}, width, colors);
            emit(row.str());
        }
        out << rule << '\n';
    }

    out << "Week " << w + 1 << ": " << week.shifts << " shifts, " << week.full << " full";
    if (week.belowWanted) out << ", " << week.belowWanted << " below wanted";
    if (week.belowMin) out << ", " << week.belowMin << " below min";
    out << '\n';
    if (week.shifts == 0) return;
    out << "Shifts this week:";
    int onLine = 0;
    for (std::size_t e = 0; e < p.employees.size(); ++e) {
        if (onLine == 8) { out << "\n                 "; onLine = 0; }
        out << ' ' << names[e] << ' ' << week.shiftsPerEmployee[e] << (e + 1 < p.employees.size() ? "," : "");
        ++onLine;
    }
    out << '\n';
}

}  // namespace

void printWeeks(std::ostream& out, const Problem& p, const Report& report, std::optional<int> onlyWeek,
                bool colors) {
    out << "Schedule " << p.periodStart.toString() << " .. " << p.periodEnd.toString()
        << "  (" << report.weeks.size() << (report.weeks.size() == 1 ? " week" : " weeks") << ")\n";
    out << "  * preferred   D part of a double   ! below wanted   !! below min (see the list below)";
    // --check (D51): only when something is marked, so other output stays as it was.
    const bool anyBroken = std::any_of(report.shifts.begin(), report.shifts.end(), [](const ShiftReport& r) {
        return std::find(r.breaksRule.begin(), r.breaksRule.end(), 1) != r.breaksRule.end();
    });
    if (anyBroken) out << "   x breaks a rule";
    out << "\n";
    const std::vector<std::string> names = shortNames(p);
    for (std::size_t w = 0; w < report.weeks.size(); ++w)
        if (!onlyWeek || static_cast<int>(w) + 1 == *onlyWeek) printOneWeek(out, p, report, w, names, colors);
}

void printPeriodSummary(std::ostream& out, const Problem& p, const Report& report) {
    out << "\nWhole period, per employee\n";
    for (const EmployeeReport& row : report.employees) {
        out << "  " << std::left << std::setw(18) << p.employees[row.employee].name
            << std::right << std::setw(3) << row.shifts.size() << " shifts " << std::setw(4) << row.minutes / 60 << " h";
        if (row.belowMinShifts) out << "  (below minimum " << *row.belowMinShifts << ")";
        out << '\n';
    }
    for (const ScoreLine& line : report.scoreLines)
        if (line.rule == "fairness" && line.weight != 0)
            out << "  Fairness: " << line.violations << "  (0 = shifts spread as evenly as possible)\n";
}

// ---- --check (stage 7.2, D51) -------------------------------------------------

void printViolations(std::ostream& out, const Problem& p, const CheckReport& check) {
    if (check.clean()) {
        out << "\nRules: no violations. Every assignment respects availability, roles and the hard rules.\n";
    } else {
        out << "\nRule violations (" << check.violations.size()
            << "): nothing was blocked, each is listed at the later of the shifts involved\n";
        for (const Violation& v : check.violations) {
            const Shift& shift = p.shifts[v.shift];
            const Employee& employee = p.employees[v.employee];
            std::string rule, why;
            switch (v.kind) {
            case Violation::Kind::Availability:
                rule = "availability";
                why = "not available for " + shift.date.toString() + " " + shift.time.toString();
                break;
            case Violation::Kind::Role:
                rule = "role";
                why = "does not have the role '" + shift.role + "'";
                break;
            case Violation::Kind::Rule:
                rule = v.rule;
                why = v.why;
                break;
            }
            out << "  " << std::left << std::setw(10) << shift.id << ' ' << std::setw(18) << employee.name << ' '
                << std::setw(22) << rule << why << '\n';
        }
    }
    for (const OverStaffed& o : check.overStaffed)
        out << "  note: " << p.shifts[o.shift].id << " has " << o.staff << " people, it wants " << o.wanted << '\n';
}

// ---- --explain (step 6.4, D43, D47) -------------------------------------------

void printExplanation(std::ostream& out, const Problem& p, const State& schedule, const Explanation& e) {
    const Employee& employee = p.employees[e.employee];
    const Shift& shift = p.shifts[e.shift];
    auto shiftText = [&](const Shift& s) {
        return s.id + " (" + weekdayName(s.date) + " " + s.date.toString() + " " + s.time.toString() + ")";
    };
    auto line = [&](const std::string& what, bool ok, const std::string& detail) {
        out << "  " << std::left << std::setw(22) << what << std::setw(9) << (ok ? "ok" : "BLOCKED") << detail << '\n';
    };

    out << "Can " << employee.name << " (#" << employee.id << ") work " << shiftText(shift) << "?\n";
    std::vector<int> theirs = schedule.shiftsOf(e.employee);
    std::sort(theirs.begin(), theirs.end());
    out << "  In this schedule " << employee.name << " works:";
    if (theirs.empty()) out << " nothing";
    for (int s : theirs) out << ' ' << p.shifts[s].id;
    out << "\n  " << shift.id << " has " << e.staff.size() << '/' << shift.wantedStaff << ": "
        << (e.staff.empty() ? "nobody" : names(p, e.staff)) << "\n";
    if (e.alreadyWorks)
        out << "  " << employee.name << " already works " << shift.id
            << " here; the rules below are checked as if they did not.\n";
    out << '\n';

    // Availability: the employee's slots on that day, so the answer can be checked by eye.
    std::string slots;
    for (const AvailabilitySlot& slot : employee.availability)
        if (slot.date == shift.date)
            slots += (slots.empty() ? "" : ", ") + std::string(toString(slot.type)) + ' ' +
                     (slot.time ? slot.time->toString() : "all day") + (slot.allowsDouble ? " (double ok)" : "");
    const std::string fit = e.availability == Fit::Preferred ? "preferred" : e.availability == Fit::Yes ? "available" : "";
    line("availability", e.availability != Fit::No,
         (fit.empty() ? std::string("not available for the whole shift") : fit) +
             (slots.empty() ? "; nothing given for " + shift.date.toString() : "; that day: " + slots));

    std::string roles;
    for (const std::string& r : employee.roles) roles += (roles.empty() ? "" : ", ") + r;
    line("role", e.roleOk, shift.role.empty() ? "no role needed"
                           : "needs '" + shift.role + "', has: " + (roles.empty() ? "none" : roles));

    for (const RuleCheck& check : e.rules) line(check.rule, !check.whyNot, check.whyNot ? *check.whyNot : "");

    out << '\n';
    if (e.canWork()) {
        out << "Answer: yes, nothing forbids it.";
        if (!e.alreadyWorks && static_cast<int>(e.staff.size()) >= shift.wantedStaff)
            out << " (But " << shift.id << " is already full: " << e.staff.size() << '/' << shift.wantedStaff << ".)";
        out << '\n';
    } else {
        std::string blockers;
        if (e.availability == Fit::No) blockers += "availability";
        if (!e.roleOk) blockers += std::string(blockers.empty() ? "" : ", ") + "role";
        for (const RuleCheck& check : e.rules)
            if (check.whyNot) blockers += (blockers.empty() ? "" : ", ") + check.rule;
        out << "Answer: no. Blocked by: " << blockers << '\n';
    }
}
